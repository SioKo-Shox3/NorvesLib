// バス構成と現在の音量を保持し、活動による減衰とバックエンドへの反映を行う。
#include "Audio/AudioMixerDomain.h"

#include "Audio/IAudioService.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::Audio
{
    namespace
    {
        constexpr const char* BusNames[] = {"master", "se", "ambient", "music", "ui"};
        static_assert(sizeof(BusNames) / sizeof(BusNames[0]) == kAudioBusCount);
        static_assert(AudioBusId::Master == static_cast<AudioBusId>(0));

        constexpr uint32_t BusIndex(AudioBusId bus) noexcept
        {
            return static_cast<uint32_t>(bus);
        }

        constexpr bool IsValidBus(AudioBusId bus) noexcept
        {
            return BusIndex(bus) < kAudioBusCount;
        }

        float DbToGain(float db)
        {
            return db == 0.0f ? 1.0f : std::pow(10.0f, db / 20.0f);
        }
    }

    const char* GetAudioBusName(AudioBusId bus) noexcept
    {
        return IsValidBus(bus) ? BusNames[BusIndex(bus)] : "";
    }

    bool TryParseAudioBusName(Core::Container::AnsiStringView name, AudioBusId& outBus) noexcept
    {
        for (uint32_t index = 0; index < kAudioBusCount; ++index)
        {
            const Core::Container::AnsiStringView candidate(BusNames[index]);
            if (name.size() == candidate.size() && name == candidate)
            {
                outBus = static_cast<AudioBusId>(index);
                return true;
            }
        }
        return false;
    }

    float MapUserVolumeToGain(float slider01) noexcept
    {
        const float slider = std::isfinite(slider01) ? std::clamp(slider01, 0.0f, 1.0f) : 0.0f;
        return slider * slider;
    }

    AudioMixerConfig MakeDefaultAudioMixerConfig()
    {
        AudioMixerConfig config;
        config.Buses[BusIndex(AudioBusId::Master)].bPauseAffected = false;
        config.Buses[BusIndex(AudioBusId::Ui)].bPauseAffected = false;
        return config;
    }
}

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        bool IsValidRule(const AudioDuckRule& rule)
        {
            if (!IsValidBus(rule.Target) || !IsValidBus(rule.TriggerBus) ||
                !std::isfinite(rule.DuckDb) || rule.DuckDb > 0.0f)
            {
                return false;
            }
            return rule.TriggerKind == AudioDuckTriggerKind::Bus ||
                   (rule.TriggerKind == AudioDuckTriggerKind::DuckTag && rule.TriggerTag.IsValid());
        }

        bool IsTriggerActive(const AudioDuckRule& rule, const AudioMixerActivity& activity)
        {
            if (rule.TriggerKind == AudioDuckTriggerKind::Bus)
            {
                return activity.ActiveInstancesPerBus[BusIndex(rule.TriggerBus)] > 0;
            }

            const auto& tags = activity.ActiveDuckTags;
            return std::find(tags.begin(), tags.end(), rule.TriggerTag) != tags.end();
        }
    }

    AudioMixerDomain::AudioMixerDomain()
    {
        const auto defaults = MakeDefaultAudioMixerConfig();
        for (uint32_t index = 0; index < kAudioBusCount; ++index)
        {
            m_Buses[index].Parent = defaults.Buses[index].Parent;
        }
        ApplyConfig(defaults);
    }

    void AudioMixerDomain::BindService(IAudioService* service)
    {
        m_Service = service;
        for (auto& bus : m_Buses)
        {
            bus.LastSentGain = -1.0f;
        }
    }

    void AudioMixerDomain::ApplyConfig(const AudioMixerConfig& config)
    {
        float baseGains[kAudioBusCount];
        for (uint32_t index = 0; index < kAudioBusCount; ++index)
        {
            const float db = config.Buses[index].VolumeDb;
            if (!std::isfinite(db))
            {
                return;
            }
            baseGains[index] = DbToGain(db);
            if (!std::isfinite(baseGains[index]))
            {
                return;
            }
        }
        if (!ReplaceDuckRules(config.DuckRules))
        {
            return;
        }

        // 作成済みのサブミックスは送り先を張り替えられないため、親は構築時の既定を保つ。
        for (uint32_t index = 0; index < kAudioBusCount; ++index)
        {
            m_Buses[index].BaseGain = baseGains[index];
            m_Buses[index].bPauseAffected = config.Buses[index].bPauseAffected;
        }
        SendChangedVolumes();
    }

    bool AudioMixerDomain::ReplaceDuckRules(Core::Container::Span<const AudioDuckRule> rules)
    {
        for (const auto& rule : rules)
        {
            if (!IsValidRule(rule))
            {
                return false;
            }
        }

        m_DuckStates.reserve(rules.size());
        m_DuckStates.clear();
        for (const auto& rule : rules)
        {
            m_DuckStates.push_back({rule});
        }
        for (auto& bus : m_Buses)
        {
            bus.DuckGain = 1.0f;
        }
        return true;
    }

    AudioResult AudioMixerDomain::SetDuckRules(Core::Container::Span<const AudioDuckRule> rules)
    {
        return ReplaceDuckRules(rules) ? SendChangedVolumes() : AudioResult::InvalidState;
    }

    void AudioMixerDomain::Update(float deltaSeconds, const AudioMixerActivity& activity)
    {
        const double dtMs = std::isfinite(deltaSeconds) && deltaSeconds > 0.0f ? deltaSeconds * 1000.0 : 0.0;
        float deepestDb[kAudioBusCount] = {};
        for (auto& state : m_DuckStates)
        {
            const auto& rule = state.Rule;
            double targetDb = 0.0;
            if (IsTriggerActive(rule, activity))
            {
                state.HoldRemainingMs = rule.HoldMs;
                targetDb = rule.DuckDb;
            }
            else if (state.HoldRemainingMs > 0.0)
            {
                state.HoldRemainingMs = (std::max)(0.0, state.HoldRemainingMs - dtMs);
                targetDb = rule.DuckDb;
            }

            const double tauMs = (targetDb < state.CurrentDb ? rule.AttackMs : rule.ReleaseMs) / 5.0;
            // expm1 を使い、フレーム時間が時定数より短くても補間係数の桁を保つ。
            if (tauMs == 0.0)
            {
                state.CurrentDb = targetDb;
            }
            else
            {
                state.CurrentDb += (targetDb - state.CurrentDb) * -std::expm1(-dtMs / tauMs);
            }
            if (std::abs(state.CurrentDb) < 0.001)
            {
                state.CurrentDb = 0.0;
            }

            float& db = deepestDb[BusIndex(rule.Target)];
            db = (std::min)(db, static_cast<float>(state.CurrentDb));
        }
        for (uint32_t index = 0; index < kAudioBusCount; ++index)
        {
            m_Buses[index].DuckGain = DbToGain(deepestDb[index]);
        }
        SendChangedVolumes();
    }

    void AudioMixerDomain::ResetActivity()
    {
        for (auto& state : m_DuckStates)
        {
            state.HoldRemainingMs = 0.0;
        }
        // リリース時間が 0 の規則だけは、次のフレームを待たずに音量を戻す。
        Update(0.0f, {});
    }

    void AudioMixerDomain::Shutdown()
    {
        BindService(nullptr);
    }

    AudioResult AudioMixerDomain::SetUserBusVolume(AudioBusId bus, float slider01)
    {
        if (!IsValidBus(bus) || !std::isfinite(slider01))
        {
            return AudioResult::InvalidState;
        }
        m_Buses[BusIndex(bus)].UserVolume = std::clamp(slider01, 0.0f, 1.0f);
        return SendChangedVolumes();
    }

    float AudioMixerDomain::GetUserBusVolume(AudioBusId bus) const
    {
        return IsValidBus(bus) ? m_Buses[BusIndex(bus)].UserVolume : 0.0f;
    }

    AudioResult AudioMixerDomain::SetPauseGain(float gain)
    {
        if (!std::isfinite(gain))
        {
            return AudioResult::InvalidState;
        }
        m_PauseGain = std::clamp(gain, 0.0f, 1.0f);
        return SendChangedVolumes();
    }

    float AudioMixerDomain::GetPauseGain() const
    {
        return m_PauseGain;
    }

    float AudioMixerDomain::GetBusDuckGain(AudioBusId bus) const
    {
        return IsValidBus(bus) ? m_Buses[BusIndex(bus)].DuckGain : 0.0f;
    }

    float AudioMixerDomain::GetBusVolume(AudioBusId bus) const
    {
        if (!IsValidBus(bus))
        {
            return 0.0f;
        }
        const auto& state = m_Buses[BusIndex(bus)];
        return state.BaseGain * MapUserVolumeToGain(state.UserVolume) * state.DuckGain *
               (state.bPauseAffected ? m_PauseGain : 1.0f);
    }

    float AudioMixerDomain::GetEffectiveBusGain(AudioBusId bus) const
    {
        if (!IsValidBus(bus))
        {
            return 0.0f;
        }
        float gain = GetBusVolume(bus);
        while (bus != AudioBusId::Master)
        {
            bus = m_Buses[BusIndex(bus)].Parent;
            gain *= GetBusVolume(bus);
        }
        return gain;
    }

    AudioResult AudioMixerDomain::SendChangedVolumes()
    {
        AudioResult firstFailure = AudioResult::Success;
        if (m_Service)
        {
            for (uint32_t index = 0; index < kAudioBusCount; ++index)
            {
                const auto bus = static_cast<AudioBusId>(index);
                const float gain = GetBusVolume(bus);
                auto& lastSent = m_Buses[index].LastSentGain;
                if (gain == lastSent)
                {
                    continue;
                }

                const auto result = m_Service->SetBusVolume(bus, gain);
                if (result == AudioResult::Success)
                {
                    lastSent = gain;
                }
                else if (firstFailure == AudioResult::Success)
                {
                    firstFailure = result;
                }
            }
        }
        return firstFailure;
    }

    AudioResult AudioMixerDomain::ReapplyBackendState()
    {
        if (!m_Service)
        {
            return AudioResult::InvalidState;
        }
        AudioResult firstFailure = AudioResult::Success;
        // Master が先頭で、残りはすべてその子。全バスの作成を試みてから音量を送る。
        for (uint32_t index = 0; index < kAudioBusCount; ++index)
        {
            auto& bus = m_Buses[index];
            bus.LastSentGain = -1.0f;
            const auto result = m_Service->CreateBus(static_cast<AudioBusId>(index), bus.Parent);
            if (result != AudioResult::Success && result != AudioResult::AlreadyInState &&
                firstFailure == AudioResult::Success)
            {
                firstFailure = result;
            }
        }
        const auto volumeResult = SendChangedVolumes();
        return firstFailure == AudioResult::Success ? volumeResult : firstFailure;
    }
}
