#pragma once

#include "Audio/AudioClipResource.h"
#include "Container/PointerTypes.h"
#include "Container/StringView.h"

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Modules::Audio::Private
{
    enum class AudioClipLoadResult : uint8_t
    {
        Success,
        NotCooked,
        ParseFailed,
        UnsupportedFormat,
        InvalidPayload,
    };

    // cook 済みを優先し、無ければばらの PCM16 WAV を読む。
    // 失敗時は outClip を空にする。成功した clip が PCM の寿命を保持する。
    [[nodiscard]] AudioClipLoadResult LoadAudioClip(const Core::Asset::AssetSystem& assets,
                                                    Core::Container::AnsiStringView logicalPath,
                                                    Core::Container::TSharedPtr<AudioClipResource>& outClip);
} // namespace NorvesLib::Modules::Audio::Private
