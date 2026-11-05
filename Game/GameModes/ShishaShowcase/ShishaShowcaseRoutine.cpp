// 場面の入退場と、組み立て完了・撮影用の印を同じ Tick に揃える共通の進行。
#include "GameModes/ShishaShowcase/ShishaShowcaseMode.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseMarker.h"
#include "Scenes/SceneReadiness.h"

#include <cmath>
#include <cstdio>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;

    namespace
    {
        ShishaShowcaseSceneContext MakeSceneContext(GameModeContext& ctx, ShishaShowcaseData& data,
                                                   float deltaTime = 0.0f)
        {
            return {ctx, data, data.m_Camera, data.m_StandinScale, deltaTime, data.m_ElapsedSeconds};
        }

        void ResetEnvironment(GameModeContext& ctx)
        {
            auto& renderWorld = ctx.EngineRef.GetRenderWorld();
            renderWorld.SetSkyAtmosphere(Rendering::MakeDefaultSkyAtmosphereParameters());
            renderWorld.SetVolumetricFogParameters(Rendering::MakeDefaultVolumetricFogParameters());
            renderWorld.SetStaticEnvironmentIntensityScale(1.0f);
            renderWorld.SetStaticEnvironmentMaxRadiance(0.0f);
        }
    } // namespace

    GameModeEnterResult ShishaShowcaseRoutine::Enter(GameModeContext& ctx, ShishaShowcaseData& data)
    {
        data.m_TickCount = 0;
        data.m_ElapsedSeconds = 0.0;
        data.m_bReady = false;
        data.m_ReadyKeyValues[0] = '\0';
        const Container::AnsiString name(data.m_SceneName.c_str());
        data.m_pScene = FindShishaShowcaseScene(name.c_str());
        if (data.m_pScene == nullptr)
        {
            LOG_ERROR("シ者の場面の名前が一覧にありません");
            LogShishaShowcaseMarker(name.c_str(), "failure", "reason=unknown_scene");
            return GameModeEnterResult::Failed;
        }
        if (!std::isfinite(data.m_StandinScale) || data.m_StandinScale <= 0.0f ||
            !std::isfinite(data.m_OrbitDegreesPerSecond))
        {
            LOG_ERROR("シ者の倍率または周回速度が不正です");
            LogShishaShowcaseMarker(data.m_pScene->Name, "failure", "reason=invalid_parameters");
            return GameModeEnterResult::Failed;
        }
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);
        auto sceneContext = MakeSceneContext(ctx, data);
        if (!data.m_Camera.Enter(ctx) || !data.m_pScene->Build(sceneContext))
        {
            LOG_ERROR("シ者の場面を組み立てられませんでした");
            LogShishaShowcaseMarker(data.m_pScene->Name, "failure", "reason=assembly_failed");
            // Enter の失敗時は Leave が呼ばれないため、場面側の要求もここで取り消す。
            if (data.m_pScene->Leave != nullptr)
            {
                data.m_pScene->Leave(sceneContext);
            }
            data.m_SceneState.reset();
            data.m_StageState.reset();
            data.m_Camera.Leave();
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_Camera.Update(ctx, data.m_ElapsedSeconds, data.m_OrbitDegreesPerSecond);
        char values[128]{};
        std::snprintf(values, sizeof(values), "scale=%.6f orbit_deg_per_sec=%.6f",
                      static_cast<double>(data.m_StandinScale), static_cast<double>(data.m_OrbitDegreesPerSecond));
        LogShishaShowcaseMarker(data.m_pScene->Name, "enter", values);
        return GameModeEnterResult::Succeeded;
    }

    void ShishaShowcaseRoutine::Tick(GameModeContext& ctx, ShishaShowcaseData& data, float deltaTime)
    {
        if (data.m_pScene == nullptr)
        {
            return;
        }
        ++data.m_TickCount;
        data.m_ElapsedSeconds += static_cast<double>(deltaTime);
        data.m_Camera.Update(ctx, data.m_ElapsedSeconds, data.m_OrbitDegreesPerSecond);
        auto sceneContext = MakeSceneContext(ctx, data, deltaTime);
        const bool bAssembled = data.m_pScene->Update == nullptr || data.m_pScene->Update(sceneContext);
        // 取り付けの Tick に場面が視点を決めた場合も、同じ時刻の姿勢をこのフレームへ渡す。
        data.m_Camera.Update(ctx, data.m_ElapsedSeconds, data.m_OrbitDegreesPerSecond);
        if (!data.m_bReady)
        {
            Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, "shisha-showcase", bAssembled);
            if (bAssembled)
            {
                data.m_bReady = true;
                char values[640]{};
                std::snprintf(values, sizeof(values), "tick=%llu frame=%llu%s%s",
                              static_cast<unsigned long long>(data.m_TickCount),
                              static_cast<unsigned long long>(ctx.EngineRef.GetFrameCount()),
                              data.m_ReadyKeyValues[0] != '\0' ? " " : "", data.m_ReadyKeyValues);
                LogShishaShowcaseMarker(data.m_pScene->Name, "ready", values);
            }
        }
    }

    void ShishaShowcaseRoutine::Leave(GameModeContext& ctx, ShishaShowcaseData& data, GameModeExitReason)
    {
        auto sceneContext = MakeSceneContext(ctx, data);
        if (data.m_pScene != nullptr && data.m_pScene->Leave != nullptr)
        {
            data.m_pScene->Leave(sceneContext);
        }
        data.m_SceneState.reset();
        data.m_StageState.reset();
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        data.m_Camera.Leave();
        ResetEnvironment(ctx);
        if (data.m_pScene != nullptr)
        {
            LogShishaShowcaseMarker(data.m_pScene->Name, "leave", "");
        }
        data.m_pScene = nullptr;
    }
} // namespace Game::GameModes
