#pragma once

#include <cstddef>

namespace NorvesLib::Core::Gltf
{
    struct AccessorByteRange
    {
        bool bValid = false;
        size_t StartOffset = 0;
        size_t RequiredBytes = 0;
    };
    // 純粋なbyte範囲検証。整列/type/strideの形式固有制約は各parserの責務とする。
    // count/elementSizeは正。viewとaccessorを宣言buffer範囲に閉じ、演算前に減算/除算で確認する。
    [[nodiscard]] constexpr AccessorByteRange ComputeAccessorByteRange(size_t actualBufferSize,
        size_t declaredBufferSize, size_t viewOffset, size_t viewLength, size_t accessorOffset,
        size_t count, size_t elementSize, size_t stride) noexcept
    {
        if (declaredBufferSize > actualBufferSize || viewOffset > declaredBufferSize ||
            viewLength > declaredBufferSize - viewOffset || accessorOffset > viewLength ||
            count == 0 || elementSize == 0 || stride < elementSize)
        {
            return {};
        }
        const size_t available = viewLength - accessorOffset;
        if (elementSize > available || count - 1 > (available - elementSize) / stride)
        {
            return {};
        }
        // 上の比較により、加算/乗算の結果がviewおよびsize_t内に収まる。
        return {true, viewOffset + accessorOffset, (count - 1) * stride + elementSize};
    }
} // namespace NorvesLib::Core::Gltf
