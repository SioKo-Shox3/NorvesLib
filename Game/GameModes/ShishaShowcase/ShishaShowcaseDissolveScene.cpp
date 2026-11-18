// 代役のディゾルブと消滅の粒を一度だけ進め、節目を記録する。
#include "GameModes/ShishaShowcase/ShishaShowcaseBaseScene.h"

#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Particle/VanishEffect.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseMarker.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseMode.h"

#include <cstdio>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Particle;
    using namespace NorvesLib::Core::Rendering;

    namespace
    {
        // 代役が描画の列に載り、表面の源が用意されるまで待つ。
        constexpr float kVanishStartDelaySeconds = 1.0f;
        constexpr uint32_t kVanishSeed = 0xC001D00Du;

        struct DissolveSceneState
        {
            VanishEffectDefinition m_Definition;
            VfxEffectAsset m_EffectAsset;
            MaterialHandle m_Material;
            Container::TUniquePtr<VanishEffect> m_VanishEffect;
            float m_StartWaitSeconds = 0.0f;
            bool m_bStandinMaterialApplied = false;
            bool m_bStartAttempted = false;
            bool m_bStartFailed = false;
            bool m_bThreshold50Logged = false;
            bool m_bEndLogged = false;
        };

        void LogVanishMarker(const ShishaShowcaseSceneContext& ctx, const char* stage,
                             const VanishEffect& effect)
        {
            char keyValues[128]{};
            std::snprintf(keyValues, sizeof(keyValues), "set=%s particles=%llu",
                          ctx.Data.m_VanishSet.c_str(),
                          static_cast<unsigned long long>(effect.GetRequestedParticleCount()));
            LogShishaShowcaseMarker("dissolve", stage, keyValues);
        }

        bool LogVanishLoadFailure(const ShishaShowcaseSceneContext& ctx, const Container::String& error)
        {
            LOG_ERROR("消滅の定義を読み込めませんでした（セット: %s）: %s",
                      ctx.Data.m_VanishSet.c_str(), error.c_str());
            char keyValues[128]{};
            std::snprintf(keyValues, sizeof(keyValues), "reason=vanish_load_failed set=%s",
                          ctx.Data.m_VanishSet.c_str());
            LogShishaShowcaseMarker("dissolve", "failure", keyValues);
            return false;
        }
    }

    bool BuildShishaShowcaseDissolveScene(ShishaShowcaseSceneContext& ctx)
    {
        if (!BuildShishaShowcaseStage(ctx))
        {
            return false;
        }
        Container::String path;
        if (ctx.Data.m_VanishSet == TEXT("ash"))
        {
            path = TEXT("Assets/Effects/ShishaVanish_Ash.vanish.json");
        }
        else if (ctx.Data.m_VanishSet == TEXT("light"))
        {
            path = TEXT("Assets/Effects/ShishaVanish_Light.vanish.json");
        }
        else
        {
            return LogVanishLoadFailure(ctx, TEXT("セットは ash または light で指定してください"));
        }

        auto state = Container::MakeShared<DissolveSceneState>();
        Container::String error;
        if (!LoadVanishEffectFile(path, state->m_Definition, state->m_EffectAsset, error))
        {
            return LogVanishLoadFailure(ctx, error);
        }

        MaterialCreateData material;
        material.DebugName = "ShishaShowcaseDissolve";
        material.Program = DissolveGBufferProgramName;
        material.Metallic = 0.0f;
        material.Roughness = 0.7f;
        material.bTwoSided = true;
        if (state->m_Definition.EdgeEmissiveNits >= 0.0f)
        {
            material.Dissolve.EdgeEmissiveNits = state->m_Definition.EdgeEmissiveNits;
        }
        state->m_Material = ctx.Mode.RenderResourcesRef.Materials().Create(material);
        if (!state->m_Material.IsValid())
        {
            LOG_ERROR("シ者のディゾルブの材質を作成できませんでした");
            return false;
        }
        ctx.Mode.ScopeRef.TrackMaterial(state->m_Material);
        state->m_VanishEffect = Container::MakeUnique<VanishEffect>(
            ctx.Mode.EngineRef.GetParticleSystem().GetVfxEffects());
        ctx.Data.m_SceneState = state;
        return true;
    }

    bool UpdateShishaShowcaseDissolveScene(ShishaShowcaseSceneContext& ctx)
    {
        if (!UpdateShishaShowcaseStage(ctx))
        {
            return false;
        }
        const auto state = Container::StaticPointerCast<DissolveSceneState>(ctx.Data.m_SceneState);
        auto* standin = GetShishaShowcaseStandin(ctx);
        if (!state || standin == nullptr)
        {
            return false;
        }
        if (!state->m_bStandinMaterialApplied)
        {
            standin->SetMaterial(state->m_Material);
            standin->SetDissolveThreshold(0.0f);
            state->m_bStandinMaterialApplied = true;
            std::snprintf(ctx.Data.m_ReadyKeyValues, sizeof(ctx.Data.m_ReadyKeyValues),
                          "skinned=1 set=%s vanish_duration=%.6f", ctx.Data.m_VanishSet.c_str(),
                          static_cast<double>(state->m_Definition.DurationSeconds));
            return true;
        }
        if (state->m_bStartFailed || state->m_bEndLogged)
        {
            return true;
        }

        const float worldDt = ctx.Mode.EngineRef.GetTimeSystem().GetFrameTimes().World;
        if (!state->m_bStartAttempted)
        {
            state->m_StartWaitSeconds += worldDt;
            if (state->m_StartWaitSeconds < kVanishStartDelaySeconds)
            {
                return true;
            }
            state->m_bStartAttempted = true;
            VanishEffectStartDesc desc;
            desc.Position = standin->GetOwner()->GetPosition();
            desc.DissolveTarget = standin;
            desc.Seed = kVanishSeed;
            Container::String error;
            if (!state->m_VanishEffect->Start(state->m_Definition, state->m_EffectAsset, desc, &error))
            {
                state->m_bStartFailed = true;
                LOG_ERROR("シ者の消滅の効果を開始できませんでした: %s", error.c_str());
                LogShishaShowcaseMarker("dissolve", "failure", "reason=vanish_start_failed");
                return true;
            }
            LogVanishMarker(ctx, "start", *state->m_VanishEffect);
            return true;
        }

        state->m_VanishEffect->Tick(worldDt);
        if (!state->m_bThreshold50Logged && state->m_VanishEffect->GetDissolveThreshold() >= 0.5f)
        {
            state->m_bThreshold50Logged = true;
            LogVanishMarker(ctx, "threshold50", *state->m_VanishEffect);
        }
        if (state->m_VanishEffect->IsFinished())
        {
            state->m_bEndLogged = true;
            LogVanishMarker(ctx, "end", *state->m_VanishEffect);
        }
        return true;
    }

    void LeaveShishaShowcaseDissolveScene(ShishaShowcaseSceneContext& ctx)
    {
        const auto state = Container::StaticPointerCast<DissolveSceneState>(ctx.Data.m_SceneState);
        if (state)
        {
            state->m_VanishEffect.reset();
        }
        LeaveShishaShowcaseStage(ctx);
        ctx.Data.m_SceneState.reset();
    }
} // namespace Game::GameModes
