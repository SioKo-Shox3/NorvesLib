// 音量設定、ポーズ、ダッキングをゲーム側から操作するミキサーの公開窓口。
#pragma once

#include "Audio/AudioTypes.h"
#include "Container/Span.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Modules::Audio
{
    /** 設定ファイルで使う小文字のバス名を返す。範囲外なら空文字列を返す。 */
    [[nodiscard]] const char* GetAudioBusName(AudioBusId bus) noexcept;
    /** 小文字のバス名を完全一致で解決する。失敗時は outBus を保つ。 */
    [[nodiscard]] bool TryParseAudioBusName(Core::Container::AnsiStringView name, AudioBusId& outBus) noexcept;
    /** スライダー値を [0, 1] に丸めて二乗し、線形ゲインにする。非有限値は 0 とする。 */
    [[nodiscard]] float MapUserVolumeToGain(float slider01) noexcept;

    /** ダッキングの開始を判定する活動の種類。 */
    enum class AudioDuckTriggerKind : uint8_t
    {
        Bus,
        DuckTag,
    };

    /** 再生中のバスまたはタグに応じて、対象バスを減衰させる規則。時間の単位はミリ秒。 */
    struct AudioDuckRule
    {
        AudioDuckTriggerKind TriggerKind = AudioDuckTriggerKind::Bus;
        AudioBusId TriggerBus = AudioBusId::Ui;
        Core::Identity TriggerTag;
        AudioBusId Target = AudioBusId::Music;
        float DuckDb = -6.0f;
        uint32_t AttackMs = 100;
        uint32_t HoldMs = 0;
        uint32_t ReleaseMs = 500;
    };

    /** バスの送り先、基準音量、ポーズの影響を表す。Master 自身の親は参照しない。 */
    struct AudioBusConfig
    {
        AudioBusId Parent = AudioBusId::Master;
        float VolumeDb = 0.0f;
        bool bPauseAffected = true;
    };

    /** バスごとの構成と、活動に応じたダッキングの規則。 */
    struct AudioMixerConfig
    {
        AudioBusConfig Buses[kAudioBusCount];
        Core::Container::VariableArray<AudioDuckRule> DuckRules;
    };

    /** 全バスを 0 dB、子の親を Master とし、Master と Ui をポーズ対象から外す。規則は空。 */
    [[nodiscard]] AudioMixerConfig MakeDefaultAudioMixerConfig();

    /** GameThread 専用。設定値と合成後の音量を保持し、音声サービスへ反映する。 */
    class IAudioMixer
    {
    public:
        virtual ~IAudioMixer() = default;

        /** 設定の音量を [0, 1] に丸めて保存する。範囲外のバス・非有限値には InvalidState を返す。 */
        virtual AudioResult SetUserBusVolume(AudioBusId bus, float slider01) = 0;
        /** 設定のスライダー値を返す。範囲外のバスでは 0 を返す。 */
        [[nodiscard]] virtual float GetUserBusVolume(AudioBusId bus) const = 0;
        /** ポーズ時の線形ゲインを [0, 1] に丸める。非有限値には InvalidState を返す。 */
        virtual AudioResult SetPauseGain(float gain) = 0;
        /** ポーズ対象の各バスに掛ける現在の倍率を返す。 */
        [[nodiscard]] virtual float GetPauseGain() const = 0;
        /** 規則を一括で替え、各減衰を 0 dB に戻す。不正な規則があれば変更せず InvalidState を返す。 */
        virtual AudioResult SetDuckRules(Core::Container::Span<const AudioDuckRule> rules) = 0;
        /** 対象バスの最も深いダッキングを線形倍率で返す。範囲外のバスでは 0 を返す。 */
        [[nodiscard]] virtual float GetBusDuckGain(AudioBusId bus) const = 0;
        /** バス自身のサブミックスに送る線形ゲインを返す。範囲外のバスでは 0 を返す。 */
        [[nodiscard]] virtual float GetBusVolume(AudioBusId bus) const = 0;
        /** Master までの親の音量を掛けた実効ゲインを返す。範囲外のバスでは 0 を返す。 */
        [[nodiscard]] virtual float GetEffectiveBusGain(AudioBusId bus) const = 0;
        /** 再初期化されたサービスへ全バスの構成と音量を再送し、最初の失敗を返す。 */
        virtual AudioResult ReapplyBackendState() = 0;
    };
}
