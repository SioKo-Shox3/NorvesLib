// 草原の谷と尾根を、既定の 1 層の材質・物理空・太陽で描く開発用シーン。
// 地形の出どころは GrasslandTerrain.json と GenerateGrasslandHeight.py。クック済みの高さ場と 4×4 の区画を使う。
// near は地表から 0.6 m、valley は谷底から尾根、low は谷沿いを低い角度で見る固定視点。
#include "GameModes/Grassland/GrasslandMode.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/TerrainComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Physics/TerrainColliderBinding.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <cstdint>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace P = NorvesLib::Modules::Physics;

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

        bool CreateTerrain(GameModeContext& ctx, GrasslandData& data)
        {
            Entity* terrain = ctx.ScopeRef.SpawnObject<Entity>();
            if (terrain == nullptr)
            {
                LOG_ERROR("grassland: 地形の Entity を作れませんでした");
                return false;
            }
            auto* component = ctx.WorldRef.CreateComponent<Component::TerrainComponent>(terrain);
            if (component == nullptr)
            {
                LOG_ERROR("grassland: 地形の Component を作れませんでした");
                return false;
            }
            component->SetTerrainPath("Assets/Terrain/Grassland/Grassland.nvtrn");
            component->SetTilePathPrefix("Assets/Terrain/Grassland/Tiles/GrasslandTile_");
            data.m_pTerrain = component;
            return true;
        }

        void ApplyEnvironment(GameModeContext& ctx)
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
        void ResetEnvironment(GameModeContext& ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }
    } // namespace

    GameModeEnterResult GrasslandRoutine::Enter(GameModeContext& ctx, GrasslandData& data)
    {
        LOG_INFO("grassland を開始します");
        data.m_TickCount = 0;
        data.m_pTerrain = nullptr;
        data.m_pTerrainCollider = nullptr;
        data.m_bTerrainColliderAttempted = false;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kGrasslandSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("grassland: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateTerrain(ctx, data))
        {
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("grassland: カメラを作れませんでした");
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        Component::CameraComponent* camera = data.m_Rig.GetCamera();
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
        camera->SetExposureCompensation(0.0f);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
        return GameModeEnterResult::Succeeded;
    }

    void GrasslandRoutine::Tick(GameModeContext& ctx, GrasslandData& data, float deltaTime)
    {
        ++data.m_TickCount;
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor* scene = Game::Scenes::FindScene(Game::Scenes::kGrasslandSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        // すべての区画が揃ってから撮影を許可する。欠損区画を含む地形を準備完了として扱わない。
        const bool bTerrainReady = data.m_pTerrain != nullptr && data.m_pTerrain->IsFullyPlaced() &&
                                   data.m_pTerrain->GetPlacedTileCount() == data.m_pTerrain->GetTileCount();
        if (bTerrainReady && !data.m_bTerrainColliderAttempted)
        {
            data.m_bTerrainColliderAttempted = true;
            const P::EPhysicsResult result = P::AttachTerrainCollider(ctx.WorldRef, *data.m_pTerrain, data.m_pTerrainCollider);
            if (result != P::EPhysicsResult::Success)
            {
                LOG_WARNING("grassland: 地形のコライダーを作れませんでした (result=%u)", static_cast<unsigned>(result));
            }
        }
        const bool bReady = bTerrainReady && data.m_TickCount >= kReadyTickCount && !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kGrasslandSceneId, bReady);
    }

    void GrasslandRoutine::Leave(GameModeContext& ctx, GrasslandData& data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        data.m_Rig.Leave(ctx);
        data.m_pTerrain = nullptr;
        data.m_pTerrainCollider = nullptr;
        data.m_bTerrainColliderAttempted = false;
        // 地形とカメラの Entity は GameModeScope::Cleanup が返す。TerrainComponent::EndPlay で区画を返す。
        // 空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("grassland を終了します");
    }

} // namespace Game::GameModes
