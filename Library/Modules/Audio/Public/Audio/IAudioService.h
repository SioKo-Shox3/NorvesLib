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

        /** @brief 再生範囲とピッチ上限を指定してボイスを作成する。 */
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
    };
} // namespace NorvesLib::Modules::Audio
