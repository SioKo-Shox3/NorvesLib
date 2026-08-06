#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    struct AudioCookResult
    {
        Core::Container::VariableArray<uint8_t> NvaudBytes;
        uint64_t SourceHash = 0;
        uint32_t SampleRate = 0;
        uint16_t ChannelCount = 0;
        uint16_t BlockAlignment = 0;
        uint64_t FrameCount = 0;
    };

    [[nodiscard]] bool IsSupportedAudioCookFormat(Core::Container::AnsiStringView format);

    [[nodiscard]] bool CookWaveToNvaud(const uint8_t* sourceData,
                                       size_t sourceSize,
                                       Core::Container::AnsiStringView format,
                                       AudioCookResult& outResult,
                                       Core::Container::AnsiString& outError);
} // namespace NorvesLib::Tools::AssetCook
