// クック済み地形を登録し、区画の分割配置と所有リソースの返却を行う。
#include "Component/TerrainComponent.h"

#include "Asset/AssetSystem.h"
#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedTerrainFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Component/MegaGeometryComponent.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Rendering/CookedTextureUpload.h"
#include "Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/Terrain/TerrainMaterialDesc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>

namespace NorvesLib::Core::Component
{
    namespace
    {
        using TextureBytes = Asset::AssetBlob::ByteArray;

        uint8_t ToUnorm8(float value)
        {
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        uint8_t LinearToSrgb8(float value)
        {
            const float linear = std::clamp(value, 0.0f, 1.0f);
            const float srgb = linear <= 0.0031308f ? linear * 12.92f :
                1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return ToUnorm8(srgb);
        }

        Rendering::TextureHandle UploadTerrainTexture(
            RHI::IDevice* device, Rendering::TextureResources& textures,
            const Container::TSharedPtr<TextureBytes>& bytes, uint32_t width, uint32_t height,
            uint32_t layerCount, const Container::String& name)
        {
            Asset::CookedTextureData data;
            data.SourceBlob = Asset::AssetBlob::FromOwnedBytes(bytes);
            data.Width = width;
            data.Height = height;
            data.LayerCount = layerCount;
            data.MipCount = 1;
            data.PixelFormat = Asset::CookedTexturePixelFormat::RGBA8UNorm;
            data.ColorSpace = Asset::CookedTextureColorSpace::Linear;
            data.Mips.push_back({0, bytes->size(), width, height});
            auto uploaded = Rendering::CreateAndUploadCookedTexture(device, data, name);
            if (!uploaded.Succeeded())
            {
                LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED texture=%s status=%u テクスチャを作れません",
                            name.c_str(), static_cast<uint32_t>(uploaded.Status));
                return Rendering::TextureHandle::Invalid();
            }
            const auto handle = textures.RegisterExternalTexture(uploaded.Texture, name);
            if (!handle.IsValid())
            {
                LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED texture=%s テクスチャを登録できません", name.c_str());
            }
            return handle;
        }
    }

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
        LoadSplatMaterial(*heightField);
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

    void TerrainComponent::LoadSplatMaterial(const Rendering::Terrain::TerrainHeightField& heightField)
    {
        const Container::String tablePath = GetLayerTablePath();
        const uint32_t sourceLayerCount = heightField.GetSplatLayerCount();
        if (tablePath.empty() || sourceLayerCount == 0)
        {
            return;
        }

        Rendering::Terrain::TerrainMaterialDesc desc;
        Container::AnsiString error;
        if (!Rendering::Terrain::LoadTerrainMaterialDesc(
                Container::AnsiStringView(tablePath.data(), tablePath.size()), heightField, desc, error))
        {
            LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED path=%s 層の表を読めません: %s", tablePath.c_str(), error.c_str());
            return;
        }
        const auto device = Engine::GEngine->GetRenderWorld().GetRenderingCoordinator().GetDevice();
        if (!device)
        {
            LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED path=%s 描画デバイスがありません", tablePath.c_str());
            return;
        }

        const auto layout = heightField.GetGridLayout();
        const auto weights = heightField.GetSplatWeights();
        const uint64_t vertexCount64 = static_cast<uint64_t>(layout.Columns) * layout.Rows;
        const uint32_t layerCount = static_cast<uint32_t>(
            std::min(desc.Layers.size(), static_cast<size_t>(Rendering::MegaGeometry::MegaTerrainSplatMaxLayers)));
        if (layout.Columns == 0 || layout.Rows == 0 || sourceLayerCount > Rendering::MegaGeometry::MegaTerrainSplatMaxLayers ||
            layerCount == 0 || vertexCount64 > std::numeric_limits<uint32_t>::max() / 4u ||
            vertexCount64 > std::numeric_limits<size_t>::max() / 8u ||
            weights.size() != vertexCount64 * sourceLayerCount)
        {
            LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED path=%s スプラットの格子または層が不正です", tablePath.c_str());
            return;
        }

        try
        {
            const size_t vertexCount = static_cast<size_t>(vertexCount64);
            auto splatBytes = Container::MakeShared<TextureBytes>();
            splatBytes->resize(vertexCount * 8u);
            std::memset(splatBytes->data(), 0, splatBytes->size());
            for (size_t vertex = 0; vertex < vertexCount; ++vertex)
            {
                for (uint32_t layer = 0; layer < sourceLayerCount; ++layer)
                {
                    const size_t target = static_cast<size_t>(layer / 4u) * vertexCount * 4u + vertex * 4u + layer % 4u;
                    (*splatBytes)[target] = weights[vertex * sourceLayerCount + layer];
                }
            }

            constexpr uint32_t textureSide = 4u;
            constexpr size_t pixelsPerLayer = textureSide * textureSide;
            const uint32_t arrayLayers = std::max(2u, layerCount);
            auto albedoBytes = Container::MakeShared<TextureBytes>();
            auto normalBytes = Container::MakeShared<TextureBytes>();
            auto armBytes = Container::MakeShared<TextureBytes>();
            albedoBytes->resize(static_cast<size_t>(arrayLayers) * pixelsPerLayer * 4u);
            normalBytes->resize(albedoBytes->size());
            armBytes->resize(albedoBytes->size());
            for (uint32_t layer = 0; layer < arrayLayers; ++layer)
            {
                const auto& source = desc.Layers[layer < layerCount ? layer : 0];
                const uint8_t albedo[4] = {LinearToSrgb8(source.PlaceholderColor[0]),
                                           LinearToSrgb8(source.PlaceholderColor[1]),
                                           LinearToSrgb8(source.PlaceholderColor[2]), 255u};
                const uint8_t normal[4] = {128u, 128u, 255u, 255u};
                const uint8_t arm[4] = {255u, ToUnorm8(source.PlaceholderRoughness), 0u, 128u};
                for (size_t pixel = 0; pixel < pixelsPerLayer; ++pixel)
                {
                    const size_t offset = (static_cast<size_t>(layer) * pixelsPerLayer + pixel) * 4u;
                    std::memcpy(albedoBytes->data() + offset, albedo, sizeof(albedo));
                    std::memcpy(normalBytes->data() + offset, normal, sizeof(normal));
                    std::memcpy(armBytes->data() + offset, arm, sizeof(arm));
                }
            }

            auto& textures = m_RenderResources->Textures();
            m_Splat.SplatArrayTexture = UploadTerrainTexture(device.get(), textures, splatBytes,
                layout.Columns, layout.Rows, 2u, "TerrainSplatWeights");
            if (m_Splat.SplatArrayTexture.IsValid())
            {
                m_Splat.AlbedoArrayTexture = UploadTerrainTexture(device.get(), textures, albedoBytes,
                    textureSide, textureSide, arrayLayers, "TerrainLayerAlbedo");
            }
            if (m_Splat.AlbedoArrayTexture.IsValid())
            {
                m_Splat.NormalArrayTexture = UploadTerrainTexture(device.get(), textures, normalBytes,
                    textureSide, textureSide, arrayLayers, "TerrainLayerNormal");
            }
            if (m_Splat.NormalArrayTexture.IsValid())
            {
                m_Splat.ArmArrayTexture = UploadTerrainTexture(device.get(), textures, armBytes,
                    textureSide, textureSide, arrayLayers, "TerrainLayerArm");
            }
            if (!m_Splat.ArmArrayTexture.IsValid())
            {
                ReleaseSplatTextures();
                return;
            }

            m_Splat.LayerCount = layerCount;
            m_Splat.bNormalTwoChannel = false;
            m_Splat.SplatMinXZ[0] = static_cast<float>(layout.FirstX - 0.5 * layout.Spacing);
            m_Splat.SplatMinXZ[1] = static_cast<float>(layout.FirstZ - 0.5 * layout.Spacing);
            m_Splat.SplatMaxXZ[0] = static_cast<float>(layout.FirstX - 0.5 * layout.Spacing + layout.Columns * layout.Spacing);
            m_Splat.SplatMaxXZ[1] = static_cast<float>(layout.FirstZ - 0.5 * layout.Spacing + layout.Rows * layout.Spacing);
            m_Splat.CliffLayer = desc.CliffLayer;
            m_Splat.CliffSlopeDegrees[0] = desc.CliffSlopeDegrees[0];
            m_Splat.CliffSlopeDegrees[1] = desc.CliffSlopeDegrees[1];
            m_Splat.HeightBlendDepth = desc.HeightBlendDepth;
            m_Splat.UvRotationWavelength = desc.UvRotationWavelength;
            m_Splat.UvRotationDegrees = desc.UvRotationDegrees;
            for (uint32_t layer = 0; layer < layerCount; ++layer)
            {
                const auto& source = desc.Layers[layer];
                auto& target = m_Splat.Layers[layer];
                target.TileMeters = source.TileMeters;
                target.Wettability = source.Wettability;
                size_t nameBytes = std::min(source.SurfaceName.size(), sizeof(target.SurfaceName) - 1u);
                // UTF-8 の途中で切らず、配列の末尾に必ず終端を置く。
                while (nameBytes > 0 && nameBytes < source.SurfaceName.size() &&
                       (static_cast<uint8_t>(source.SurfaceName[nameBytes]) & 0xC0u) == 0x80u)
                {
                    --nameBytes;
                }
                std::memcpy(target.SurfaceName, source.SurfaceName.data(), nameBytes);
                target.SurfaceName[nameBytes] = '\0';
            }
            m_Splat.bEnabled = true;
            LOG_INFO("TERRAIN_COMPONENT_LAYERS path=%s layers=%u source=placeholder", tablePath.c_str(), layerCount);
        }
        catch (const std::exception& exception)
        {
            ReleaseSplatTextures();
            LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED path=%s 材質を作れません: %s", tablePath.c_str(), exception.what());
        }
        catch (...)
        {
            ReleaseSplatTextures();
            LOG_WARNING("TERRAIN_COMPONENT_LAYERS_SKIPPED path=%s 材質を作れません", tablePath.c_str());
        }
    }

    void TerrainComponent::ReleaseSplatTextures()
    {
        if (m_RenderResources)
        {
            auto& textures = m_RenderResources->Textures();
            const Rendering::TextureHandle handles[] = {m_Splat.AlbedoArrayTexture, m_Splat.NormalArrayTexture,
                                                         m_Splat.ArmArrayTexture, m_Splat.SplatArrayTexture};
            for (const auto handle : handles)
            {
                if (handle.IsValid())
                {
                    textures.ReleaseTexture(handle);
                }
            }
        }
        m_Splat = {};
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
        createInfo.Material.TerrainBlend.Splat = m_Splat;
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
        ReleaseSplatTextures();
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
