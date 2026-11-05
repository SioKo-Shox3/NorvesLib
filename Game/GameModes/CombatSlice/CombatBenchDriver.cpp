#include "CombatBenchDriver.h"

#include "Debug/Stats.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"

#if NORVES_ENABLE_STATS
#include "Ecs/EcsScheduler.h"
#include "GameMode/GameModeScope.h"
#include "Gameplay/Combat/CombatData.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/IGameplayModule.h"

#include <algorithm>
#include <cmath>
#endif

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;

#if NORVES_ENABLE_STATS
    namespace C = NorvesLib::Modules::Gameplay::Combat;
    namespace G = NorvesLib::Modules::Gameplay;

    namespace
    {
        constexpr uint32_t HitboxCount = 4;
        constexpr uint32_t VictimCount = 16;
        constexpr uint32_t SampleCount = 120;
        constexpr uint32_t WarmupFrames = 8;
        constexpr uint32_t TimeoutFrames = 600;
        constexpr const char* ScopeName = "Ecs.Sys.PostLateTick.ModuleLateTick";
        constexpr const char* ReaderName = "CombatBench.ReadStats";

        float Median(Container::VariableArray<float>& samples)
        {
            std::sort(samples.begin(), samples.end());
            return (samples[SampleCount / 2 - 1] + samples[SampleCount / 2]) * .5f;
        }
    } // namespace
#endif

    struct CombatBenchDriver::State
    {
#if NORVES_ENABLE_STATS
        G::IGameplayModule* Module = nullptr;
        C::CombatResponseSettings SavedResponse;
        bool bResponseSaved = false;
        bool bStartedTrace = false;
        Ecs::EcsOrderedHandle Reader;
        Entity* Attackers[HitboxCount] = {};
        C::HitboxComponent* Hitboxes[HitboxCount] = {};
        uint64_t Victims[VictimCount] = {};
        Container::VariableArray<float> ActiveSamples;
        Container::VariableArray<float> IdleSamples;
        uint32_t WarmupRemaining = WarmupFrames;
        uint32_t Ticks = 0;
        uint64_t Hits = 0;
        uint64_t ExpectedFrame = 0;
        float PendingUs = 0;
        bool bActive = true;
        bool bMoveRight = true;
        bool bArmed = false;
        bool bPending = false;
        bool bStatsFound = false;

        static void Capture(void* user, const Ecs::EcsSystemContext&)
        {
            auto& state = *static_cast<State*>(user);
            if (!state.bArmed)
            {
                return;
            }
            state.bArmed = false;
            state.bPending = true;
            state.bStatsFound = false;
            // BeginFrame は記録を消すため、ModuleLateTick 後の RenderSync で退避する。
            // この値だけを次のモード Tick が前フレームの試料として消費する。
            const auto profile = NorvesLib::Debug::StatsManager::Get().GetFrameProfileSnapshot();
            if (profile.FrameNumber != state.ExpectedFrame)
            {
                return;
            }
            for (const auto& event : profile.Events)
            {
                if (event.Name == ScopeName && event.Category == "Ecs" &&
                    std::isfinite(event.DurationMs) && event.DurationMs >= 0)
                {
                    state.PendingUs = event.DurationMs * 1000.0f;
                    state.bStatsFound = true;
                    break;
                }
            }
        }

        void OnHit(const C::CombatHitEvent& event)
        {
            if (!bActive || WarmupRemaining != 0)
            {
                return;
            }
            for (auto* attacker : Attackers)
            {
                if (attacker && event.AttackerActorId == attacker->GetObjectId())
                {
                    for (const auto victim : Victims)
                    {
                        if (event.VictimActorId == victim)
                        {
                            ++Hits;
                            return;
                        }
                    }
                }
            }
        }
#endif
    };

    CombatBenchDriver::CombatBenchDriver() = default;
    CombatBenchDriver::~CombatBenchDriver() = default;

    bool CombatBenchDriver::Begin(GameModeContext& ctx, const CombatSliceSceneIds& ids,
                                  const CombatSliceOptions& options)
    {
        (void)ids;
        (void)options;
        End(ctx);
        m_bFinished = false;
        m_ExitCode = 0;
#if NORVES_ENABLE_STATS
        m_State = Container::MakeUnique<State>();
        auto& state = *m_State;
        state.Module = G::FindGameplayModule(Module::GetModuleRegistry());
        if (!state.Module)
        {
            Fail(ctx, "bench_no_gameplay");
            return false;
        }
        state.SavedResponse = state.Module->GetCombatResponseSettings();
        state.bResponseSaved = true;
        auto response = state.SavedResponse;
        response.bHitEffects = false;
        response.bHitStop = false;
        response.bDebugDraw = false;
        state.Module->SetCombatResponseSettings(response);

        C::CombatFactionTable factions;
        Container::String error;
        uint32_t enemyBit = 0;
        if (!C::LoadCombatFactionsFromAsset(CombatSliceLayout::FactionsPath, factions, error) ||
            !factions.FindBit("enemy", enemyBit))
        {
            NORVES_LOG_ERROR("CombatSlice", "計測用の陣営を読み込めません: %s", error.c_str());
            Fail(ctx, "bench_factions");
            return false;
        }

        for (uint32_t index = 0; index < VictimCount; ++index)
        {
            auto* owner = ctx.ScopeRef.SpawnObject<Entity>();
            auto* hurtboxes = owner ? ctx.WorldRef.CreateComponent<C::HurtboxSetComponent>(owner) : nullptr;
            if (!hurtboxes)
            {
                Fail(ctx, "bench_setup");
                return false;
            }
            owner->SetPosition({30.0f + (index % 4) * .25f, .8f, (index / 4) * .25f});
            state.Victims[index] = owner->GetObjectId();
            for (uint32_t partIndex = 0; partIndex < 4; ++partIndex)
            {
                C::HurtboxPart part;
                part.PartId = partIndex;
                part.Faction = enemyBit;
                part.Shape.Type = C::ECombatShapeType::Sphere;
                part.Shape.Center = {0, (static_cast<float>(partIndex) - 1.5f) * .04f, 0};
                part.Shape.Radius = .1f;
                if (hurtboxes->AddPart(part) == C::HurtboxSetComponent::InvalidPartIndex)
                {
                    Fail(ctx, "bench_setup");
                    return false;
                }
            }
        }

        auto profile = Container::MakeShared<C::AttackProfile>();
        profile->ProfileId = Identity("combat_slice.bench");
        profile->HitMask = enemyBit;
        profile->RehitPolicy = C::ERehitPolicy::Interval;
        // 同じ World 時刻でも毎回の評価で全ての部位を報告する。
        profile->RehitIntervalSeconds = 0;
        profile->PerVictimPolicy = C::EPerVictimPolicy::AllParts;
        profile->MaxDurationSeconds = 3600;
        for (uint32_t index = 0; index < HitboxCount; ++index)
        {
            auto* owner = ctx.ScopeRef.SpawnObject<Entity>();
            auto* hitbox = owner ? ctx.WorldRef.CreateComponent<C::HitboxComponent>(owner) : nullptr;
            state.Attackers[index] = owner;
            state.Hitboxes[index] = hitbox;
            if (!hitbox)
            {
                Fail(ctx, "bench_setup");
                return false;
            }
            owner->SetPosition({29.65f, .8f, .375f});
            C::CombatShape shape;
            shape.Type = C::ECombatShapeType::Capsule;
            shape.PointA = {0, 0, -.75f};
            shape.PointB = {0, 0, .75f};
            shape.Radius = .12f;
            hitbox->SetActorId(owner->GetObjectId());
            hitbox->SetProfile(profile);
            if (!hitbox->SetShape(shape) || !hitbox->BeginAttack())
            {
                Fail(ctx, "bench_setup");
                return false;
            }
        }
        ctx.WorldRef.UpdateWorldTransforms();
        state.ActiveSamples.reserve(SampleCount);
        state.IdleSamples.reserve(SampleCount);
        state.Module->GetOnAnyHit().Add(&state, &State::OnHit);

        // ScopedStat の保存はトレース有効時だけ。既存のトレースは終了させない。
        auto& stats = NorvesLib::Debug::StatsManager::Get();
        if (!stats.IsTraceActive())
        {
            state.bStartedTrace = stats.StartTrace(".harness/combat-bench.trace.csv");
            if (!state.bStartedTrace)
            {
                Fail(ctx, "bench_no_stats");
                return false;
            }
        }
        auto& scheduler = ctx.WorldRef.GetScheduler();
        auto readerSystem = scheduler.FindSystem(ReaderName);
        if (readerSystem == Ecs::kInvalidEcsSystem)
        {
            Ecs::OrderedSystemDesc desc;
            desc.Name = ReaderName;
            desc.Stage = scheduler.FindStage("RenderSync");
            readerSystem = scheduler.RegisterOrderedSystem(desc);
        }
        Ecs::OrderedEntryDesc entry;
        entry.Run = &State::Capture;
        entry.User = &state;
        state.Reader = scheduler.AddOrderedEntry(readerSystem, entry);
        if (!state.Reader.IsValid())
        {
            Fail(ctx, "bench_no_stats");
            return false;
        }
#else
        NORVES_LOG_WARNING("CombatSlice", "統計が無効の構成のため、戦闘判定の計測を省略します。");
        m_bFinished = true;
        NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=complete exit_code=0");
        ctx.EngineRef.RequestExit(0);
#endif
        return true;
    }

    void CombatBenchDriver::Tick(GameModeContext& ctx, float deltaTime)
    {
        (void)deltaTime;
#if NORVES_ENABLE_STATS
        if (m_bFinished || !m_State)
        {
            return;
        }
        auto& state = *m_State;
        if (++state.Ticks > TimeoutFrames)
        {
            Fail(ctx, "bench_no_stats");
            return;
        }
        if (state.bPending)
        {
            state.bPending = false;
            if (state.WarmupRemaining != 0)
            {
                --state.WarmupRemaining;
            }
            else
            {
                if (!state.bStatsFound)
                {
                    Fail(ctx, "bench_no_stats");
                    return;
                }
                auto& samples = state.bActive ? state.ActiveSamples : state.IdleSamples;
                samples.push_back(state.PendingUs);
                if (samples.size() == SampleCount)
                {
                    if (state.bActive)
                    {
                        if (state.Hits == 0)
                        {
                            Fail(ctx, "bench_no_hits");
                            return;
                        }
                        for (auto* hitbox : state.Hitboxes)
                        {
                            hitbox->EndAttack();
                        }
                        state.bActive = false;
                        state.WarmupRemaining = WarmupFrames;
                    }
                    else
                    {
                        const float activeMedian = Median(state.ActiveSamples);
                        const float idleMedian = Median(state.IdleSamples);
                        NORVES_LOG_INFO("CombatSlice", "アイドル区間の最大時間: %.3f us", state.IdleSamples.back());
                        NORVES_LOG_INFO("CombatSlice", "COMBAT_BENCH hitboxes=4 hurtboxes=64 nmax=16 frames=%u active_median_us=%.3f active_max_us=%.3f idle_median_us=%.3f sweep_median_us=%.3f",
                                        SampleCount, activeMedian, state.ActiveSamples.back(), idleMedian,
                                        activeMedian - idleMedian);
                        m_bFinished = true;
                        End(ctx);
                        NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=complete exit_code=0");
                        ctx.EngineRef.RequestExit(0);
                        return;
                    }
                }
            }
        }
        if (state.bActive)
        {
            // 各 capsule は格子の全列を覆い、左右の外側を毎フレーム往復する。
            const float x = state.bMoveRight ? 31.1f : 29.65f;
            for (auto* attacker : state.Attackers)
            {
                attacker->SetPosition({x, .8f, .375f});
            }
            state.bMoveRight = !state.bMoveRight;
        }
        state.ExpectedFrame = ctx.EngineRef.GetFrameCount();
        state.bArmed = true;
#else
        (void)ctx;
#endif
    }

    bool CombatBenchDriver::IsFinished() const
    {
        return m_bFinished;
    }

    int32_t CombatBenchDriver::GetExitCode() const
    {
        return m_ExitCode;
    }

    void CombatBenchDriver::Fail(GameModeContext& ctx, const char* reason)
    {
        m_bFinished = true;
        m_ExitCode = 1;
        End(ctx);
        NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=failure reason=%s", reason);
        ctx.EngineRef.RequestExit(1);
    }

    void CombatBenchDriver::End(GameModeContext& ctx)
    {
#if NORVES_ENABLE_STATS
        if (!m_State)
        {
            return;
        }
        auto& state = *m_State;
        if (state.Reader.IsValid())
        {
            ctx.WorldRef.GetScheduler().RemoveOrderedEntry(state.Reader);
        }
        for (auto* hitbox : state.Hitboxes)
        {
            if (hitbox)
            {
                hitbox->EndAttack();
            }
        }
        if (state.Module)
        {
            state.Module->GetOnAnyHit().Remove(&state, &State::OnHit);
            if (state.bResponseSaved)
            {
                state.Module->SetCombatResponseSettings(state.SavedResponse);
            }
        }
        if (state.bStartedTrace)
        {
            NorvesLib::Debug::StatsManager::Get().StopTrace();
        }
#else
        (void)ctx;
#endif
        m_State.reset();
    }
} // namespace Game::GameModes
