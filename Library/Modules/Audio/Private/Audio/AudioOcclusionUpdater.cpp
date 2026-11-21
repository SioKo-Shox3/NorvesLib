// 追跡した声の寿命を確認し、遮蔽の評価結果を平滑化して適用する。
#include "Audio/AudioOcclusionUpdater.h"

#include "Audio/IAudioService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        bool IsFinitePosition(const Math::Vector3& position)
        {
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
        }

        float PositiveOrDefault(float value, float fallback)
        {
            return std::isfinite(value) && value > 0.0f ? value : fallback;
        }
    }

    size_t AudioOcclusionUpdater::FindVoice(VoiceHandle handle) const
    {
        const auto it = std::find_if(m_Voices.begin(), m_Voices.end(),
                                     [handle](const TrackedVoice& voice) { return voice.Handle == handle; });
        return static_cast<size_t>(it - m_Voices.begin());
    }

    AudioResult AudioOcclusionUpdater::TrackVoiceAcoustics(VoiceHandle handle, const Math::Vector3& sourcePosition,
                                                           Core::Scene::ESoundClass soundClass, uint8_t priority)
    {
        if (!handle.IsValid() || !IsFinitePosition(sourcePosition) || soundClass >= Core::Scene::ESoundClass::Count)
        {
            return AudioResult::Rejected;
        }
        const size_t index = FindVoice(handle);
        if (index == m_Voices.size())
        {
            auto& voice = m_Voices.emplace_back();
            voice.Handle = handle;
            voice.RegistrationOrder = m_NextRegistrationOrder++;
        }
        auto& voice = m_Voices[index];
        voice.Source = sourcePosition;
        voice.SoundClass = soundClass;
        voice.Priority = priority;
        return AudioResult::Success;
    }

    AudioResult AudioOcclusionUpdater::SetVoiceSourcePosition(VoiceHandle handle, const Math::Vector3& sourcePosition)
    {
        const size_t index = FindVoice(handle);
        if (index == m_Voices.size())
        {
            return AudioResult::StaleHandle;
        }
        if (!IsFinitePosition(sourcePosition))
        {
            return AudioResult::Rejected;
        }
        m_Voices[index].Source = sourcePosition;
        return AudioResult::Success;
    }

    AudioResult AudioOcclusionUpdater::UntrackVoice(VoiceHandle handle)
    {
        const size_t index = FindVoice(handle);
        if (index == m_Voices.size())
        {
            return AudioResult::StaleHandle;
        }
        m_Voices.erase(m_Voices.begin() + index);
        return AudioResult::Success;
    }

    void AudioOcclusionUpdater::SetListenerPosition(const Math::Vector3& position)
    {
        if (IsFinitePosition(position))
        {
            m_Listener = position;
            m_bListenerSet = true;
        }
    }

    void AudioOcclusionUpdater::ClearListenerPosition()
    {
        m_bListenerSet = false;
    }

    bool AudioOcclusionUpdater::TryGetVoiceAcoustics(VoiceHandle handle, VoiceAcousticParams& outCurrent) const
    {
        const size_t index = FindVoice(handle);
        if (index == m_Voices.size() || !m_Voices[index].bApplied)
        {
            return false;
        }
        outCurrent = m_Voices[index].Current;
        return true;
    }

    AudioOcclusionDiagnostics AudioOcclusionUpdater::GetDiagnostics() const
    {
        auto diagnostics = m_Diagnostics;
        diagnostics.TrackedVoiceCount = static_cast<uint32_t>(m_Voices.size());
        return diagnostics;
    }

    void AudioOcclusionUpdater::SetSettings(const AudioOcclusionSettings& settings)
    {
        const AudioOcclusionSettings defaults;
        m_Settings.VoiceBudgetPerFrame = settings.VoiceBudgetPerFrame > 0 ? settings.VoiceBudgetPerFrame : defaults.VoiceBudgetPerFrame;
        m_Settings.UpdateHz = PositiveOrDefault(settings.UpdateHz, defaults.UpdateHz);
        m_Settings.SmoothingTimeConstantSeconds = PositiveOrDefault(settings.SmoothingTimeConstantSeconds, defaults.SmoothingTimeConstantSeconds);
    }

    void AudioOcclusionUpdater::Reset()
    {
        m_Voices.clear();
        m_Candidates.clear();
        ClearListenerPosition();
        m_ElapsedSeconds = 0.0;
        m_NextRegistrationOrder = 0;
        m_Diagnostics.EvaluatedLastFrame = 0;
    }

    void AudioOcclusionUpdater::Tick(IAudioService& service, const Core::Scene::AcousticTransmissionEvaluator* evaluator,
                                     float deltaSeconds)
    {
        const double dt = PositiveOrDefault(deltaSeconds, 0.0f);
        m_ElapsedSeconds += dt;
        m_Diagnostics.EvaluatedLastFrame = 0;
        m_Diagnostics.PrunedVoiceCount += std::erase_if(m_Voices, [&service](const TrackedVoice& voice)
        {
            AudioVoiceState state;
            return service.GetVoiceState(voice.Handle, state) != AudioResult::Success ||
                   (state != AudioVoiceState::Created && state != AudioVoiceState::Submitted && state != AudioVoiceState::Playing);
        });

        if (evaluator && m_bListenerSet)
        {
            EvaluateDueVoices(*evaluator);
        }
        // 1 − exp(−dt / τ) を、小さな dt でも桁落ちしない形で求める。
        SmoothAndApply(service, -std::expm1(-dt / m_Settings.SmoothingTimeConstantSeconds));
    }

    void AudioOcclusionUpdater::EvaluateDueVoices(const Core::Scene::AcousticTransmissionEvaluator& evaluator)
    {
        const auto& settings = evaluator.GetSettings();
        const size_t budget = settings.VoiceBudgetPerFrame > 0 ? settings.VoiceBudgetPerFrame : m_Settings.VoiceBudgetPerFrame;
        const double period = 1.0 / PositiveOrDefault(settings.UpdateHz, m_Settings.UpdateHz);
        m_Candidates.clear();
        for (size_t i = 0; i < m_Voices.size(); ++i)
        {
            const auto& voice = m_Voices[i];
            const double age = m_ElapsedSeconds - voice.LastEvaluatedSeconds;
            if (!voice.bEvaluated || age >= period)
            {
                // 待ち時間は伸び続け、優先度の加点は最大 1 周期なので、低い優先度にも順番が回る。
                const double score = voice.bEvaluated ? age + (voice.Priority / 255.0) * period
                                                      : std::numeric_limits<double>::infinity();
                m_Candidates.push_back({i, score});
            }
        }
        std::sort(m_Candidates.begin(), m_Candidates.end(), [this](const Candidate& a, const Candidate& b)
        {
            if (a.Score != b.Score)
            {
                return a.Score > b.Score;
            }
            return m_Voices[a.VoiceIndex].RegistrationOrder < m_Voices[b.VoiceIndex].RegistrationOrder;
        });
        const size_t count = std::min(budget, m_Candidates.size());
        for (size_t i = 0; i < count; ++i)
        {
            EvaluateVoice(m_Voices[m_Candidates[i].VoiceIndex], evaluator);
        }
    }

    void AudioOcclusionUpdater::EvaluateVoice(TrackedVoice& voice, const Core::Scene::AcousticTransmissionEvaluator& evaluator)
    {
        Core::Scene::AcousticPathQuery query;
        query.Source = voice.Source;
        query.Listener = m_Listener;
        query.SoundClass = voice.SoundClass;
        Core::Scene::AcousticPathResult result;
        if (evaluator.Evaluate(query, result) == Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            // 距離減衰は GR73 の定位が出力係数に掛けるため、ここでは静的幾何・草・地形の遮蔽分だけを当てる。
            // 分類別の遮蔽・マスキングの免除は評価器が利得と高域損失に反映済み。
            const float distanceGainDb = evaluator.GetDistanceGainDb(voice.SoundClass, result.DistanceM);
            voice.Target.TargetGainDb = std::clamp(result.GainDb - distanceGainDb, kVoiceAcousticMinGainDb, 0.0f);
            voice.Target.LowpassCutoffHz = std::clamp(evaluator.HighFrequencyLossToCutoffHz(result.HighFrequencyLossDb),
                                                     kVoiceAcousticMinCutoffHz, kVoiceAcousticMaxCutoffHz);
            if (!voice.bEvaluated)
            {
                // 鳴り始めに開けた音から曇っていくのを避け、初回は経路の値を直接使う。
                voice.Current = voice.Target;
            }
        }
        else
        {
            ++m_Diagnostics.EvaluationFailureCount;
        }
        voice.LastEvaluatedSeconds = m_ElapsedSeconds;
        voice.bEvaluated = true;
        ++m_Diagnostics.EvaluationCount;
        ++m_Diagnostics.EvaluatedLastFrame;
    }

    void AudioOcclusionUpdater::SmoothAndApply(IAudioService& service, double alpha)
    {
        constexpr float gainThresholdDb = 0.01f;
        constexpr double cutoffThresholdLog2 = 0.001;
        for (size_t i = m_Voices.size(); i > 0; --i)
        {
            auto& voice = m_Voices[i - 1];
            if (!voice.bEvaluated)
            {
                continue;
            }
            auto& current = voice.Current;
            current.TargetGainDb = static_cast<float>(std::lerp(double(current.TargetGainDb), double(voice.Target.TargetGainDb), alpha));
            if (current.LowpassCutoffHz != voice.Target.LowpassCutoffHz)
            {
                const double logCut = std::log2(double(current.LowpassCutoffHz));
                const double logTarget = std::log2(double(voice.Target.LowpassCutoffHz));
                current.LowpassCutoffHz = static_cast<float>(std::exp2(std::lerp(logCut, logTarget, alpha)));
            }
            const auto& last = voice.LastApplied;
            if (voice.bApplied && !voice.bRetryApply &&
                std::abs(current.TargetGainDb - last.TargetGainDb) < gainThresholdDb &&
                std::abs(std::log2(double(current.LowpassCutoffHz) / last.LowpassCutoffHz)) < cutoffThresholdLog2)
            {
                continue;
            }
            const AudioResult result = service.SetVoiceAcoustics(voice.Handle, current);
            if (result == AudioResult::Success)
            {
                voice.LastApplied = current;
                voice.bApplied = true;
                voice.bRetryApply = false;
                ++m_Diagnostics.AppliedCount;
            }
            else if (result == AudioResult::StaleHandle || result == AudioResult::InvalidState)
            {
                voice = m_Voices.back();
                m_Voices.pop_back();
                ++m_Diagnostics.PrunedVoiceCount;
            }
            else if (result == AudioResult::BackendFailure)
            {
                voice.bRetryApply = true;
            }
        }
    }
}
