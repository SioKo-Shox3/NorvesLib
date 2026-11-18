// レベルマーカーの編集と、マーカーのルートだけのシーン保存・読み込みを実装する。
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
} // namespace NorvesLib::Modules::Gameplay::Level
