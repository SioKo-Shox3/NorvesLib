#include "GameModes/Sandbox/SandboxMode.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <cstdint>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes
{

    namespace
    {
        // 空の太陽は仰角 40°・方位 30°（Rendering3DTest の既定と同じ）
        constexpr float kSunAltitudeDegrees = 40.0f;
        constexpr float kSunAzimuthDegrees = 30.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）。
        constexpr float kExposureEV100 = 15.0f;
        // SceneReady を出すまでの Tick の数。決定的な撮影は「読み込み中」を一度観測してから数え始めるので、
        // 最初のフレームは組み立て中として見せる。
        constexpr uint32_t kReadyTickCount = 3;

        // 地面: 一辺 400 m の平面。格子の模様は 8 m ごとに繰り返す（テクスチャ 1 枚 = 8 m 四方、1 升 = 1 m）
        constexpr float kGroundSize = 400.0f;
        constexpr float kTileMeters = 8.0f;
        constexpr uint32_t kTextureSize = 512;
        constexpr uint32_t kCellsPerTile = 8;
        // 他のシーンのメッシュの ID（100〜、200〜）と重ならない値
        constexpr uint32_t kGroundMeshHandleId = 900;

        void SetPixel(NorvesLib::Core::Container::VariableArray<uint8_t> &pixels, uint32_t x, uint32_t y, uint8_t r,
                      uint8_t g, uint8_t b)
        {
            const uint32_t index = (y * kTextureSize + x) * 4;
            pixels[index + 0] = r;
            pixels[index + 1] = g;
            pixels[index + 2] = b;
            pixels[index + 3] = 255;
        }

        /// 8×8 升の格子。市松の明暗の上に、x=0 の升の列を赤、z=0 の升の行を青、両方が重なる升を黄で示す
        TextureHandle CreateGridTexture(GameModeContext &ctx)
        {
            NorvesLib::Core::Container::VariableArray<uint8_t> pixels(kTextureSize * kTextureSize * 4);
            constexpr uint32_t kCellPixels = kTextureSize / kCellsPerTile;
            for (uint32_t y = 0; y < kTextureSize; ++y)
            {
                for (uint32_t x = 0; x < kTextureSize; ++x)
                {
                    const uint32_t cellX = x / kCellPixels;
                    const uint32_t cellZ = y / kCellPixels;
                    const bool bLight = ((cellX + cellZ) % 2) == 0;
                    if (cellX == 0 && cellZ == 0)
                    {
                        SetPixel(pixels, x, y, 230, 200, 60);
                    }
                    else if (cellX == 0)
                    {
                        SetPixel(pixels, x, y, bLight ? 215 : 160, bLight ? 70 : 45, bLight ? 70 : 45);
                    }
                    else if (cellZ == 0)
                    {
                        SetPixel(pixels, x, y, bLight ? 70 : 45, bLight ? 100 : 70, bLight ? 215 : 160);
                    }
                    else
                    {
                        const uint8_t value = bLight ? 190 : 120;
                        SetPixel(pixels, x, y, value, value, value);
                    }
                }
            }

            TextureCreateInfo info;
            info.Width = kTextureSize;
            info.Height = kTextureSize;
            info.PixelFormat = TextureCreateInfo::Format::RGBA8_SRGB;
            info.DebugName = "SandboxGridTexture";
            return ctx.RenderResourcesRef.Textures().CreateTexture(info, pixels.data(), pixels.size());
        }

        bool CreateGround(GameModeContext &ctx)
        {
            const TextureHandle grid = CreateGridTexture(ctx);
            if (!grid.IsValid())
            {
                LOG_ERROR("sandbox: 地面の格子のテクスチャを作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackTexture(grid);

            MaterialCreateData materialData;
            materialData.DebugName = "SandboxGround";
            materialData.AlbedoTexture = grid;
            materialData.Metallic = 0.0f;
            materialData.Roughness = 0.9f;
            const MaterialHandle material = ctx.RenderResourcesRef.Materials().Create(materialData);
            if (!material.IsValid())
            {
                LOG_ERROR("sandbox: 地面の材質を作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMaterial(material);

            NorvesLib::Core::Container::VariableArray<Mesh3DVertex> vertices;
            NorvesLib::Core::Container::VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(kGroundSize, kGroundSize, 1, 1, vertices, indices);
            for (Mesh3DVertex &vertex : vertices)
            {
                // 世界の x・z を 8 m で割った値。原点の升がテクスチャの左上（赤・青・黄の印の升）に来る
                vertex.TexCoord[0] = vertex.Position[0] / kTileMeters;
                vertex.TexCoord[1] = vertex.Position[2] / kTileMeters;
            }
            const MeshDataHandle mesh{kGroundMeshHandleId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR("sandbox: 地面のメッシュを登録できませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);

            Entity *ground = ctx.ScopeRef.SpawnObject<Entity>();
            if (ground == nullptr)
            {
                return false;
            }
            ground->SetPosition(0.0f, 0.0f, 0.0f);
            auto *meshComponent = ctx.WorldRef.CreateComponent<Component::MeshComponent>(ground);
            if (meshComponent == nullptr)
            {
                return false;
            }
            meshComponent->SetMeshHandle(mesh);
            meshComponent->SetMaterial(0, material);
            meshComponent->SetCastShadow(false);
            return true;
        }

        void ApplyEnvironment(GameModeContext &ctx)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = kSunAltitudeDegrees;
            sky.SunAzimuthDegrees = kSunAzimuthDegrees;
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        /// 空の設定を、ほかのモードへ持ち越さないよう既定へ戻す
        void ResetEnvironment(GameModeContext &ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }
    } // namespace

    GameModeEnterResult SandboxRoutine::Enter(GameModeContext &ctx, SandboxData &data)
    {
        LOG_INFO("sandbox を開始します");
        data.m_TickCount = 0;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kSandboxSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("sandbox: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateGround(ctx))
        {
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("sandbox: カメラを作れませんでした");
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        Component::CameraComponent *camera = data.m_Rig.GetCamera();
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
        camera->SetExposureCompensation(0.0f);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
        return GameModeEnterResult::Succeeded;
    }

    void SandboxRoutine::Tick(GameModeContext &ctx, SandboxData &data, float deltaTime)
    {
        ++data.m_TickCount;
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        const bool bReady = data.m_TickCount >= kReadyTickCount && !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kSandboxSceneId, bReady);
    }

    void SandboxRoutine::Leave(GameModeContext &ctx, SandboxData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        data.m_Rig.Leave(ctx);
        // 地面・材質・テクスチャ・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("sandbox を終了します");
    }

} // namespace Game::GameModes
