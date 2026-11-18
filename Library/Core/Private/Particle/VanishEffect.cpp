/**
 * @file VanishEffect.cpp
 * @brief World の時間に沿って消滅の閾値と粒の放出を進める。
 */

#include "Particle/VanishEffect.h"

#include "Component/SkinnedMeshComponent.h"
#include "Logging/LogMacros.h"
#include "Particle/VfxSchema.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Particle
{
    VanishEffect::VanishEffect(VfxEffectSystem& vfx)
        : m_Vfx(vfx)
    {
    }

    VanishEffect::~VanishEffect()
    {
        Stop();
    }

    bool VanishEffect::Start(const VanishEffectDefinition& definition, const VfxEffectAsset& effect,
        const VanishEffectStartDesc& desc, Container::String* outError)
    {
        Stop();
        if (outError != nullptr)
        {
            outError->clear();
        }
        if (!std::isfinite(definition.DurationSeconds) || definition.DurationSeconds <= 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "消滅の長さには正の有限値が必要です。";
            }
            return false;
        }
        for (const VanishEmitterTrack& track : definition.Emitters)
        {
            if (track.EmitterIndex >= effect.Emitters.size())
            {
                if (outError != nullptr)
                {
                    *outError = "消滅の割り付けのエミッタが解決されていません。";
                }
                return false;
            }
        }
        for (const VanishBurst& burst : definition.Bursts)
        {
            if (burst.EmitterIndex >= effect.Emitters.size())
            {
                if (outError != nullptr)
                {
                    *outError = "消滅の単発放出のエミッタが解決されていません。";
                }
                return false;
            }
        }

        VfxInstanceDesc instanceDesc;
        instanceDesc.Position = desc.Position;
        instanceDesc.Seed = desc.Seed;
        instanceDesc.SkinComponentId = desc.SkinComponentId != 0u ? desc.SkinComponentId :
            (desc.DissolveTarget != nullptr ? desc.DissolveTarget->GetComponentId() : 0u);
        if (desc.SkinJoints != nullptr)
        {
            instanceDesc.SkinJoints = *desc.SkinJoints;
        }
        instanceDesc.SkinJointCount = desc.SkinJointCount;
        m_Instance = m_Vfx.CreateInstance(effect, instanceDesc, outError);
        if (!m_Vfx.IsLiveInstance(m_Instance))
        {
            m_Instance = {};
            return false;
        }

        // VFX が保持する既定の生成率も、表示用の期待値に含める。
        const VfxObjectSchema* schema = FindVfxModuleSchema(VfxModuleType::SpawnRate);
        const int32_t rateIndex = schema != nullptr ? FindVfxParameterIndex(*schema, "Rate") : -1;
        if (rateIndex < 0)
        {
            if (outError != nullptr)
            {
                *outError = "VFX の生成率の定義を取得できません。";
            }
            Stop();
            return false;
        }
        m_Definition = definition;
        m_DissolveTarget = desc.DissolveTarget;
        m_Position = desc.Position;
        m_Emitters.resize(effect.Emitters.size());
        for (size_t index = 0u; index < effect.Emitters.size(); ++index)
        {
            float defaultRate = 0.0f;
            for (const VfxModule& module : effect.Emitters[index].Modules)
            {
                if (module.Type == VfxModuleType::SpawnRate)
                {
                    defaultRate += module.Values[static_cast<size_t>(rateIndex)].Vector.x;
                }
            }
            m_Emitters[index].DefaultRate = defaultRate;
            m_Emitters[index].Rate = defaultRate;
        }

        SetDissolveThreshold(EvaluateVanishThreshold(m_Definition, 0.0));
        for (const VanishEmitterTrack& track : m_Definition.Emitters)
        {
            bool bEnabled = false;
            float rate = 0.0f;
            EvaluateVanishEmitter(track, 0.0, bEnabled, rate);
            if (!SetEmitterEnabled(track.EmitterIndex, bEnabled, rate))
            {
                if (outError != nullptr)
                {
                    *outError = "消滅のエミッタの初期状態を設定できません。";
                }
                Stop();
                return false;
            }
        }
        while (m_NextBurstIndex < m_Definition.Bursts.size() &&
            m_Definition.Bursts[m_NextBurstIndex].TimeSeconds == 0.0f)
        {
            const VanishBurst& burst = m_Definition.Bursts[m_NextBurstIndex++];
            EmitBurst(burst.EmitterIndex, burst.Count, burst.Velocity);
        }
        return true;
    }

    void VanishEffect::Tick(float worldDeltaSeconds)
    {
        if (!IsRunning() || m_bFinished || !std::isfinite(worldDeltaSeconds) || worldDeltaSeconds <= 0.0f)
        {
            return;
        }
        const double previousTime = m_TimeSeconds;
        m_TimeSeconds = std::min(previousTime + static_cast<double>(worldDeltaSeconds),
            static_cast<double>(m_Definition.DurationSeconds));
        const double elapsed = m_TimeSeconds - previousTime;
        for (EmitterState& emitter : m_Emitters)
        {
            emitter.PreviousRate = emitter.bEnabled ? emitter.Rate : 0.0;
        }

        SetDissolveThreshold(EvaluateVanishThreshold(m_Definition, m_TimeSeconds));
        for (const VanishEmitterTrack& track : m_Definition.Emitters)
        {
            bool bEnabled = false;
            float rate = 0.0f;
            EvaluateVanishEmitter(track, m_TimeSeconds, bEnabled, rate);
            SetEmitterEnabled(track.EmitterIndex, bEnabled, rate);
        }
        for (const EmitterState& emitter : m_Emitters)
        {
            const double rate = emitter.bEnabled ? emitter.Rate : 0.0;
            m_RequestedContinuousParticles += (emitter.PreviousRate + rate) * 0.5 * elapsed;
        }
        while (m_NextBurstIndex < m_Definition.Bursts.size() &&
            static_cast<double>(m_Definition.Bursts[m_NextBurstIndex].TimeSeconds) <= m_TimeSeconds)
        {
            const VanishBurst& burst = m_Definition.Bursts[m_NextBurstIndex++];
            if (static_cast<double>(burst.TimeSeconds) > previousTime)
            {
                EmitBurst(burst.EmitterIndex, burst.Count, burst.Velocity);
            }
        }
        if (m_TimeSeconds >= static_cast<double>(m_Definition.DurationSeconds))
        {
            m_bFinished = true;
            for (const VanishEmitterTrack& track : m_Definition.Emitters)
            {
                SetEmitterEnabled(track.EmitterIndex, false, 0.0f);
            }
        }
    }

    void VanishEffect::Stop()
    {
        if (m_Vfx.IsLiveInstance(m_Instance))
        {
            m_Vfx.DestroyInstance(m_Instance);
        }
        m_Instance = {};
        m_Definition = {};
        m_Emitters.clear();
        m_DissolveTarget = nullptr;
        m_Position = Math::Vector3();
        m_TimeSeconds = 0.0;
        m_RequestedContinuousParticles = 0.0;
        m_RequestedBurstParticles = 0u;
        m_NextBurstIndex = 0u;
        m_BurstCount = 0u;
        m_DissolveThreshold = 0.0f;
        m_bFinished = false;
    }

    void VanishEffect::SetDissolveThreshold(float threshold)
    {
        // NaN も範囲内の値として扱えるよう 0 にする。
        m_DissolveThreshold = !(threshold >= 0.0f) ? 0.0f : std::min(threshold, 1.0f);
        if (m_DissolveTarget != nullptr)
        {
            m_DissolveTarget->SetDissolveThreshold(m_DissolveThreshold);
        }
    }

    bool VanishEffect::SetEmitterEnabled(uint32_t emitterIndex, bool bEnabled, float spawnRatePerSecond)
    {
        if (!IsRunning() || emitterIndex >= m_Emitters.size() || !std::isfinite(spawnRatePerSecond))
        {
            return false;
        }
        if (!m_Vfx.SetEmitterEnabled(m_Instance, emitterIndex, bEnabled) ||
            !m_Vfx.SetEmitterSpawnRate(m_Instance, emitterIndex, spawnRatePerSecond))
        {
            return false;
        }
        EmitterState& emitter = m_Emitters[emitterIndex];
        emitter.bEnabled = bEnabled;
        emitter.Rate = spawnRatePerSecond < 0.0f ? emitter.DefaultRate : static_cast<double>(spawnRatePerSecond);
        return true;
    }

    bool VanishEffect::EmitBurst(uint32_t emitterIndex, uint32_t count, const Math::Vector3& velocity)
    {
        if (!IsRunning())
        {
            return false;
        }
        const bool bRaised = m_Vfx.RaiseGpuBurst(m_Instance, emitterIndex, m_Position,
            Math::Vector3(0.0f, 1.0f, 0.0f), velocity, count);
        if (!bRaised && !m_bWarnedBurst)
        {
            LOG_WARNING("消滅の単発放出を受け付けられませんでした");
            m_bWarnedBurst = true;
        }
        if (m_BurstCount != std::numeric_limits<uint32_t>::max())
        {
            ++m_BurstCount;
        }
        const uint64_t maxCount = std::numeric_limits<uint64_t>::max();
        m_RequestedBurstParticles = count > maxCount - m_RequestedBurstParticles ?
            maxCount : m_RequestedBurstParticles + count;
        return bRaised;
    }

    bool VanishEffect::IsRunning() const
    {
        return m_Vfx.IsLiveInstance(m_Instance);
    }

    bool VanishEffect::IsFinished() const
    {
        return m_bFinished;
    }

    double VanishEffect::GetTimeSeconds() const
    {
        return m_TimeSeconds;
    }

    float VanishEffect::GetDissolveThreshold() const
    {
        return m_DissolveThreshold;
    }

    float VanishEffect::GetEdgeEmissiveNits() const
    {
        return m_Definition.EdgeEmissiveNits;
    }

    uint32_t VanishEffect::GetBurstCount() const
    {
        return m_BurstCount;
    }

    uint64_t VanishEffect::GetRequestedParticleCount() const
    {
        const uint64_t maxCount = std::numeric_limits<uint64_t>::max();
        if (m_RequestedContinuousParticles >= static_cast<double>(maxCount))
        {
            return maxCount;
        }
        const uint64_t continuous = static_cast<uint64_t>(m_RequestedContinuousParticles);
        return continuous > maxCount - m_RequestedBurstParticles ?
            maxCount : m_RequestedBurstParticles + continuous;
    }

    VfxEffectHandle VanishEffect::GetInstance() const
    {
        return m_Instance;
    }
} // namespace NorvesLib::Core::Particle
