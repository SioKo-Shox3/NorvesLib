#pragma once

#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::Gltf
{
    enum class ContainerParseResult : uint8_t
    {
        Success,
        NotGlb,
        InvalidArgument,
        TruncatedHeader,
        UnsupportedVersion,
        LengthMismatch,
        TruncatedChunk,
        InvalidChunkLength,
        MissingJsonChunk,
        InvalidChunkOrder,
        DuplicateChunk
    };

    // 入力バイト列を借用する。使用中は入力の寿命とアドレスを維持し、変更しないこと。
    // JSON/BINのpaddingは保持し、JSONの文法・buffer byteLengthとの照合は上位層で行う。
    struct ContainerView
    {
        Container::Span<const uint8_t> Json;
        Container::Span<const uint8_t> Bin;
        bool IsGlb = false;
        bool HasBin = false;
    };

    // SuccessはGLBの構造解析成功、NotGlbはBOM除去後の全入力をJsonへ渡す非GLB経路。
    // JSON文法の成功を意味しない。その他の失敗時はoutViewを空にする。拡張子/I/O/確保は使わない。
    [[nodiscard]] ContainerParseResult ParseContainer(Container::Span<const uint8_t> file,
        ContainerView& outView) noexcept;
}
