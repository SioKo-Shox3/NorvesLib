#include "GameModes/AssetPreview/AssetPreviewMode.h"

// 確認用シーンは開発ビルドだけ。
#if NORVES_BUILD_DEVELOPMENT

#include "Core/Private/Resource/SkinnedMaterialBuilder.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Platform/PlatformFile.h"
#include "Core/Public/Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "Core/Public/Thread/Thread.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <cmath>
#include <cstdint>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes
{
    namespace Math = NorvesLib::Math;

    namespace
    {
        constexpr float kGroundSize = 200.0f;
        constexpr float kTileMeters = 8.0f;
        constexpr uint32_t kTextureSize = 512;
        constexpr uint32_t kCellPixels = kTextureSize / 8;
        constexpr uint32_t kReadyTickCount = 3;
        constexpr float kAxisLength = 0.3f;
        constexpr float kAxisThickness = 0.02f;
        constexpr uint32_t kGroundMeshId = 1300;
        constexpr uint32_t kAxisMeshIds[3] = {1301, 1302, 1303};

        void ApplyEnvironment(GameModeContext& ctx)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = 40.0f;
            sky.SunAzimuthDegrees = 30.0f;
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        void ResetEnvironment(GameModeContext& ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        TextureHandle CreateGridTexture(GameModeContext& ctx)
        {
            VariableArray<uint8_t> pixels(kTextureSize * kTextureSize * 4);
            for (uint32_t y = 0; y < kTextureSize; ++y)
            {
                for (uint32_t x = 0; x < kTextureSize; ++x)
                {
                    const bool bLine = x % kCellPixels < 2 || y % kCellPixels < 2;
                    const uint8_t value = bLine ? 76 : 195;
                    const uint32_t index = (y * kTextureSize + x) * 4;
                    pixels[index] = value;
                    pixels[index + 1] = value;
                    pixels[index + 2] = value;
                    pixels[index + 3] = 255;
                }
            }
            TextureCreateInfo info;
            info.Width = kTextureSize;
            info.Height = kTextureSize;
            info.PixelFormat = TextureCreateInfo::Format::RGBA8_SRGB;
            info.DebugName = "AssetPreviewGrid";
            return ctx.RenderResourcesRef.Textures().CreateTexture(info, pixels.data(), pixels.size());
        }

        bool RegisterMesh(GameModeContext& ctx, uint32_t id, const VariableArray<Mesh3DVertex>& vertices,
                          const VariableArray<uint32_t>& indices)
        {
            const MeshDataHandle mesh{id};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                    static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                    indices.data(), static_cast<uint32_t>(indices.size())))
            {
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);
            return true;
        }

        bool SpawnMesh(GameModeContext& ctx, uint32_t id, MaterialHandle material, const Math::Vector3& position,
                       const Math::Vector3& scale = Math::Vector3(1.0f), bool bReceiveShadow = true)
        {
            Entity* entity = ctx.ScopeRef.SpawnObject<Entity>();
            if (entity == nullptr)
            {
                return false;
            }
            entity->SetPosition(position);
            entity->SetScale(scale);
            auto* component = ctx.WorldRef.CreateComponent<Component::MeshComponent>(entity);
            if (component == nullptr)
            {
                return false;
            }
            component->SetMeshHandle(MeshDataHandle{id});
            component->SetMaterial(0, material);
            component->SetCastShadow(false);
            component->SetReceiveShadow(bReceiveShadow);
            return true;
        }

        bool CreateGround(GameModeContext& ctx)
        {
            const TextureHandle grid = CreateGridTexture(ctx);
            if (!grid.IsValid())
            {
                LOG_ERROR("asset-preview: 格子のテクスチャを作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackTexture(grid);

            MaterialCreateData materialData;
            materialData.DebugName = "AssetPreviewGround";
            materialData.AlbedoTexture = grid;
            materialData.Metallic = 0.0f;
            materialData.Roughness = 0.9f;
            const MaterialHandle material = ctx.RenderResourcesRef.Materials().Create(materialData);
            if (!material.IsValid())
            {
                LOG_ERROR("asset-preview: 地面の材質を作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMaterial(material);

            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(kGroundSize, kGroundSize, 1, 1, vertices, indices);
            for (Mesh3DVertex& vertex : vertices)
            {
                vertex.TexCoord[0] = vertex.Position[0] / kTileMeters;
                vertex.TexCoord[1] = vertex.Position[2] / kTileMeters;
            }
            return RegisterMesh(ctx, kGroundMeshId, vertices, indices) &&
                   SpawnMesh(ctx, kGroundMeshId, material, Math::Vector3(0.0f));
        }

        // 箱の生成 API がないため、平面生成で作った 6 面を軸に合わせて組み立てる。
        void BuildAxisMesh(uint32_t direction, VariableArray<Mesh3DVertex>& vertices, VariableArray<uint32_t>& indices)
        {
            vertices.clear();
            indices.clear();
            float size[3] = {kAxisThickness, kAxisThickness, kAxisThickness};
            size[direction] = kAxisLength;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const uint32_t uAxis = axis == 0 ? 2 : 0;
                const uint32_t vAxis = axis == 1 ? 2 : 1;
                for (int sign = -1; sign <= 1; sign += 2)
                {
                    VariableArray<Mesh3DVertex> face;
                    VariableArray<uint32_t> faceIndices;
                    ProceduralMeshGenerator::GeneratePlane(size[uAxis], size[vAxis], 1, 1, face, faceIndices);
                    const uint32_t base = static_cast<uint32_t>(vertices.size());
                    for (Mesh3DVertex& vertex : face)
                    {
                        const float u = vertex.Position[0];
                        const float v = vertex.Position[2] * static_cast<float>(sign) * (axis == 2 ? -1.0f : 1.0f);
                        vertex.Position[0] = vertex.Position[1] = vertex.Position[2] = 0.0f;
                        vertex.Position[direction] = kAxisLength * 0.5f;
                        vertex.Position[axis] += static_cast<float>(sign) * size[axis] * 0.5f;
                        vertex.Position[uAxis] += u;
                        vertex.Position[vAxis] += v;
                        vertex.Normal[0] = vertex.Normal[1] = vertex.Normal[2] = 0.0f;
                        vertex.Normal[axis] = static_cast<float>(sign);
                        vertices.push_back(vertex);
                    }
                    for (const uint32_t index : faceIndices)
                    {
                        indices.push_back(base + index);
                    }
                }
            }
        }

        bool CreateAxes(GameModeContext& ctx, AssetPreviewData& data)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                VariableArray<Mesh3DVertex> vertices;
                VariableArray<uint32_t> indices;
                BuildAxisMesh(axis, vertices, indices);
                if (!RegisterMesh(ctx, kAxisMeshIds[axis], vertices, indices))
                {
                    LOG_ERROR("asset-preview: 原点の印のメッシュを登録できませんでした");
                    return false;
                }
                MaterialCreateData materialData;
                materialData.DebugName = "AssetPreviewAxis";
                materialData.Metallic = 0.0f;
                materialData.Roughness = 0.7f;
                materialData.BaseColor[0] = axis == 0 ? 1.0f : 0.02f;
                materialData.BaseColor[1] = axis == 1 ? 1.0f : 0.02f;
                materialData.BaseColor[2] = axis == 2 ? 1.0f : 0.02f;
                data.m_AxisMaterials[axis] = ctx.RenderResourcesRef.Materials().Create(materialData);
                if (!data.m_AxisMaterials[axis].IsValid())
                {
                    LOG_ERROR("asset-preview: 原点の印の材質を作れませんでした");
                    return false;
                }
                ctx.ScopeRef.TrackMaterial(data.m_AxisMaterials[axis]);
            }

            // 格子の繰り返しに色を焼かず、世界原点を通る 2 本だけを着色する。
            const float stretch = kGroundSize / kAxisLength;
            return SpawnMesh(ctx, kAxisMeshIds[0], data.m_AxisMaterials[0],
                             Math::Vector3(-kGroundSize * 0.5f, -0.009f, 0.0f),
                             Math::Vector3(stretch, 1.0f, 1.0f)) &&
                   SpawnMesh(ctx, kAxisMeshIds[2], data.m_AxisMaterials[2],
                             Math::Vector3(0.0f, -0.008f, -kGroundSize * 0.5f),
                             Math::Vector3(1.0f, 1.0f, stretch));
        }

        bool SpawnOrigin(GameModeContext& ctx, const AssetPreviewData& data, const Math::Vector3& position)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!SpawnMesh(ctx, kAxisMeshIds[axis], data.m_AxisMaterials[axis], position,
                               Math::Vector3(1.0f), false))
                {
                    return false;
                }
            }
            return true;
        }

        void ReportMissing(const AssetPreviewData& data, const String& path, const String& reason)
        {
            LOG_WARNING_F("ASSET_PREVIEW_MISSING path=%s reason=%s", path.c_str(), reason.c_str());
            if (data.m_SceneReportPath.empty())
            {
                return;
            }

            VariableArray<uint8_t> bytes;
            const auto read = Platform::ReadFileBytes(data.m_SceneReportPath, bytes);
            if (!read.bSuccess && read.Kind != Platform::FileErrorKind::NotFound)
            {
                LOG_ERROR_F("ASSET_PREVIEW_REPORT_WRITE_FAILED path=%s 報告を読み込めませんでした",
                            data.m_SceneReportPath.c_str());
                return;
            }
            if (!bytes.empty() && bytes.back() != '\n')
            {
                bytes.push_back('\n');
            }
            const String line = String("missing_asset=") + path + " reason=" + reason + "\n";
            for (const char character : line)
            {
                bytes.push_back(static_cast<uint8_t>(character));
            }
            if (!Platform::WriteFileAtomic(data.m_SceneReportPath, bytes, false).bSuccess)
            {
                LOG_ERROR_F("ASSET_PREVIEW_REPORT_WRITE_FAILED path=%s", data.m_SceneReportPath.c_str());
            }
        }

        struct Placement
        {
            bool bSkeletal = false;
            size_t Index = 0;
            Math::AABB Bounds;
            MegaGeometry::MegaMeshHandle MegaMesh;
        };

        bool AllModelsComplete(const AssetPreviewData& data)
        {
            for (const AssetPreviewModelLoad& load : data.m_ModelLoads)
            {
                if (!load.m_State->m_bCompleted.Load())
                {
                    return false;
                }
            }
            return true;
        }

        void PlaceAssets(GameModeContext& ctx, AssetPreviewData& data)
        {
            auto& mega = ctx.RenderResourcesRef.MegaGeometry();
            VariableArray<Placement> placements;
            VariableArray<Math::AABB> bounds;
            for (size_t i = 0; i < data.m_ModelLoads.size(); ++i)
            {
                AssetPreviewModelLoad& load = data.m_ModelLoads[i];
                ModelHandle& handle = load.m_State->m_Handle;
                if (!handle.IsValid())
                {
                    if (load.m_RequestId != 0)
                    {
                        ReportMissing(data, load.m_Path, "load_failed");
                    }
                    continue;
                }
                const auto mesh = mega.GetModelMegaMeshHandle(handle);
                const auto* meshData = mesh.IsValid() ? mega.GetMegaMeshGPUData(mesh) : nullptr;
                Math::AABB modelBounds = Math::AABB::CreateInvalid();
                if (meshData != nullptr && std::isfinite(meshData->TotalBounds.Radius) &&
                    meshData->TotalBounds.Radius > 0.0f)
                {
                    const BoundingSphere& sphere = meshData->TotalBounds;
                    // NVMESH は AABB を持たないので境界球から作る。床から少し浮くことがある。
                    modelBounds = Math::AABB::FromCenterExtents(
                        Math::Vector3(sphere.CenterX, sphere.CenterY, sphere.CenterZ), Math::Vector3(sphere.Radius));
                }
                if (!IsValidAssetPreviewBounds(modelBounds))
                {
                    ReportMissing(data, load.m_Path, "invalid_bounds");
                    mega.ReleaseModel(handle);
                    handle = ModelHandle::Invalid();
                    continue;
                }
                placements.push_back(Placement{false, i, modelBounds, mesh});
                bounds.push_back(modelBounds);
            }

            const auto& skeletal = data.m_SkeletalLoads.GetResults();
            for (size_t i = 0; i < skeletal.size(); ++i)
            {
                const AssetPreviewSkeletalResult& result = skeletal[i];
                if (!result.MissingReason.empty())
                {
                    ReportMissing(data, result.Path, result.MissingReason);
                }
                if (!result.Asset)
                {
                    if (result.MissingReason.empty())
                    {
                        ReportMissing(data, result.Path, "skeletal_resource_load_failed");
                    }
                    continue;
                }
                if (!IsValidAssetPreviewBounds(result.BindBounds))
                {
                    ReportMissing(data, result.Path, "invalid_bounds");
                    continue;
                }
                placements.push_back(Placement{true, i, result.BindBounds, {}});
                bounds.push_back(result.BindBounds);
            }

            VariableArray<Math::Vector3> positions;
            if (!ComputeAssetPreviewLayout(bounds.data(), bounds.size(), kAssetPreviewGapMeters, positions))
            {
                LOG_ERROR_F("ASSET_PREVIEW_LAYOUT_FAILED 資産の配置位置を計算できませんでした count=%zu", bounds.size());
                data.m_bPlaced = true;
                return;
            }

            for (size_t i = 0; i < placements.size(); ++i)
            {
                const Placement& placement = placements[i];
                const Math::Vector3& position = positions[i];
                const String& path = placement.bSkeletal ? skeletal[placement.Index].Path :
                                                           data.m_ModelLoads[placement.Index].m_Path;
                bool bAttached = false;
                if (placement.bSkeletal)
                {
                    bAttached = AttachAssetPreviewSkeletal(ctx, *data.m_MaterialBuilder, skeletal[placement.Index],
                                                          data.m_FallbackMaterial, position,
                                                          data.m_Arguments.TimeSeconds) != nullptr;
                }
                else
                {
                    AssetPreviewModelLoad& load = data.m_ModelLoads[placement.Index];
                    Entity* entity = ctx.ScopeRef.SpawnObject<Entity>();
                    if (entity != nullptr)
                    {
                        entity->SetPosition(position);
                        auto* component = ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(entity);
                        if (component != nullptr)
                        {
                            component->SetMegaMeshHandle(placement.MegaMesh);
                            component->SetCastShadow(true);
                            ctx.ScopeRef.TrackModel(load.m_State->m_Handle);
                            load.m_State->m_Handle = ModelHandle::Invalid();
                            bAttached = true;
                        }
                    }
                }
                if (!bAttached)
                {
                    ReportMissing(data, path, "placement_failed");
                    continue;
                }
                if (!SpawnOrigin(ctx, data, position))
                {
                    ReportMissing(data, path, "origin_marker_failed");
                }
                const Math::Vector3 size = placement.Bounds.Max - placement.Bounds.Min;
                LOG_INFO_F("ASSET_PREVIEW_PLACED kind=%s path=%s x=%.3f y=%.3f z=%.3f size=%.3fx%.3fx%.3f",
                           placement.bSkeletal ? "skeletal" : "model", path.c_str(),
                           position.x, position.y, position.z, size.x, size.y, size.z);
            }
            data.m_bPlaced = true;
        }
    } // namespace

    GameModeEnterResult AssetPreviewRoutine::Enter(GameModeContext& ctx, AssetPreviewData& data)
    {
        LOG_INFO("asset-preview を開始します");
        data.m_TickCount = 0;
        data.m_bPlaced = false;
        data.m_bReady = false;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kAssetPreviewSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("asset-preview: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateGround(ctx) || !CreateAxes(ctx, data))
        {
            LOG_ERROR("asset-preview: 地面または原点の印を作れませんでした");
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        MaterialCreateData fallback;
        fallback.DebugName = "AssetPreviewFallbackSkin";
        fallback.Metallic = 0.0f;
        fallback.Roughness = 0.7f;
        data.m_FallbackMaterial = ctx.RenderResourcesRef.Materials().Create(fallback);
        if (!data.m_FallbackMaterial.IsValid())
        {
            LOG_ERROR("asset-preview: 既定のスキン材質を作れませんでした");
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        ctx.ScopeRef.TrackMaterial(data.m_FallbackMaterial);

        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("asset-preview: カメラを作れませんでした");
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        Component::CameraComponent* camera = data.m_Rig.GetCamera();
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), 15.0f));
        camera->SetExposureCompensation(0.0f);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);

        data.m_MaterialBuilder = MakeShared<ResourceIO::SkinnedMaterialBuilder>(
            ctx.RenderResourcesRef, NorvesLib::Thread::Thread::GetCurrentThreadId());
        data.m_ModelLoads.clear();
        data.m_ModelLoads.reserve(data.m_Arguments.ModelPaths.size());
        for (const String& path : data.m_Arguments.ModelPaths)
        {
            AssetPreviewModelLoad load;
            load.m_Path = path;
            load.m_State = MakeShared<AssetPreviewModelState>();
            const auto state = load.m_State;
            load.m_RequestId = ctx.RenderResourcesRef.MegaGeometry().LoadModelAsync(path,
                [state](ModelHandle handle)
                {
                    state->m_Handle = handle;
                    state->m_bCompleted.Store(true);
                });
            if (load.m_RequestId == 0)
            {
                state->m_bCompleted.Store(true);
                ReportMissing(data, path, "admission_rejected");
            }
            data.m_ModelLoads.push_back(load);
        }
        if (!data.m_Arguments.SkeletalPaths.empty())
        {
            SkeletalAssetRuntime* runtime = nullptr;
            try
            {
                runtime = data.m_GetSkeletalRuntime.InvokeIfBound();
            }
            catch (...)
            {
                runtime = nullptr;
            }
            if (runtime != nullptr)
            {
                data.m_SkeletalLoads.Start(*runtime, data.m_Arguments.SkeletalPaths, data.m_Arguments.ClipName);
            }
            else
            {
                data.m_SkeletalLoads.FailAll(data.m_Arguments.SkeletalPaths, "skeletal_runtime_unavailable");
            }
        }
        if (data.m_Arguments.ModelPaths.empty() && data.m_Arguments.SkeletalPaths.empty())
        {
            LOG_WARNING_F("ASSET_PREVIEW_EMPTY --asset-preview-model・--asset-preview-skeletal で資産を指定してください");
        }
        return GameModeEnterResult::Succeeded;
    }

    void AssetPreviewRoutine::Tick(GameModeContext& ctx, AssetPreviewData& data, float deltaTime)
    {
        ++data.m_TickCount;
        if (const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kAssetPreviewSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
        if (!data.m_bPlaced && AllModelsComplete(data) && data.m_SkeletalLoads.IsComplete())
        {
            PlaceAssets(ctx, data);
        }
        data.m_bReady = data.m_TickCount >= kReadyTickCount && data.m_bPlaced && !data.m_Rig.IsScriptRunning();
        if (data.m_bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kAssetPreviewSceneId, data.m_bReady);
    }

    void AssetPreviewRoutine::Leave(GameModeContext& ctx, AssetPreviewData& data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        data.m_Rig.Leave(ctx);
        auto& mega = ctx.RenderResourcesRef.MegaGeometry();
        for (AssetPreviewModelLoad& load : data.m_ModelLoads)
        {
            if (!load.m_State->m_bCompleted.Load() && load.m_RequestId != 0)
            {
                mega.CancelModelLoad(load.m_RequestId);
            }
            if (load.m_State->m_bCompleted.Load() && load.m_State->m_Handle.IsValid())
            {
                mega.ReleaseModel(load.m_State->m_Handle);
                load.m_State->m_Handle = ModelHandle::Invalid();
            }
        }
        data.m_ModelLoads.clear();
        data.m_SkeletalLoads.Cancel();
        if (data.m_MaterialBuilder)
        {
            data.m_MaterialBuilder->Clear();
            data.m_MaterialBuilder.reset();
        }
        ResetEnvironment(ctx);
        LOG_INFO("asset-preview を終了します");
    }
} // namespace Game::GameModes

#endif
