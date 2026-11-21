// 声ごとの遮蔽を予算内で評価し、音量とローパスを更新する。
#pragma once

#include "Audio/IAudioOcclusion.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Modules::Audio
{
    class IAudioService;
}

namespace NorvesLib::Modules::Audio::Private
{
    struct AudioOcclusionSettings
    {
        uint32_t VoiceBudgetPerFrame = 4;           // 評価器の予算が無効なときに使う声数。
        float UpdateHz = 10.0f;                     // 評価器の頻度が無効なときに使う Hz。
        float SmoothingTimeConstantSeconds = 0.15f; // 平滑化の時定数（秒、暫定値）。
    };

    // GameThread で使用する。サービスと評価器は Tick の間だけ借りる。
    class AudioOcclusionUpdater final : public IAudioOcclusion
    {
    public:
        AudioResult TrackVoiceAcoustics(VoiceHandle handle, const Math::Vector3& sourcePosition,
                                        Core::Scene::ESoundClass soundClass, uint8_t priority) override;
        AudioResult SetVoiceSourcePosition(VoiceHandle handle, const Math::Vector3& sourcePosition) override;
        AudioResult UntrackVoice(VoiceHandle handle) override;
        void SetListenerPosition(const Math::Vector3& position) override;
        void ClearListenerPosition() override;
        [[nodiscard]] bool TryGetVoiceAcoustics(VoiceHandle handle, VoiceAcousticParams& outCurrent) const override;
        [[nodiscard]] AudioOcclusionDiagnostics GetDiagnostics() const override;

        void SetSettings(const AudioOcclusionSettings& settings);
        [[nodiscard]] const AudioOcclusionSettings& GetSettings() const { return m_Settings; }
        void Tick(IAudioService& service, const Core::Scene::AcousticTransmissionEvaluator* evaluator, float deltaSeconds);
        void Reset();

    private:
        struct TrackedVoice
        {
            VoiceHandle Handle;
            Math::Vector3 Source;
            Core::Scene::ESoundClass SoundClass = Core::Scene::ESoundClass::Voice;
            uint8_t Priority = 0;
            uint64_t RegistrationOrder = 0;
            double LastEvaluatedSeconds = 0.0;
            bool bEvaluated = false;
            VoiceAcousticParams Target;
            VoiceAcousticParams Current;
            VoiceAcousticParams LastApplied;
            bool bApplied = false;
            bool bRetryApply = false;
        };

        struct Candidate
        {
            size_t VoiceIndex;
            double Score;
        };

        size_t FindVoice(VoiceHandle handle) const;
        void EvaluateDueVoices(const Core::Scene::AcousticTransmissionEvaluator& evaluator);
        void EvaluateVoice(TrackedVoice& voice, const Core::Scene::AcousticTransmissionEvaluator& evaluator);
        void SmoothAndApply(IAudioService& service, double alpha);

        AudioOcclusionSettings m_Settings;
        AudioOcclusionDiagnostics m_Diagnostics;
        Core::Container::VariableArray<TrackedVoice> m_Voices;
        Core::Container::VariableArray<Candidate> m_Candidates;
        Math::Vector3 m_Listener;
        bool m_bListenerSet = false;
        double m_ElapsedSeconds = 0.0;
        uint64_t m_NextRegistrationOrder = 0;
    };
}
