// レベルマーカーと地形マスクの編集・保存・読み込みを実装する。
#include "Gameplay/Level/LevelEditOperations.h"

#include "Gameplay/Level/AnimalSpawnPointComponent.h"
#include "Gameplay/Level/GameplayComponentClasses.h"
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/LevelMarkerSnap.h"
#include "Gameplay/Level/PoiMarkerComponent.h"
#include "Gameplay/Level/RestPointComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Gameplay/Level/TerritoryMarkerComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Container/StringBuilder.h"
#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"
#include "Scene/SceneSerializer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <Windows.h>

namespace NorvesLib::Modules::Gameplay::Level
{
    namespace
    {
        using Core::Container::String;
        using Markers = Core::Container::VariableArray<Core::Entity*>;

        // 高さ場への光線は実距離 1m ごとに調べ、交差した区間を 16 回二分する。
        constexpr float kGroundRayStep = 1.0f;
        constexpr uint32_t kGroundRayBisections = 16;
        // 持ち場の球は中心から 10m を既定の範囲とする。
        constexpr float kTerritoryRadius = 10.0f;
        // Key の番号は 1 から始め、最低 3 桁のゼロ埋めにする。
        constexpr uint64_t kFirstKeyNumber = 1;
        constexpr int kKeyNumberDigits = 3;

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool NormalizeRay(const Math::Ray& ray, Math::Ray& outRay)
        {
            const float length = ray.Direction.Length();
            if (!IsFinite(ray.Origin) || !IsFinite(ray.Direction) || !std::isfinite(length) || !(length > 0.0f))
            {
                return false;
            }
            outRay = Math::Ray(ray.Origin, ray.Direction / length);
            return true;
        }

        template<typename T>
        T* GetLiveComponent(const Core::Entity& entity)
        {
            T* component = entity.GetComponent<T>();
            return component != nullptr && !component->IsPendingDestroy() ? component : nullptr;
        }

        bool IsMarker(const Core::Entity& entity)
        {
            return GetLiveComponent<LevelMarkerComponent>(entity) != nullptr
                || GetLiveComponent<ScentTrailPointComponent>(entity) != nullptr;
        }

        bool IsLive(const Core::Entity& entity)
        {
            for (const Core::Entity* current = &entity; current != nullptr; current = current->GetParentEntity())
            {
                if (current->IsPendingDestroy())
                {
                    return false;
                }
            }
            return true;
        }

        void CollectEntity(Core::Entity* entity, Markers& outMarkers)
        {
            if (entity == nullptr || entity->IsPendingDestroy())
            {
                return;
            }
            if (IsMarker(*entity))
            {
                outMarkers.push_back(entity);
            }
            for (Core::Entity* child : entity->GetChildEntities())
            {
                CollectEntity(child, outMarkers);
            }
        }

        void NotifyBeforeDestroy(Core::Entity& entity, ILevelEditEntityOwner* owner)
        {
            if (owner == nullptr)
            {
                return;
            }
            owner->OnLevelEditBeforeDestroy(entity);
            for (Core::Entity* child : entity.GetChildEntities())
            {
                if (child != nullptr)
                {
                    NotifyBeforeDestroy(*child, owner);
                }
            }
        }

        const LevelMarkerComponent* GetSnapSettings(const Core::Entity& entity)
        {
            if (GetLiveComponent<ScentTrailPointComponent>(entity) != nullptr)
            {
                const Core::Entity* parent = entity.GetParentEntity();
                return parent != nullptr && GetLiveComponent<ScentTrailComponent>(*parent) != nullptr
                    ? GetLiveComponent<LevelMarkerComponent>(*parent) : nullptr;
            }
            return GetLiveComponent<LevelMarkerComponent>(entity);
        }

        void SnapMarker(const Core::Scene::SceneQuery& query, Core::Entity& entity)
        {
            const auto* settings = GetSnapSettings(entity);
            if (settings == nullptr || !settings->getbSnapToGround() || !std::isfinite(settings->getGroundOffset()))
            {
                return;
            }
            Math::Transform transform = entity.GetWorldTransform();
            Math::Vector3 ground;
            Math::Vector3 normal;
            if (Core::Scene::LevelSnap::SnapToGround(query, transform.position, ground, normal,
                LevelEditOperations::kDefaultMaxDrop))
            {
                transform.position.y = ground.y + settings->getGroundOffset();
                entity.SetWorldTransform(transform);
            }
        }

        void SnapChildTrailPoints(Core::World& world, const Core::Scene::SceneQuery& query, Core::Entity& entity)
        {
            for (Core::Entity* child : entity.GetChildEntities())
            {
                if (child == nullptr || child->IsPendingDestroy())
                {
                    continue;
                }
                if (GetLiveComponent<ScentTrailPointComponent>(*child) != nullptr)
                {
                    SnapMarker(query, *child);
                    world.UpdateWorldTransforms();
                }
                SnapChildTrailPoints(world, query, *child);
            }
        }

        void PrunePendingSnapshot(const Core::Entity& entity, Core::EntitySubtreeSnapshotNode& node)
        {
            // Projector と同じく null とクラス無しを飛ばし、生成順に実体とスナップショットを対応させる。
            Core::Container::VariableArray<Core::ComponentSubtreeSnapshot> components;
            components.reserve(node.Components.size());
            size_t index = 0;
            for (const auto* component : entity.GetComponents())
            {
                if (component == nullptr || component->GetClass() == nullptr)
                {
                    continue;
                }
                if (index >= node.Components.size())
                {
                    break;
                }
                auto& snapshot = node.Components[index++];
                if (!component->IsPendingDestroy())
                {
                    components.push_back(std::move(snapshot));
                }
            }
            node.Components = std::move(components);

            Core::Container::VariableArray<Core::EntitySubtreeSnapshotNode> children;
            children.reserve(node.Children.size());
            index = 0;
            for (const Core::Entity* child : entity.GetChildEntities())
            {
                if (child == nullptr || child->GetClass() == nullptr)
                {
                    continue;
                }
                if (index >= node.Children.size())
                {
                    break;
                }
                auto& snapshot = node.Children[index++];
                if (!child->IsPendingDestroy())
                {
                    PrunePendingSnapshot(*child, snapshot);
                    children.push_back(std::move(snapshot));
                }
            }
            node.Children = std::move(children);
        }

        bool SaveError(String* pOutError, const char* message)
        {
            if (pOutError != nullptr)
            {
                *pOutError = message;
            }
            NORVES_LOG_ERROR("LevelEdit", "%s", message);
            return false;
        }

        bool IsPaintMaskDirectoryValid(const String& directory)
        {
            return !directory.empty() && std::strlen(directory.c_str()) == directory.size();
        }

        String MakePaintMaskPath(const String& directory, ELevelPaintLayer layer)
        {
            String path = directory;
            if (path.back() != '/' && path.back() != '\\')
            {
                path += "/";
            }
            return path + GetLevelPaintLayerFileStem(layer) + ".png";
        }

        bool PaintMaskFileError(String* pOutError, const String& path, const String& error)
        {
            const String message = String("マスク PNG '") + path + "': " + error;
            return SaveError(pOutError, message.c_str());
        }

        // ファイルの不在だけを基準値へ戻す。アクセス拒否や不正なパスは読み込み失敗とする。
        bool ReadPaintMaskFile(LevelPaintMask& mask, const String& path, bool& bOutLoaded, String* pOutError)
        {
            bOutLoaded = false;
            if (path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
                std::strlen(path.c_str()) != path.size())
            {
                return PaintMaskFileError(pOutError, path, "パスが長すぎるか、不正な文字を含んでいます。");
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
            if (count <= 0)
            {
                return PaintMaskFileError(pOutError, path, "パスを UTF-8 として読み取れませんでした。");
            }
            Core::Container::VariableArray<wchar_t> widePath(static_cast<size_t>(count));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, widePath.data(), count) != count)
            {
                return PaintMaskFileError(pOutError, path, "パスの変換に失敗しました。");
            }
            const DWORD attributes = GetFileAttributesW(widePath.data());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return mask.SetValues(mask.GetBaseline());
                }
                return PaintMaskFileError(pOutError, path, "ファイルの有無を確認できませんでした。");
            }
            Core::Container::VariableArray<uint8_t> values;
            String error;
            if (!LoadLevelPaintMaskPng(path, mask.GetLayout(), values, &error))
            {
                return PaintMaskFileError(pOutError, path, error);
            }
            if (!mask.SetValues(Core::Container::Span<const uint8_t>(values)))
            {
                return PaintMaskFileError(pOutError, path, "標本数が配置と合いません。");
            }
            bOutLoaded = true;
            return true;
        }
    } // namespace

    const char* GetLevelMarkerKindId(ELevelMarkerKind kind)
    {
        switch (kind)
        {
        case ELevelMarkerKind::Territory: return "territory";
        case ELevelMarkerKind::RestPoint: return "rest";
        case ELevelMarkerKind::FastTravel: return "fasttravel";
        case ELevelMarkerKind::Poi: return "poi";
        case ELevelMarkerKind::AnimalSpawn: return "spawn";
        case ELevelMarkerKind::ScentTrail: return "trail";
        case ELevelMarkerKind::ScentTrailPoint: return "trailpoint";
        default: return "";
        }
    }

    bool TryGetLevelMarkerKind(const Core::Entity& entity, ELevelMarkerKind& outKind)
    {
        if (!IsLive(entity))
        {
            return false;
        }
        if (GetLiveComponent<ScentTrailPointComponent>(entity) != nullptr)
        {
            outKind = ELevelMarkerKind::ScentTrailPoint;
        }
        else if (GetLiveComponent<TerritoryMarkerComponent>(entity) != nullptr)
        {
            outKind = ELevelMarkerKind::Territory;
        }
        else if (const auto* rest = GetLiveComponent<RestPointComponent>(entity))
        {
            outKind = rest->getbFastTravel() ? ELevelMarkerKind::FastTravel : ELevelMarkerKind::RestPoint;
        }
        else if (GetLiveComponent<PoiMarkerComponent>(entity) != nullptr)
        {
            outKind = ELevelMarkerKind::Poi;
        }
        else if (GetLiveComponent<AnimalSpawnPointComponent>(entity) != nullptr)
        {
            outKind = ELevelMarkerKind::AnimalSpawn;
        }
        else if (GetLiveComponent<ScentTrailComponent>(entity) != nullptr)
        {
            outKind = ELevelMarkerKind::ScentTrail;
        }
        else
        {
            return false;
        }
        return true;
    }

    void LevelEditOperations::SetEntityOwner(ILevelEditEntityOwner* owner)
    {
        m_pEntityOwner = owner;
    }

    ILevelEditEntityOwner* LevelEditOperations::GetEntityOwner() const
    {
        return m_pEntityOwner;
    }

    bool LevelEditOperations::RaycastGround(const Core::Scene::SceneQuery& query, const Math::Ray& ray,
        float maxDistance, Math::Vector3& outPoint, Math::Vector3& outNormal) const
    {
        Math::Ray normalizedRay;
        if (!std::isfinite(maxDistance) || maxDistance < 0.0f || !NormalizeRay(ray, normalizedRay))
        {
            return false;
        }
        Core::Scene::PhysicsQueryDesc desc;
        desc.Kind = Core::Scene::EPhysicsQueryKind::RaycastClosest;
        desc.Ray = normalizedRay;
        desc.MaxDistance = maxDistance;
        desc.Filter.TriggerPolicy = Core::Scene::EPhysicsQueryTriggerPolicy::Exclude;
        Core::Container::VariableArray<Core::Scene::PhysicsQueryHit> hits;
        if (query.ExecuteQuery(desc, hits) == Core::Scene::EPhysicsSceneQueryResult::Success && !hits.empty())
        {
            outPoint = hits[0].Point;
            outNormal = hits[0].Normal;
            return true;
        }

        float previousDistance = 0.0f;
        float distance = 0.0f;
        bool bPreviousAbove = false;
        for (;;)
        {
            Core::Scene::GroundQueryHit ground;
            const bool bGround = query.QueryGround(normalizedRay.PointAt(distance), maxDistance, ground, desc.Filter)
                == Core::Scene::EPhysicsSceneQueryResult::Success && ground.bHit;
            if (bGround && ground.Distance == 0.0f)
            {
                outPoint = normalizedRay.PointAt(distance);
                outNormal = ground.Normal;
                return true;
            }
            if (bGround && ground.Distance < 0.0f && bPreviousAbove)
            {
                float lower = previousDistance;
                float upper = distance;
                for (uint32_t iteration = 0; iteration < kGroundRayBisections; ++iteration)
                {
                    const float middle = lower + (upper - lower) * 0.5f;
                    Core::Scene::GroundQueryHit middleGround;
                    if (query.QueryGround(normalizedRay.PointAt(middle), maxDistance, middleGround, desc.Filter)
                        != Core::Scene::EPhysicsSceneQueryResult::Success || !middleGround.bHit)
                    {
                        return false;
                    }
                    if (middleGround.Distance > 0.0f)
                    {
                        lower = middle;
                    }
                    else
                    {
                        upper = middle;
                        ground = middleGround;
                    }
                }
                outPoint = normalizedRay.PointAt(upper);
                outNormal = ground.Normal;
                return true;
            }
            bPreviousAbove = bGround && ground.Distance > 0.0f;
            previousDistance = distance;
            if (distance >= maxDistance)
            {
                break;
            }
            const float next = distance + kGroundRayStep;
            if (next <= distance)
            {
                break;
            }
            distance = next < maxDistance ? next : maxDistance;
        }
        return false;
    }

    Core::Entity* LevelEditOperations::PlaceMarker(Core::World& world, const Core::Scene::SceneQuery& query,
        ELevelMarkerKind kind, const Math::Vector3& groundPoint, const String& requestedKey)
    {
        if (GetLevelMarkerKindId(kind)[0] == '\0' || !IsFinite(groundPoint))
        {
            return nullptr;
        }
        Core::Entity* parent = nullptr;
        if (kind == ELevelMarkerKind::ScentTrailPoint)
        {
            parent = GetSelected(world);
            if (parent != nullptr && GetLiveComponent<ScentTrailPointComponent>(*parent) != nullptr)
            {
                parent = parent->GetParentEntity();
            }
            if (parent == nullptr || GetLiveComponent<ScentTrailComponent>(*parent) == nullptr
                || GetLiveComponent<LevelMarkerComponent>(*parent) == nullptr || !IsLive(*parent))
            {
                return nullptr;
            }
        }
        const String key = requestedKey.empty() ? MakeUniqueKey(world, kind) : requestedKey;
        if (key.empty() || IsKeyInUse(world, key))
        {
            return nullptr;
        }
        Core::Entity* entity = world.SpawnEntity<Core::Entity>(parent);
        if (entity == nullptr)
        {
            return nullptr;
        }
        auto* marker = world.CreateComponent<LevelMarkerComponent>(entity);
        if (marker == nullptr)
        {
            entity->Destroy();
            return nullptr;
        }
        marker->getKey() = key;
        bool bCreated = false;
        switch (kind)
        {
        case ELevelMarkerKind::Territory:
            if (world.CreateComponent<TerritoryMarkerComponent>(entity) != nullptr)
            {
                if (auto* zone = world.CreateComponent<World::EncounterZoneComponent>(entity))
                {
                    zone->getShape() = World::EEncounterZoneShape::Sphere;
                    zone->getRadius() = kTerritoryRadius;
                    zone->getEnemyId() = String{};
                    zone->getProfileId() = String{};
                    // EnemyId が空でもセンサーは交戦状態になるため、配置用の領域は全センサーを除外する。
                    zone->getSensorMask() = 0;
                    bCreated = true;
                }
            }
            break;
        case ELevelMarkerKind::RestPoint:
        case ELevelMarkerKind::FastTravel:
            if (auto* rest = world.CreateComponent<RestPointComponent>(entity))
            {
                rest->getbFastTravel() = kind == ELevelMarkerKind::FastTravel;
                bCreated = true;
            }
            break;
        case ELevelMarkerKind::Poi:
            bCreated = world.CreateComponent<PoiMarkerComponent>(entity) != nullptr;
            break;
        case ELevelMarkerKind::AnimalSpawn:
            bCreated = world.CreateComponent<AnimalSpawnPointComponent>(entity) != nullptr;
            break;
        case ELevelMarkerKind::ScentTrail:
            bCreated = world.CreateComponent<ScentTrailComponent>(entity) != nullptr;
            break;
        case ELevelMarkerKind::ScentTrailPoint:
            bCreated = world.CreateComponent<ScentTrailPointComponent>(entity) != nullptr;
            // 点は親の設定で吸着するため、共通マーカーの列との二重吸着を防ぐ。
            marker->getbSnapToGround() = false;
            break;
        default:
            break;
        }
        if (!bCreated)
        {
            entity->Destroy();
            return nullptr;
        }
        world.UpdateWorldTransforms();
        Math::Transform transform = entity->GetWorldTransform();
        transform.position = groundPoint;
        if (const auto* settings = GetSnapSettings(*entity))
        {
            transform.position.y += settings->getGroundOffset();
        }
        entity->SetWorldTransform(transform);
        world.UpdateWorldTransforms();
        SnapMarker(query, *entity);
        world.UpdateWorldTransforms();
        if (parent == nullptr && m_pEntityOwner != nullptr)
        {
            m_pEntityOwner->OnLevelEditRootSpawned(*entity);
        }
        Select(entity);
        return entity;
    }

    bool LevelEditOperations::IsKeyInUse(const Core::World& world, const String& key, const Core::Entity* ignore) const
    {
        Markers markers;
        CollectMarkers(world, markers);
        for (const Core::Entity* entity : markers)
        {
            const auto* marker = GetLiveComponent<LevelMarkerComponent>(*entity);
            if (entity != ignore && marker != nullptr && *marker->getKey() == key)
            {
                return true;
            }
        }
        return false;
    }

    String LevelEditOperations::MakeUniqueKey(const Core::World& world, ELevelMarkerKind kind) const
    {
        const char* prefix = GetLevelMarkerKindId(kind);
        if (prefix[0] == '\0')
        {
            return {};
        }
        for (uint64_t number = kFirstKeyNumber;; ++number)
        {
            Core::Container::StringBuilder builder(prefix);
            builder.AppendFormat("_%0*llu", kKeyNumberDigits, static_cast<unsigned long long>(number));
            String key = builder.ToString();
            if (!IsKeyInUse(world, key))
            {
                return key;
            }
            if (number == (std::numeric_limits<uint64_t>::max)())
            {
                return {};
            }
        }
    }

    bool LevelEditOperations::SetMarkerKey(Core::World& world, Core::Entity& marker, const String& newKey)
    {
        auto* component = GetLiveComponent<LevelMarkerComponent>(marker);
        if (marker.GetWorld() != &world || !IsLive(marker) || component == nullptr || newKey.empty()
            || IsKeyInUse(world, newKey, &marker))
        {
            return false;
        }
        component->getKey() = newKey;
        return true;
    }

    Core::Entity* LevelEditOperations::FindMarkerByKey(const Core::World& world, const String& key) const
    {
        if (key.empty())
        {
            return nullptr;
        }
        Markers markers;
        CollectMarkers(world, markers);
        for (Core::Entity* entity : markers)
        {
            const auto* marker = GetLiveComponent<LevelMarkerComponent>(*entity);
            if (marker != nullptr && *marker->getKey() == key)
            {
                return entity;
            }
        }
        return nullptr;
    }

    void LevelEditOperations::CollectMarkers(const Core::World& world, Markers& outMarkers) const
    {
        outMarkers.clear();
        for (Core::Entity* root : world.GetRootEntities())
        {
            CollectEntity(root, outMarkers);
        }
    }

    Core::Entity* LevelEditOperations::PickMarker(const Core::World& world, const Math::Ray& ray, float pickRadius) const
    {
        Math::Ray normalizedRay;
        if (!std::isfinite(pickRadius) || pickRadius < 0.0f || !NormalizeRay(ray, normalizedRay))
        {
            return nullptr;
        }
        Markers markers;
        CollectMarkers(world, markers);
        Core::Entity* closest = nullptr;
        float closestDistance = (std::numeric_limits<float>::max)();
        for (Core::Entity* entity : markers)
        {
            const Math::Vector3 offset = entity->GetWorldTransform().position - normalizedRay.Origin;
            const float distance = Math::Vector3::Dot(offset, normalizedRay.Direction);
            if (distance < 0.0f || distance >= closestDistance)
            {
                continue;
            }
            if ((offset - normalizedRay.Direction * distance).Length() <= pickRadius)
            {
                closest = entity;
                closestDistance = distance;
            }
        }
        return closest;
    }

    void LevelEditOperations::Select(const Core::Entity* marker)
    {
        m_SelectedObjectId = marker != nullptr && IsLive(*marker) && IsMarker(*marker) ? marker->GetObjectId() : 0;
    }

    void LevelEditOperations::ClearSelection()
    {
        m_SelectedObjectId = 0;
    }

    Core::Entity* LevelEditOperations::GetSelected(const Core::World& world) const
    {
        return world.FindEntityByObjectId(m_SelectedObjectId);
    }

    uint64_t LevelEditOperations::GetSelectedObjectId() const
    {
        return m_SelectedObjectId;
    }

    bool LevelEditOperations::MoveMarker(Core::World& world, const Core::Scene::SceneQuery& query,
        Core::Entity& marker, const Math::Vector3& position)
    {
        if (marker.GetWorld() != &world || !IsLive(marker) || !IsMarker(marker) || !IsFinite(position))
        {
            return false;
        }
        world.UpdateWorldTransforms();
        Math::Transform transform = marker.GetWorldTransform();
        transform.position = position;
        marker.SetWorldTransform(transform);
        world.UpdateWorldTransforms();
        SnapMarker(query, marker);
        world.UpdateWorldTransforms();
        SnapChildTrailPoints(world, query, marker);
        world.UpdateWorldTransforms();
        return true;
    }

    bool LevelEditOperations::DeleteMarker(Core::World& world, Core::Entity& marker)
    {
        if (marker.GetWorld() != &world || !IsLive(marker) || !IsMarker(marker))
        {
            return false;
        }
        for (const Core::Entity* selected = GetSelected(world); selected != nullptr; selected = selected->GetParentEntity())
        {
            if (selected == &marker)
            {
                ClearSelection();
                break;
            }
        }
        NotifyBeforeDestroy(marker, m_pEntityOwner);
        marker.Destroy();
        return true;
    }

    Core::Scene::LevelSnap::LevelSnapStats LevelEditOperations::ReSnapAll(Core::World& world,
        const Core::Scene::SceneQuery& query, float maxDrop)
    {
        return ReSnapAllLevelMarkers(world, query, maxDrop);
    }

    bool LevelEditOperations::SaveMarkers(const Core::World& world, const String& filePath,
        size_t* pOutSavedRoots, String* pOutError) const
    {
        if (pOutSavedRoots != nullptr)
        {
            *pOutSavedRoots = 0;
        }
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        if (filePath.empty() || std::strlen(filePath.c_str()) != filePath.size())
        {
            return SaveError(pOutError, "保存先のパスが空か、不正な文字を含んでいます。");
        }
        Core::Container::VariableArray<Core::EntitySubtreeSnapshot> roots;
        for (const Core::Entity* root : world.GetRootEntities())
        {
            if (root == nullptr || root->IsPendingDestroy() || root->GetClass() == nullptr
                || GetLiveComponent<LevelMarkerComponent>(*root) == nullptr)
            {
                continue;
            }
            auto snapshot = Core::RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root);
            PrunePendingSnapshot(*root, snapshot.Root);
            roots.push_back(std::move(snapshot));
        }
        const auto document = Core::Scene::SceneSerializer::BuildDocument(roots);
        const String json = Core::Scene::SceneSerializer::ToJson(document);
        if (json.empty())
        {
            return SaveError(pOutError, "マーカーの JSON への変換に失敗しました。");
        }
        const String temporaryPath = filePath + ".tmp";
        std::FILE* file = nullptr;
        if (fopen_s(&file, temporaryPath.c_str(), "wb") != 0 || file == nullptr)
        {
            return SaveError(pOutError, "保存用の一時ファイルを開けませんでした。");
        }
        // 各処理を独立して実行し、書き込み失敗時も必ず閉じて一時ファイルを取り除く。
        const size_t written = std::fwrite(json.c_str(), 1, json.size(), file);
        const bool bFlushed = std::fflush(file) == 0;
        const bool bClosed = std::fclose(file) == 0;
        if (written != json.size() || !bFlushed || !bClosed)
        {
            std::remove(temporaryPath.c_str());
            return SaveError(pOutError, "マーカーの一時ファイルへの書き込みに失敗しました。");
        }
        if (!MoveFileExA(temporaryPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::remove(temporaryPath.c_str());
            return SaveError(pOutError, "マーカーの保存先ファイルの置換に失敗しました。");
        }
        if (pOutSavedRoots != nullptr)
        {
            *pOutSavedRoots = roots.size();
        }
        return true;
    }

    bool LevelEditOperations::LoadMarkers(Core::World& world, const String& filePath, Markers* pOutRoots)
    {
        if (pOutRoots != nullptr)
        {
            pOutRoots->clear();
        }
        String jsonText;
        {
            NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
                filePath,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read);
            if (!stream || !stream->IsOpen())
            {
                NORVES_LOG_ERROR("LevelEdit", "マーカーファイル '%s' を開けません。", filePath.c_str());
                return false;
            }
            jsonText = stream->ReadString();
        }
        Core::Scene::SceneDocument document;
        String error;
        if (!Core::Scene::SceneSerializer::TryParseJson(jsonText, document, &error))
        {
            NORVES_LOG_ERROR("LevelEdit", "マーカーファイル '%s' の解析に失敗しました: %s", filePath.c_str(), error.c_str());
            return false;
        }

        ClearSelection();
        for (Core::Entity* root : world.GetRootEntities())
        {
            if (root != nullptr && !root->IsPendingDestroy() && GetLiveComponent<LevelMarkerComponent>(*root) != nullptr)
            {
                NotifyBeforeDestroy(*root, m_pEntityOwner);
                root->Destroy();
            }
        }
        RegisterGameplayComponentClasses();
        Markers roots;
        const bool bLoaded = Core::Scene::SceneSerializer::LoadIntoWorld(world, filePath, nullptr, &roots);
        for (Core::Entity* root : roots)
        {
            if (m_pEntityOwner != nullptr)
            {
                m_pEntityOwner->OnLevelEditRootSpawned(*root);
            }
            if (pOutRoots != nullptr)
            {
                pOutRoots->push_back(root);
            }
        }
        world.UpdateWorldTransforms();
        return bLoaded;
    }

    bool LevelEditOperations::BindPaintMasks(const LevelPaintMaskLayout& layout, const String& masksDirectory,
        String* pOutError)
    {
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        if (!layout.IsValid())
        {
            return SaveError(pOutError, "マスクの配置が無効です。");
        }
        if (!IsPaintMaskDirectoryValid(masksDirectory))
        {
            return SaveError(pOutError, "マスクの置き場が空か、不正な文字を含んでいます。");
        }
        try
        {
            String directory = masksDirectory;
            LevelPaintMask masks[kLevelPaintLayerCount];
            bool bLoaded[kLevelPaintLayerCount] = {};
            for (uint32_t index = 0; index < kLevelPaintLayerCount; ++index)
            {
                const auto layer = static_cast<ELevelPaintLayer>(index);
                const uint8_t baseline = layer == ELevelPaintLayer::GrassDensity
                    ? kGrassDensityBaselineValue : kDrynessBaselineValue;
                if (!masks[index].Initialize(layout, baseline))
                {
                    return SaveError(pOutError, "マスクの初期化に失敗しました。");
                }
                if (!ReadPaintMaskFile(masks[index], MakePaintMaskPath(directory, layer), bLoaded[index], pOutError))
                {
                    return false;
                }
            }
            // 全層の読み込みが成功してから、確保を伴わない移動で一括反映する。
            for (uint32_t index = 0; index < kLevelPaintLayerCount; ++index)
            {
                m_PaintMasks[index] = std::move(masks[index]);
                m_bPaintMaskLoadedFromFile[index] = bLoaded[index];
            }
            m_PaintMaskDirectory = std::move(directory);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return SaveError(pOutError, "マスクを配置へ合わせるためのメモリを確保できませんでした。");
        }
        catch (const std::length_error&)
        {
            return SaveError(pOutError, "マスクの配列またはパスが長すぎます。");
        }
    }

    void LevelEditOperations::UnbindPaintMasks()
    {
        for (uint32_t index = 0; index < kLevelPaintLayerCount; ++index)
        {
            m_PaintMasks[index].Reset();
            m_bPaintMaskLoadedFromFile[index] = false;
        }
        m_PaintMaskDirectory.clear();
    }

    bool LevelEditOperations::HasPaintMasks() const
    {
        return m_PaintMasks[0].IsValid();
    }

    const LevelPaintMaskLayout& LevelEditOperations::GetPaintMaskLayout() const
    {
        return m_PaintMasks[0].GetLayout();
    }

    const String& LevelEditOperations::GetPaintMaskDirectory() const
    {
        return m_PaintMaskDirectory;
    }

    const LevelPaintMask* LevelEditOperations::GetPaintMask(ELevelPaintLayer layer) const
    {
        const uint32_t index = static_cast<uint32_t>(layer);
        return index < kLevelPaintLayerCount && HasPaintMasks() ? &m_PaintMasks[index] : nullptr;
    }

    bool LevelEditOperations::WasPaintMaskLoadedFromFile(ELevelPaintLayer layer) const
    {
        const uint32_t index = static_cast<uint32_t>(layer);
        return index < kLevelPaintLayerCount && HasPaintMasks() && m_bPaintMaskLoadedFromFile[index];
    }

    uint32_t LevelEditOperations::PaintMask(ELevelPaintLayer layer, const Math::Vector3& groundPoint,
        const LevelPaintBrush& brush)
    {
        const uint32_t index = static_cast<uint32_t>(layer);
        return index < kLevelPaintLayerCount && HasPaintMasks()
            ? m_PaintMasks[index].Paint(groundPoint.x, groundPoint.z, brush) : 0;
    }

    uint32_t LevelEditOperations::EraseMask(ELevelPaintLayer layer, const Math::Vector3& groundPoint,
        const LevelPaintBrush& brush)
    {
        const uint32_t index = static_cast<uint32_t>(layer);
        return index < kLevelPaintLayerCount && HasPaintMasks()
            ? m_PaintMasks[index].Erase(groundPoint.x, groundPoint.z, brush) : 0;
    }

    bool LevelEditOperations::SetPaintMaskBaseline(ELevelPaintLayer layer,
        Core::Container::Span<const uint8_t> baseline, bool bResetValues)
    {
        const uint32_t index = static_cast<uint32_t>(layer);
        return index < kLevelPaintLayerCount && HasPaintMasks()
            && m_PaintMasks[index].SetBaseline(baseline, bResetValues);
    }

    bool LevelEditOperations::RegenerateDrynessBaseline(const Core::World& world,
        const LevelDrynessBaselineSettings& settings, bool bResetValues, size_t* pOutTerritoryCount,
        size_t* pOutWitherCount, String* pOutError)
    {
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        if (!HasPaintMasks())
        {
            return SaveError(pOutError, "マスクが配置に合わせられていません。");
        }

        Core::Container::VariableArray<uint8_t> values;
        size_t territoryCount = 0;
        size_t witherCount = 0;
        if (!BuildLevelDrynessBaseline(world, GetPaintMaskLayout(), settings, values, &territoryCount, &witherCount))
        {
            return SaveError(pOutError, "枯れの初期値を作れませんでした。設定と配置を確認してください。");
        }
        if (!SetPaintMaskBaseline(ELevelPaintLayer::Dryness, Core::Container::Span<const uint8_t>(values), bResetValues))
        {
            return SaveError(pOutError, "枯れの初期値をマスクに設定できませんでした。");
        }
        if (pOutTerritoryCount != nullptr)
        {
            *pOutTerritoryCount = territoryCount;
        }
        if (pOutWitherCount != nullptr)
        {
            *pOutWitherCount = witherCount;
        }
        return true;
    }

    bool LevelEditOperations::SavePaintMasks(const String& masksDirectory, size_t* pOutSavedLayers, String* pOutError)
    {
        if (pOutSavedLayers != nullptr)
        {
            *pOutSavedLayers = 0;
        }
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        if (!HasPaintMasks())
        {
            return SaveError(pOutError, "マスクが配置に合わせられていません。");
        }
        const String& directory = masksDirectory.empty() ? m_PaintMaskDirectory : masksDirectory;
        if (!IsPaintMaskDirectoryValid(directory))
        {
            return SaveError(pOutError, "マスクの保存先が空か、不正な文字を含んでいます。");
        }
        try
        {
            bool bSuccess = true;
            for (uint32_t index = 0; index < kLevelPaintLayerCount; ++index)
            {
                const String path = MakePaintMaskPath(directory, static_cast<ELevelPaintLayer>(index));
                String error;
                if (SaveLevelPaintMaskPng(m_PaintMasks[index], path, &error))
                {
                    m_PaintMasks[index].ClearDirty();
                }
                else
                {
                    // 全層の保存を試み、最初に失敗した層の説明を返す。
                    PaintMaskFileError(bSuccess ? pOutError : nullptr, path, error);
                    bSuccess = false;
                }
            }
            if (bSuccess && pOutSavedLayers != nullptr)
            {
                *pOutSavedLayers = kLevelPaintLayerCount;
            }
            return bSuccess;
        }
        catch (const std::bad_alloc&)
        {
            return SaveError(pOutError, "マスクの保存に必要なメモリを確保できませんでした。");
        }
        catch (const std::length_error&)
        {
            return SaveError(pOutError, "マスクの保存先パスが長すぎます。");
        }
    }

    bool LevelEditOperations::LoadPaintMasks(String* pOutError)
    {
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        if (!HasPaintMasks())
        {
            return SaveError(pOutError, "マスクが配置に合わせられていません。");
        }
        try
        {
            LevelPaintMask masks[kLevelPaintLayerCount];
            bool bLoaded[kLevelPaintLayerCount] = {};
            for (uint32_t index = 0; index < kLevelPaintLayerCount; ++index)
            {
                // 基準値を含めて複製し、全層の検査が終わるまで現在の状態を保つ。
                masks[index] = m_PaintMasks[index];
                const String path = MakePaintMaskPath(m_PaintMaskDirectory, static_cast<ELevelPaintLayer>(index));
                if (!ReadPaintMaskFile(masks[index], path, bLoaded[index], pOutError))
                {
                    return false;
                }
            }
            for (uint32_t index = 0; index < kLevelPaintLayerCount; ++index)
            {
                m_PaintMasks[index] = std::move(masks[index]);
                m_bPaintMaskLoadedFromFile[index] = bLoaded[index];
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return SaveError(pOutError, "マスクの読み直しに必要なメモリを確保できませんでした。");
        }
        catch (const std::length_error&)
        {
            return SaveError(pOutError, "マスクの読み直しに必要な配列またはパスが長すぎます。");
        }
    }
} // namespace NorvesLib::Modules::Gameplay::Level
