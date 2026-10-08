#pragma once

#include "Resource/GltfBufferSet.h"
#include <cmath>
#include <limits>

namespace NorvesLib::Core
{
    class JsonValue;
}

namespace NorvesLib::Core::Gltf
{
    struct BufferByteLengthOutcome
    {
        bool bValid = false;
        size_t Value = 0;
    };
    // doubleで正確に表せる正の整数だけをsize_tへ変換する。失敗値は0。
    [[nodiscard]] inline BufferByteLengthOutcome ParseBufferByteLength(double value) noexcept
    {
        if (!std::isfinite(value) || value <= 0 || value > 9007199254740991.0 ||
            value > static_cast<double>(std::numeric_limits<size_t>::max()) || std::floor(value) != value)
        {
            return {};
        }
        return {true, static_cast<size_t>(value)};
    }
    // root.buffersの記述だけを検証してresolverへ渡す。URI storageは呼出中に所有する。
    // 失敗/例外ではoutSetを空にし、確保/reader例外は伝播する。JSON文法や他fieldは検証しない。
    [[nodiscard]] BufferResolveOutcome ResolveJsonBuffers(const JsonValue& root, const ContainerView& container,
        ExternalBufferReader reader, void* context, BufferSet& outSet);
} // namespace NorvesLib::Core::Gltf
