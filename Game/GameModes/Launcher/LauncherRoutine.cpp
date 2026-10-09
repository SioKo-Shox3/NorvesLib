#include "GameModes/Launcher/LauncherMode.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/SpringArmComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/GameMode/IGameModeController.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Module/ModuleRegistry.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/IImGuiView.h"
#endif

#include <cstdint>
#include <filesystem>
#include <system_error>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes
{

    namespace
    {
        // 夕景の空。太陽は地平線の少し上、カメラの斜め前に置き、地平線に暖色の帯を出す。
        constexpr float kSunAltitudeDegrees = 3.0f;
        constexpr float kSunAzimuthDegrees = 30.0f;
        // カメラの向き（SpringArm の方位角）と仰角。太陽が画面の端の外に来る向きにする。
        constexpr float kCameraYawDegrees = 155.0f;
        constexpr float kCameraPitchDegrees = -3.0f;
        constexpr float kCameraArmLength = 10.0f;
        constexpr float kCameraPivotHeight = 2.0f;
        // 夕景の手動露出（EV100）。絞り f/16・ISO 100 のままシャッター速度で合わせる。
        constexpr float kExposureEV100 = 11.5f;
        // SceneReady を出すまでの Tick の数（ImGui の最初のフレームが描かれるまでの余裕）
        constexpr uint32_t kReadyTickCount = 3;

        /// シーンの必要な資産が1つでも無いか
        bool IsAnyAssetMissing(const Game::Scenes::SceneDescriptor &scene)
        {
            for (std::size_t index = 0; index < scene.RequiredAssetCount; ++index)
            {
                std::filesystem::path resolvedPath(scene.RequiredAssets[index]);
#if defined(NORVES_SOURCE_ASSET_DIR)
                // 作業ディレクトリは実行ファイルのディレクトリなので、リポジトリの Assets を基準に探す。
                resolvedPath = std::filesystem::path(NORVES_SOURCE_ASSET_DIR).parent_path() / resolvedPath;
#endif
                std::error_code error;
                if (!std::filesystem::exists(resolvedPath, error) || error)
                {
                    return true;
                }
            }
            return false;
        }

        /// 夕景の空と、空だけの画面のための描画設定を入れる
        void ApplyLauncherEnvironment(GameModeContext &ctx)
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

        /// カメラの現在の状態を描画側へ渡す
        void PublishCamera(GameModeContext &ctx, LauncherData &data)
        {
            if (data.m_pSpringArm == nullptr || data.m_pCamera == nullptr)
            {
                return;
            }
            data.m_pSpringArm->RefreshOwnerTransform();
            CameraProxy proxy;
            if (data.m_pCamera->BuildCameraProxy(proxy))
            {
                ctx.EngineRef.GetRenderWorld().SetMainCamera(proxy);
            }
        }

        bool CreateCamera(GameModeContext &ctx, LauncherData &data)
        {
            data.m_pCameraPivot = ctx.ScopeRef.SpawnObject<Entity>();
            data.m_pCameraObject = ctx.ScopeRef.SpawnObject<Entity>();
            if (data.m_pCameraPivot == nullptr || data.m_pCameraObject == nullptr)
            {
                return false;
            }
            data.m_pCameraPivot->SetPosition(0.0f, kCameraPivotHeight, 0.0f);
            data.m_pSpringArm = ctx.WorldRef.CreateComponent<Component::SpringArmComponent>(data.m_pCameraObject);
            data.m_pCamera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(data.m_pCameraObject);
            if (data.m_pSpringArm == nullptr || data.m_pCamera == nullptr ||
                !data.m_pSpringArm->SetPivot(data.m_pCameraPivot))
            {
                return false;
            }
            data.m_pSpringArm->SetArmLength(kCameraArmLength);
            data.m_pSpringArm->SetYaw(kCameraYawDegrees);
            data.m_pSpringArm->SetPitch(kCameraPitchDegrees);

            data.m_pCamera->SetActiveCamera(true);
            data.m_pCamera->SetAperture(16.0f);
            data.m_pCamera->SetISO(100.0f);
            data.m_pCamera->SetShutterSpeed(
                ComputeShutterSpeedForEV100(data.m_pCamera->GetAperture(), data.m_pCamera->GetISO(), kExposureEV100));
            data.m_pCamera->SetExposureCompensation(0.0f);
            data.m_pCamera->SetGradingContrast(1.0f);
            data.m_pCamera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
            return true;
        }
    } // namespace

    GameModeEnterResult LauncherRoutine::Enter(GameModeContext &ctx, LauncherData &data)
    {
        LOG_INFO("ランチャーを開始します");
        data.m_bLeaving = false;
        data.m_bFallbackToFirstScene = false;
        data.m_TickCount = 0;
        data.m_Menu.Reset(static_cast<int>(Game::Scenes::kSceneCatalogCount) + 1);
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        if (!CreateCamera(ctx, data))
        {
            LOG_ERROR("ランチャーのカメラを作れませんでした");
            data.m_pCameraPivot = nullptr;
            data.m_pCameraObject = nullptr;
            data.m_pSpringArm = nullptr;
            data.m_pCamera = nullptr;
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        // 地物の無い空だけの画面なので、間接光（RTGI）は要らない。Leave で戻す。
        auto &coordinator = ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator();
        data.m_bPreviousRTGI = coordinator.IsRTGIEnabled();
        coordinator.SetRTGIEnabled(false);
        ApplyLauncherEnvironment(ctx);
        PublishCamera(ctx, data);

        ctx.EngineRef.GetInputRouter().RegisterController(&data.m_Menu,
                                                          NorvesLib::Core::Input::InputRouter::PriorityGame);
        data.m_bMenuRegistered = true;

#if defined(NORVES_ENABLE_IMGUI)
        if (Module::GetModuleRegistry().FindModule(Identity("NorvesImGuiModule")) != nullptr)
        {
            for (std::size_t index = 0; index < Game::Scenes::kSceneCatalogCount; ++index)
            {
                data.m_View.SetAssetsMissing(index, IsAnyAssetMissing(Game::Scenes::kSceneCatalog[index]));
            }
            NorvesLib::Modules::Gui::RegisterImGuiView(&data.m_View);
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(true);
            data.m_bViewRegistered = true;
        }
        else
        {
            LOG_WARNING("ランチャーのメニューを出せません（ImGui モジュールが登録されていません）。先頭のシーンへ入ります");
            data.m_bFallbackToFirstScene = true;
        }
#else
        LOG_WARNING("ランチャーのメニューを出せません（NORVES_ENABLE_IMGUI なしのビルド）。先頭のシーンへ入ります");
        data.m_bFallbackToFirstScene = true;
#endif
        return GameModeEnterResult::Succeeded;
    }

    void LauncherRoutine::Tick(GameModeContext &ctx, LauncherData &data, float)
    {
        ++data.m_TickCount;
        PublishCamera(ctx, data);

        if (!data.m_bLeaving)
        {
            int activated = -1;
            if (data.m_bFallbackToFirstScene)
            {
                activated = 0;
            }
            else if (!data.m_Menu.ConsumeActivation(activated))
            {
                activated = -1;
            }

            if (activated >= 0 && activated < static_cast<int>(Game::Scenes::kSceneCatalogCount))
            {
                const Game::Scenes::SceneDescriptor &scene = Game::Scenes::kSceneCatalog[activated];
                LOG_INFO_F("LAUNCHER_ENTER scene=%s", scene.Id);
                data.m_bLeaving = true;
                ctx.ControllerRef.RequestChange(scene.Mode, data.m_SceneParams);
            }
            else if (activated >= 0)
            {
                LOG_INFO("LAUNCHER_QUIT");
                data.m_bLeaving = true;
                ctx.EngineRef.RequestExit(0);
            }
        }

        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kLauncherSceneId,
                                          data.m_TickCount >= kReadyTickCount);
    }

    void LauncherRoutine::Leave(GameModeContext &ctx, LauncherData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);

#if defined(NORVES_ENABLE_IMGUI)
        if (data.m_bViewRegistered)
        {
            NorvesLib::Modules::Gui::UnregisterImGuiView(&data.m_View);
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(false);
            data.m_bViewRegistered = false;
        }
#endif
        if (data.m_bMenuRegistered)
        {
            ctx.EngineRef.GetInputRouter().UnregisterController(&data.m_Menu);
            data.m_bMenuRegistered = false;
        }

        // 空と RTGI は RenderWorld の設定なので、ほかのモードへ持ち越さないよう戻す。
        // カメラの実体は GameModeScope::Cleanup が返す。
        ResetEnvironment(ctx);
        ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().SetRTGIEnabled(data.m_bPreviousRTGI);
        data.m_pCameraPivot = nullptr;
        data.m_pCameraObject = nullptr;
        data.m_pSpringArm = nullptr;
        data.m_pCamera = nullptr;
        LOG_INFO("ランチャーを終了します");
    }

} // namespace Game::GameModes
