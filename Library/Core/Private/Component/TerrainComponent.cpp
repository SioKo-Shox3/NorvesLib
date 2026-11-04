// クック済み地形を登録し、区画の分割配置と所有リソースの返却を行う。
#include "Component/TerrainComponent.h"

#include "Asset/AssetSystem.h"
#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedTerrainFormat.h"
#include "Component/MegaGeometryComponent.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Rendering/RenderResources.h"

#include <cstdio>
#include <limits>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(TerrainComponent, Component)

    TerrainComponent::TerrainComponent()
        : Component()
    {
        SetDefaults();
    }

    TerrainComponent::TerrainComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    TerrainComponent::TerrainComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    TerrainComponent::~TerrainComponent()
    {
    }

    void TerrainComponent::SetDefaults()
    {
        SetBaseColor(0.30f, 0.27f, 0.22f);
        SetRoughness(0.9f);
        SetCastShadow(true);
        SetTilesPerFrame(2u);
        SetTickEnabled(false);
    }

    void TerrainComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();

        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!Engine::GEngine || !world)
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED エンジンまたは所有者の World がありません");
            return;
        }
        m_World = world;
        m_OwnerEntityId = owner->GetObjectId();
        m_TerrainSystem = &Engine::GEngine->GetTerrainSystem();
        m_RenderResources = &Engine::GEngine->GetRenderResources();

        if (!world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "Terrain.PlaceTiles";
            tick.Run = [](Component& component, float)
            { static_cast<TerrainComponent&>(component).PlacePendingTiles(); };
            tick.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type);
            world->RegisterComponentTick(StaticClass(), tick);
        }
        SetTickEnabled(true);
    }

    bool TerrainComponent::LoadTerrain()
    {
        auto assetSystem = m_TerrainSystem->GetAssetSystem();
        if (!assetSystem)
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED 地形を読む資産システムがありません");
            return false;
        }

        const Container::String terrainPath = GetTerrainPath();
        const auto resolved = assetSystem->ResolveAsset(terrainPath, Asset::AssetKind::World);
        if (!resolved.UsedCooked())
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED path=%s クック済み地形を読めません", terrainPath.c_str());
            return false;
        }
        const auto parsed = Asset::ParseCookedTerrain(resolved.Blob);
        if (!parsed.Succeeded())
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED path=%s status=%u 地形を解析できません",
                        terrainPath.c_str(), static_cast<uint32_t>(parsed.Status));
            return false;
        }
        const uint64_t tileCount = static_cast<uint64_t>(parsed.Terrain.TileCountX) * parsed.Terrain.TileCountZ;
        if (tileCount == 0 || tileCount > std::numeric_limits<uint32_t>::max())
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED 区画の数が扱える範囲にありません");
            return false;
        }
        Container::AnsiString error;
        auto heightField = Rendering::Terrain::TerrainHeightField::CreateFromCookedTerrain(parsed.Terrain, error);
        if (!heightField)
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED path=%s 高さ場を作れません: %s",
                        terrainPath.c_str(), error.c_str());
            return false;
        }
        const auto handle = m_TerrainSystem->RegisterTerrain(heightField);
        if (!handle.IsValid())
        {
            LOG_WARNING("TERRAIN_COMPONENT_SKIPPED 高さ場を登録できません");
            return false;
        }

        m_AssetSystem = assetSystem;
        m_HeightField = heightField;
        m_TerrainHandle = handle;
        m_Tiles.reserve(static_cast<size_t>(tileCount));
        for (uint32_t z = 0; z < parsed.Terrain.TileCountZ; ++z)
        {
            for (uint32_t x = 0; x < parsed.Terrain.TileCountX; ++x)
            {
                m_Tiles.push_back({x, z});
            }
        }

        return true;
    }

    void TerrainComponent::PlacePendingTiles()
    {
        if (!bBegunPlay || !m_TerrainSystem || m_bLoadFailed)
        {
            return;
        }
        if (!m_TerrainHandle.IsValid())
        {
            if (GetTerrainPath().empty())
            {
                return;
            }
            if (!LoadTerrain())
            {
                m_bLoadFailed = true;
                SetTickEnabled(false);
                return;
            }
        }
        const uint32_t configuredBudget = GetTilesPerFrame();
        const uint32_t budget = configuredBudget == 0 ? 1u : configuredBudget;
        for (uint32_t processed = 0; processed < budget && m_NextTile < GetTileCount(); ++processed)
        {
            if (PlaceTile(m_Tiles[m_NextTile]))
            {
                ++m_PlacedTileCount;
            }
            ++m_NextTile;
        }
        if (IsFullyPlaced())
        {
            SetTickEnabled(false);
            LOG_INFO("TERRAIN_COMPONENT tiles=%u placed=%u", GetTileCount(), GetPlacedTileCount());
        }
    }

    bool TerrainComponent::PlaceTile(const TileCoordinate& tile)
    {
        char suffix[64];
        std::snprintf(suffix, sizeof(suffix), "%u_%u.generated", tile.X, tile.Z);
        Container::String logicalPath = GetTilePathPrefix();
        logicalPath += suffix;
        const auto resolved = m_AssetSystem->ResolveAsset(logicalPath, Asset::AssetKind::Model);
        if (!resolved.UsedCooked())
        {
            LOG_WARNING("TERRAIN_COMPONENT_TILE_SKIPPED path=%s 区画のメッシュを読めません", logicalPath.c_str());
            return false;
        }
        const auto parsed = Asset::ParseCookedMesh(resolved.Blob);
        if (!parsed.Succeeded())
        {
            LOG_WARNING("TERRAIN_COMPONENT_TILE_SKIPPED path=%s status=%u 区画のメッシュを解析できません",
                        logicalPath.c_str(), static_cast<uint32_t>(parsed.Status));
            return false;
        }
        Rendering::MegaGeometry::MegaMeshCreateInfo createInfo;
        if (!Rendering::MegaGeometry::BuildMegaMeshCreateInfoFromCookedMesh(parsed.Mesh, createInfo))
        {
            LOG_WARNING("TERRAIN_COMPONENT_TILE_SKIPPED path=%s MegaMesh の入力を作れません", logicalPath.c_str());
            return false;
        }
        Asset::AssetCookedRange pageRange;
        if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
        {
            createInfo.PageSource = Rendering::MegaGeometry::MakeCookedMeshPageSource(
                parsed.Mesh, m_AssetSystem->GetCookedFileReader(), pageRange.Request, pageRange.BaseOffset);
        }
        GetBaseColor(createInfo.Material.BaseColor[0], createInfo.Material.BaseColor[1], createInfo.Material.BaseColor[2]);
        createInfo.Material.Roughness = GetRoughness();
        createInfo.Material.Metallic = 0.0f;
        createInfo.Material.TerrainBlend.bTerrainSurface = true;
        createInfo.DebugName = logicalPath.c_str();
        auto& megaGeometry = m_RenderResources->MegaGeometry();
        const auto megaMesh = megaGeometry.CreateMegaMesh(createInfo);
        if (!megaMesh.IsValid())
        {
            LOG_WARNING("TERRAIN_COMPONENT_TILE_SKIPPED path=%s MegaMesh を作れません", logicalPath.c_str());
            return false;
        }
        const auto model = megaGeometry.RegisterModel(megaMesh, logicalPath, logicalPath);
        if (!model.IsValid())
        {
            megaGeometry.ReleaseMegaMesh(megaMesh);
            LOG_WARNING("TERRAIN_COMPONENT_TILE_SKIPPED path=%s モデルを登録できません", logicalPath.c_str());
            return false;
        }

        Entity* child = m_World->SpawnEntity<Entity>(GetOwner());
        auto* component = child ? m_World->CreateComponent<MegaGeometryComponent>(child) : nullptr;
        if (!component)
        {
            if (child)
            {
                m_World->RemoveEntity(child);
            }
            megaGeometry.ReleaseModel(model);
            LOG_WARNING("TERRAIN_COMPONENT_TILE_SKIPPED path=%s 区画の Entity を作れません", logicalPath.c_str());
            return false;
        }
        child->SetPosition(0.0f, 0.0f, 0.0f);
        component->SetMegaMeshHandle(megaMesh);
        component->SetCastShadow(GetCastShadow());
        m_ChildEntityIds.push_back(child->GetObjectId());
        m_Models.push_back(model);
        return true;
    }

    void TerrainComponent::EndPlay()
    {
        if (m_bEndingPlay || (!bBegunPlay && !m_World && !m_RenderResources && !m_TerrainSystem))
        {
            return;
        }
        m_bEndingPlay = true;
        SetTickEnabled(false);
        // World / Entity の破棄では Finalize より先に部分木が World から外れる。
        // 所有者が見つからない、または祖先までのどこかが破棄待ちなら、子は Inner の連鎖に任せる。
        Entity* owner = m_World ? m_World->FindEntityByObjectId(m_OwnerEntityId) : nullptr;
        bool bOwnerSurvives = owner != nullptr;
        for (Entity* ancestor = owner; ancestor; ancestor = ancestor->GetParentEntity())
        {
            if (ancestor->IsPendingDestroy())
            {
                bOwnerSurvives = false;
                break;
            }
        }
        if (bOwnerSurvives)
        {
            // Component 単体の解除では、すでに消えた子を ObjectId で避けて、残る子だけを外す。
            // Tick / cleanup 中なら RemoveEntity が破棄を予約し、安全な回収位置まで待つ。
            for (uint64_t id : m_ChildEntityIds)
            {
                if (Entity* child = m_World->FindEntityByObjectId(id))
                {
                    m_World->RemoveEntity(child);
                }
            }
        }
        m_ChildEntityIds.clear();
        // モデルはここで返す。子の連鎖破棄が残っていても、GPU 資源はエンジンが使用完了まで保持する。
        if (m_RenderResources)
        {
            for (Rendering::ModelHandle model : m_Models)
            {
                m_RenderResources->MegaGeometry().ReleaseModel(model);
            }
        }
        m_Models.clear();
        if (m_TerrainSystem && m_TerrainHandle.IsValid())
        {
            m_TerrainSystem->UnregisterTerrain(m_TerrainHandle);
        }
        m_TerrainHandle = {};
        m_HeightField.reset();
        m_AssetSystem.reset();
        m_Tiles.clear();
        m_NextTile = 0;
        m_PlacedTileCount = 0;
        m_bLoadFailed = false;
        m_World = nullptr;
        m_OwnerEntityId = 0;
        m_RenderResources = nullptr;
        m_TerrainSystem = nullptr;
        Component::EndPlay();
        m_bEndingPlay = false;
    }
} // namespace NorvesLib::Core::Component
