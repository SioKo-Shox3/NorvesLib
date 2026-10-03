#pragma once

#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    enum class ImageInspectionStatus { Success, InvalidInput, UnsupportedFormat, InvalidDimensions, DecodedSizeLimit, DecodeFailed };
    struct ImageChannelInspection
    {
        uint32_t Minimum = 0;
        uint32_t Maximum = 0;
        double Mean = 0;
    };
    struct ImageInspection
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t Channels = 0;
        uint32_t BitsPerChannel = 0;
        ImageChannelInspection Channel[4];
    };
    // 最終pixel payloadの上限。stb内部の作業領域/IDAT inflateを含む総メモリ上限ではない。
    inline constexpr size_t MaximumInspectionDecodedBytes = 512 * 1024 * 1024;
    // PNG/JPEGを実stbで展開する。8/16bitの整数sample統計で、色空間の線形化は行わない。
    // 入力と出力は独立。失敗時outを保持し、decoded storageは呼出内で解放する。
    [[nodiscard]] ImageInspectionStatus InspectImage(Core::Container::Span<const uint8_t> encoded,
        ImageInspection& outInspection) noexcept;
} // namespace NorvesLib::Tools::AssetCook
