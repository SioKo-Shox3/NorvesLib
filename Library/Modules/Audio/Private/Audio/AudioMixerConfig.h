// ミキサー設定の JSON を検証し、指定された項目を一括で上書きする。
#pragma once

#include "Audio/AudioMixer.h"

namespace NorvesLib::Modules::Audio::Private
{
    /** 不正な項目は診断にまとめ、失敗時は inOutConfig を保持する。 */
    [[nodiscard]] bool ParseAudioMixerConfig(Core::Container::Span<const uint8_t> jsonBytes,
                                             AudioMixerConfig& inOutConfig, AudioMixerLoadReport& outReport);
}
