// バスの音量と規則ごとのダッキングの状態をドメインで保持する。
#pragma once

#include "Audio/AudioMixer.h"

namespace NorvesLib::Modules::Audio
{
    class IAudioService;
}

namespace NorvesLib::Modules::Audio::Private
{
    /** このフレームに活動中のインスタンスの数とダッキングタグ。 */
    struct AudioMixerActivity
    {
        uint32_t ActiveInstancesPerBus[kAudioBusCount] = {};
        Core::Container::VariableArray<Core::Identity> ActiveDuckTags;
    };

    class AudioMixerDomain final : public IAudioMixer
    {
    public:
        AudioMixerDomain();
        AudioMixerDomain(const AudioMixerDomain&) = delete;
        AudioMixerDomain& operator=(const AudioMixerDomain&) = delete;

        /** 反映先を借りる。nullptr で結合を外し、設定値を保持する。 */
        void BindService(IAudioService* service);
        /** 基準音量・ポーズ対象・規則をまとめて替える。不正な値があれば前の設定を保つ。 */
        void ApplyConfig(const AudioMixerConfig& config);
        /** 明示読み込みも含めて初回の試行前なら、既定の論理パスから設定を読む。 */
        void EnsureDefaultConfigLoaded(const Core::Asset::AssetSystem& assets);
        /** 現在の活動からダッキングを進め、音量の差分を反映する。 */
        void Update(float deltaSeconds, const AudioMixerActivity& activity);
        /** 全規則のダッキングを即座に 0 dB へ戻し、保持時間を捨てる。SoundSystem の終了時に呼ぶ。 */
        void ResetActivity();
        /** ダッキングを解除して反映先を切り離す。バスの音量と設定は保持する。 */
        void Shutdown();

        AudioResult LoadConfig(const Core::Asset::AssetSystem& assets, Core::Container::AnsiStringView logicalPath,
                               AudioMixerLoadReport& outReport) override;
        AudioResult SetUserBusVolume(AudioBusId bus, float slider01) override;
        [[nodiscard]] float GetUserBusVolume(AudioBusId bus) const override;
        AudioResult SetPauseGain(float gain) override;
        [[nodiscard]] float GetPauseGain() const override;
        AudioResult SetDuckRules(Core::Container::Span<const AudioDuckRule> rules) override;
        [[nodiscard]] float GetBusDuckGain(AudioBusId bus) const override;
        [[nodiscard]] float GetBusVolume(AudioBusId bus) const override;
        [[nodiscard]] float GetEffectiveBusGain(AudioBusId bus) const override;
        AudioResult ReapplyBackendState() override;

    private:
        struct BusState
        {
            AudioBusId Parent = AudioBusId::Master;
            bool bPauseAffected = true;
            float BaseGain = 1.0f;
            float UserVolume = 1.0f;
            float DuckGain = 1.0f;
            float LastSentGain = -1.0f; ///< 有効なゲインは非負。負なら送信の成功記録がない。
        };

        struct DuckState
        {
            AudioDuckRule Rule;
            double CurrentDb = 0.0;
            double HoldRemainingMs = 0.0;
        };

        bool ReplaceDuckRules(Core::Container::Span<const AudioDuckRule> rules);
        void ClearDucking();
        AudioResult SendChangedVolumes();

        BusState m_Buses[kAudioBusCount];
        Core::Container::VariableArray<DuckState> m_DuckStates;
        float m_PauseGain = 1.0f;
        bool m_bConfigLoadAttempted = false;
        IAudioService* m_Service = nullptr;
    };
}
