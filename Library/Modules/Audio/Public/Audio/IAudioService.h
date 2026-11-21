#pragma once

#include "Audio/AudioTypes.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Modules::Audio
{
    class AudioClipResource;

    /**
     * @brief Backend-neutral audio service driven on the GameThread.
     */
    class IAudioService
    {
    public:
        virtual ~IAudioService() = default;

        virtual AudioResult Initialize() = 0;
        virtual AudioResult CreateVoice(const Core::Container::TSharedPtr<AudioClipResource>& clip,
                                        VoiceHandle& outHandle) = 0;
        virtual AudioResult StartVoice(VoiceHandle handle) = 0;
        virtual AudioResult StopVoice(VoiceHandle handle) = 0;
        virtual AudioResult DestroyVoice(VoiceHandle handle) = 0;
        virtual AudioResult GetVoiceState(VoiceHandle handle, AudioVoiceState& outState) const = 0;
        virtual void Tick() = 0;
        virtual AudioResult Shutdown() = 0;
        [[nodiscard]] virtual AudioDiagnostics GetDiagnostics() const = 0;

        /** @brief 再生範囲、ピッチ上限、送り先を指定してボイスを作成する。送り先が未作成なら Master、次いで出力へ送る。 */
        virtual AudioResult CreateVoice(const Core::Container::TSharedPtr<AudioClipResource>& clip,
                                        const AudioVoiceDesc& desc,
                                        VoiceHandle& outHandle) = 0;

        /**
         * @brief 音量を線形倍率で設定する。
         * @details SetVoiceVolume、SetVoicePitch、SetVoicePan は Created、Submitted、Playing でのみ
         * バックエンドを呼ぶ。Stopping、Drained、Quarantined では InvalidState を返し、
         * 隔離されたボイスの寿命の境を守るためバックエンドに触れない。
         */
        virtual AudioResult SetVoiceVolume(VoiceHandle handle, float volume) = 0;
        /** @brief ピッチを半音単位で設定する。 */
        virtual AudioResult SetVoicePitch(VoiceHandle handle, float semitones) = 0;
        /** @brief パンを -1 の左から 1 の右まで等パワーで設定する。 */
        virtual AudioResult SetVoicePan(VoiceHandle handle, float pan) = 0;
        /** @brief 再生中のボイスをバッファを保持したまま一時停止する。 */
        virtual AudioResult PauseVoice(VoiceHandle handle) = 0;
        /** @brief 一時停止中のボイスの再生を再開する。 */
        virtual AudioResult ResumeVoice(VoiceHandle handle) = 0;

        /**
         * @brief 奪取のために、ボイスを止めてすぐ破棄する。
         * @details Created、Submitted、Playing、Stopping、Drained のボイスに使える。
         * Playing ではバッファを捨てて停止した後、バックエンドのボイスを同期的に破棄する。
         * 成功するとハンドルは無効になり、計上済みのボイスは ActiveVoiceCount から減る。
         * 無効なハンドルには StaleHandle、Quarantined には InvalidState を返す。
         * 停止または破棄に失敗すると BackendFailure を返す。停止済みの停止失敗、
         * 不完全な停止結果、破棄失敗ではボイスを隔離する。
         */
        virtual AudioResult AbortVoice(VoiceHandle handle) = 0;

        /**
         * @brief 出力の左右の係数を線形でそのまま設定する(3D の定位で距離のゲインとパンを掛けた値を渡す)。
         * @details 各係数は有限で 0 以上。SetVoicePan と同じく Created・Submitted・Playing でのみバックエンドを呼ぶ。
         */
        virtual AudioResult SetVoiceOutputGains(VoiceHandle handle, float leftGain, float rightGain) = 0;
        /**
         * @brief 周波数比を直接設定する(ピッチとドップラーを掛けた値)。
         * @details 有限で 0 より大きい値を受け、作成時の上限と下限 1/1024 に丸めてバックエンドへ渡す。
         */
        virtual AudioResult SetVoiceFrequencyRatio(VoiceHandle handle, float ratio) = 0;

        /**
         * @brief 初期化後に GameThread でバスを作成する。Shutdown で全バスを破棄する。
         * @details Master は親を無視して出力へ送り、他のバスは作成済みの親へ送る。
         * 作成済みなら設定を保って AlreadyInState を返す。
         */
        virtual AudioResult CreateBus(AudioBusId bus, AudioBusId parent) = 0;
        /** @brief 初期化後に GameThread で、作成済みのバスの音量を有限で 0 以上の線形倍率に設定する。 */
        virtual AudioResult SetBusVolume(AudioBusId bus, float gain) = 0;

        /**
         * @brief 声に遮蔽の利得 (dB) とローパスの遮断周波数 (Hz) をすぐに適用する。平滑化は呼び手が行う。
         * @details SetVoiceVolume が指定した線形音量と、遮蔽の線形利得の積をバックエンドの音量にする。
         * 利得は [-120, 0] dB、遮断周波数は [20, 20000] Hz に丸める。非有限値には InvalidState を返す。
         * Created・Submitted・Playing でのみバックエンドを呼ぶ。Stopping・Drained・Quarantined では
         * バックエンドに触れず InvalidState を返す。無効なハンドルには StaleHandle を返す。
         */
        virtual AudioResult SetVoiceAcoustics(VoiceHandle handle, const VoiceAcousticParams& params) = 0;
    };
} // namespace NorvesLib::Modules::Audio
