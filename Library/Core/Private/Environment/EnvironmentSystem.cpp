// 気象を固定刻みで進め、上書きの層を解決して描画へ渡す。
#include "Environment/EnvironmentSystem.h"

#include "Logging/LogMacros.h"
#include "Rendering/RenderWorld.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        constexpr uint64_t NanosecondsPerSecond = 1'000'000'000;

        uint64_t StepsForNanoseconds(uint64_t nanoseconds)
        {
            return (nanoseconds / NanosecondsPerSecond) * EnvironmentSystem::StepsPerSecond +
                   ((nanoseconds % NanosecondsPerSecond) * EnvironmentSystem::StepsPerSecond) / NanosecondsPerSecond;
        }

        uint64_t NanosecondsForGameTime(double seconds)
        {
            const double nanoseconds = seconds * static_cast<double>(NanosecondsPerSecond);
            constexpr double SignedLimit = 9223372036854775808.0;
            constexpr double UnsignedLimit = 18446744073709551616.0;
            if (nanoseconds >= UnsignedLimit)
            {
                LOG_WARNING("EnvironmentSystem の経過時刻が保持できる範囲を超えたため上限に丸めます");
                return std::numeric_limits<uint64_t>::max();
            }
            if (nanoseconds >= SignedLimit)
            {
                return (uint64_t{1} << 63) + static_cast<uint64_t>(std::llround(nanoseconds - SignedLimit));
            }
            return static_cast<uint64_t>(std::llround(nanoseconds));
        }

        void WarnDisabledCommand(const char* command)
        {
            LOG_WARNING("EnvironmentSystem が無効なため %s を無視します", command);
            (void)command;
        }

        double FieldPeriod(EEnvironmentField field)
        {
            switch (field)
            {
            case EEnvironmentField::WindDirectionYaw:
            case EEnvironmentField::SunAzimuth:
                return 360.0;
            case EEnvironmentField::TimeOfDayHours:
                return 24.0;
            default:
                return 0.0;
            }
        }
    } // namespace

    void EnvironmentSystem::Enable(const EnvironmentState& initial, const EnvironmentSceneBase& base)
    {
        m_SceneBase = base;
        m_BaseState = SanitizeEnvironmentState(initial);
        ClearTransitions();
        m_Overrides.clear();
        m_TotalNanoseconds = NanosecondsForGameTime(m_BaseState.GameTimeSeconds);
        if (m_TotalNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
        }
        m_StepCount = StepsForNanoseconds(m_TotalNanoseconds);
        m_bEnabled = true;
        m_bRestorePending = false;
        RebuildEffective();
        Notify(EEnvironmentChange::Enabled);
    }

    void EnvironmentSystem::Disable()
    {
        if (!m_bEnabled)
        {
            return;
        }
        m_bEnabled = false;
        m_bRestorePending = true;
        ClearTransitions();
        m_Overrides.clear();
        RebuildEffective();
        Notify(EEnvironmentChange::Disabled);
    }

    bool EnvironmentSystem::IsEnabled() const
    {
        return m_bEnabled;
    }

    void EnvironmentSystem::SetTarget(const EnvironmentTarget& target)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SetTarget");
            return;
        }

        RefreshFields();
        const EnvironmentState values = SanitizeEnvironmentState(target.Values);
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            const auto field = static_cast<EEnvironmentField>(index);
            if ((target.Mask & EnvironmentFieldBit(field)) == 0)
            {
                continue;
            }
            if (field == EEnvironmentField::Wetness)
            {
                LOG_WARNING("EnvironmentSystem の濡れは SetTarget では遷移できないため無視します");
                continue;
            }

            FieldTransition& transition = m_Transitions[index];
            const float to = GetEnvironmentField(values, field);
            const float duration = target.DurationSeconds[index];
            if (std::isfinite(duration) && duration > 0.0f)
            {
                transition.bActive = true;
                transition.From = GetEnvironmentField(m_BaseState, field);
                transition.To = to;
                transition.StartStep = m_StepCount;
                transition.DurationSeconds = duration;
                transition.Curve = target.Curves[index];
                if (field == EEnvironmentField::CloudLayerEnabled)
                {
                    SetEnvironmentField(m_BaseState, field, EvaluateField(field, m_StepCount));
                }
            }
            else
            {
                SetEnvironmentField(m_BaseState, field, to);
                transition.bActive = false;
            }
        }
        RebuildEffective();
        Notify(EEnvironmentChange::TargetSet);
    }

    void EnvironmentSystem::SetImmediate(const EnvironmentTarget& target)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SetImmediate");
            return;
        }

        const EnvironmentState values = SanitizeEnvironmentState(target.Values);
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            const auto field = static_cast<EEnvironmentField>(index);
            if ((target.Mask & EnvironmentFieldBit(field)) != 0)
            {
                SetEnvironmentField(m_BaseState, field, GetEnvironmentField(values, field));
                m_Transitions[index].bActive = false;
            }
        }
        ++m_BaseState.DiscontinuityId;
        RebuildEffective();
        Notify(EEnvironmentChange::Immediate);
    }

    void EnvironmentSystem::SnapToTarget()
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SnapToTarget");
            return;
        }

        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            FieldTransition& transition = m_Transitions[index];
            if (transition.bActive)
            {
                SetEnvironmentField(m_BaseState, static_cast<EEnvironmentField>(index), transition.To);
                transition.bActive = false;
            }
        }
        ++m_BaseState.DiscontinuityId;
        RebuildEffective();
        Notify(EEnvironmentChange::Snapped);
    }

    void EnvironmentSystem::LoadState(const EnvironmentState& state)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("LoadState");
            return;
        }

        const uint32_t nextDiscontinuityId = m_BaseState.DiscontinuityId + 1u;
        m_BaseState = SanitizeEnvironmentState(state);
        m_BaseState.DiscontinuityId = nextDiscontinuityId;
        ClearTransitions();
        m_TotalNanoseconds = NanosecondsForGameTime(m_BaseState.GameTimeSeconds);
        if (m_TotalNanoseconds == std::numeric_limits<uint64_t>::max())
        {
            m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
        }
        m_StepCount = StepsForNanoseconds(m_TotalNanoseconds);
        RebuildEffective();
        Notify(EEnvironmentChange::Loaded);
    }

    void EnvironmentSystem::SkipTime(double seconds)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("SkipTime");
            return;
        }
        if (!std::isfinite(seconds) || seconds <= 0.0)
        {
            return;
        }

        const double delta = seconds < MaxSkipSeconds ? seconds : MaxSkipSeconds;
        AdvanceNanoseconds(static_cast<uint64_t>(std::llround(delta * static_cast<double>(NanosecondsPerSecond))));
        ++m_BaseState.DiscontinuityId;
        RebuildEffective();
        Notify(EEnvironmentChange::TimeSkipped);
    }

    void EnvironmentSystem::Tick(float worldDeltaSeconds)
    {
        if (!m_bEnabled || !std::isfinite(worldDeltaSeconds) || worldDeltaSeconds <= 0.0f)
        {
            return;
        }

        const double delta = static_cast<double>(worldDeltaSeconds) < MaxTickDeltaSeconds
            ? static_cast<double>(worldDeltaSeconds) : MaxTickDeltaSeconds;
        AdvanceNanoseconds(static_cast<uint64_t>(std::llround(delta * static_cast<double>(NanosecondsPerSecond))));
    }

    const EnvironmentState& EnvironmentSystem::GetState() const
    {
        return m_EffectiveState;
    }

    const EnvironmentState& EnvironmentSystem::GetBaseState() const
    {
        return m_BaseState;
    }

    const EnvironmentSceneBase& EnvironmentSystem::GetSceneBase() const
    {
        return m_SceneBase;
    }

    EnvironmentResolved EnvironmentSystem::Resolve() const
    {
        return ResolveEnvironment(m_EffectiveState, m_SceneBase);
    }

    bool EnvironmentSystem::PushOverride(EnvironmentOverrideScope scope, const EnvironmentTarget& target)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("PushOverride");
            return false;
        }

        EnvironmentTarget sanitizedTarget = target;
        sanitizedTarget.Mask &= EnvironmentFieldMaskAll;
        sanitizedTarget.Values = SanitizeEnvironmentState(target.Values);
        for (OverrideEntry& entry : m_Overrides)
        {
            if (entry.Scope == scope)
            {
                entry.Target = sanitizedTarget;
                RebuildEffective();
                Notify(EEnvironmentChange::OverridePushed);
                return true;
            }
        }
        m_Overrides.push_back(OverrideEntry{scope, sanitizedTarget});
        RebuildEffective();
        Notify(EEnvironmentChange::OverridePushed);
        return true;
    }

    bool EnvironmentSystem::PopOverride(EnvironmentOverrideScope scope)
    {
        if (!m_bEnabled)
        {
            WarnDisabledCommand("PopOverride");
            return false;
        }

        for (auto entry = m_Overrides.begin(); entry != m_Overrides.end(); ++entry)
        {
            if (entry->Scope == scope)
            {
                m_Overrides.erase(entry);
                RebuildEffective();
                Notify(EEnvironmentChange::OverridePopped);
                return true;
            }
        }
        return false;
    }

    void EnvironmentSystem::PublishTo(Rendering::RenderWorld& renderWorld)
    {
        if (m_bEnabled)
        {
            const EnvironmentResolved resolved = Resolve();
            renderWorld.SetSkyAtmosphere(resolved.Sky);
            renderWorld.SetVolumetricFogParameters(resolved.Fog);
            renderWorld.SetCloudLayer(resolved.CloudLayer);
            renderWorld.SetWindParameters(resolved.Wind);
            renderWorld.SetEnvironmentParameters(resolved.Environment);
        }
        else if (m_bRestorePending)
        {
            renderWorld.SetEnvironmentParameters(Rendering::MakeDefaultEnvironmentParameters());
            m_bRestorePending = false;
        }
    }

    MulticastDelegate<EEnvironmentChange, const EnvironmentState&>& EnvironmentSystem::OnChanged()
    {
        return m_OnChanged;
    }

    void EnvironmentSystem::ClearTransitions()
    {
        for (FieldTransition& transition : m_Transitions)
        {
            transition = {};
        }
    }

    float EnvironmentSystem::EvaluateField(EEnvironmentField field, uint64_t step) const
    {
        const FieldTransition& transition = m_Transitions[static_cast<uint32_t>(field)];
        if (!transition.bActive)
        {
            return GetEnvironmentField(m_BaseState, field);
        }

        const double progress = static_cast<double>(step - transition.StartStep) /
                                static_cast<double>(StepsPerSecond) / transition.DurationSeconds;
        if (progress >= 1.0)
        {
            return transition.To;
        }
        if (field == EEnvironmentField::CloudLayerEnabled)
        {
            return (transition.From >= 0.5f || transition.To >= 0.5f) ? 1.0f : 0.0f;
        }

        const double curve = EvaluateTransitionCurve(transition.Curve, static_cast<float>(progress));
        const double period = FieldPeriod(field);
        if (period > 0.0)
        {
            double difference = std::fmod(static_cast<double>(transition.To) - transition.From, period);
            if (difference > 0.5 * period)
            {
                difference -= period;
            }
            else if (difference < -0.5 * period)
            {
                difference += period;
            }
            double wrapped = std::fmod(static_cast<double>(transition.From) + difference * curve, period);
            if (wrapped < 0.0)
            {
                wrapped += period;
            }
            const float result = static_cast<float>(wrapped);
            return result >= period ? 0.0f : result;
        }
        return static_cast<float>(static_cast<double>(transition.From) +
                                  (static_cast<double>(transition.To) - transition.From) * curve);
    }

    void EnvironmentSystem::RefreshFields()
    {
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            FieldTransition& transition = m_Transitions[index];
            const auto field = static_cast<EEnvironmentField>(index);
            if (!transition.bActive || field == EEnvironmentField::Wetness)
            {
                continue;
            }
            const double elapsed = static_cast<double>(m_StepCount - transition.StartStep) /
                                   static_cast<double>(StepsPerSecond);
            if (elapsed >= transition.DurationSeconds)
            {
                SetEnvironmentField(m_BaseState, field, transition.To);
                transition.bActive = false;
            }
            else
            {
                SetEnvironmentField(m_BaseState, field, EvaluateField(field, m_StepCount));
            }
        }
    }

    void EnvironmentSystem::RebuildEffective()
    {
        m_EffectiveState = m_BaseState;
        for (const OverrideEntry& entry : m_Overrides)
        {
            for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
            {
                const auto field = static_cast<EEnvironmentField>(index);
                if ((entry.Target.Mask & EnvironmentFieldBit(field)) != 0)
                {
                    SetEnvironmentField(m_EffectiveState, field, GetEnvironmentField(entry.Target.Values, field));
                }
            }
        }
    }

    void EnvironmentSystem::StepOnce()
    {
        float rain = EvaluateField(EEnvironmentField::RainIntensity, m_StepCount);
        for (const OverrideEntry& entry : m_Overrides)
        {
            if ((entry.Target.Mask & EnvironmentFieldBit(EEnvironmentField::RainIntensity)) != 0)
            {
                rain = entry.Target.Values.RainIntensity;
            }
        }
        m_BaseState.Wetness = AdvanceEnvironmentWetness(m_BaseState.Wetness, rain,
                                                        1.0 / static_cast<double>(StepsPerSecond),
                                                        m_BaseState.WetTimeConstantSeconds,
                                                        m_BaseState.DryTimeConstantSeconds);
        ++m_StepCount;
    }

    void EnvironmentSystem::AdvanceNanoseconds(uint64_t nanoseconds)
    {
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        m_TotalNanoseconds = nanoseconds > maximum - m_TotalNanoseconds
            ? maximum : m_TotalNanoseconds + nanoseconds;
        const uint64_t target = StepsForNanoseconds(m_TotalNanoseconds);
        while (m_StepCount < target)
        {
            StepOnce();
        }
        RefreshFields();
        m_BaseState.GameTimeSeconds = static_cast<double>(m_TotalNanoseconds) * 1e-9;
        RebuildEffective();
    }

    void EnvironmentSystem::Notify(EEnvironmentChange change)
    {
        m_OnChanged.Broadcast(change, m_EffectiveState);
    }
} // namespace NorvesLib::Core::Environment
