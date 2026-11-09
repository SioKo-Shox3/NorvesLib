// 間接描画・ディスパッチの引数。各メンバは VkDrawIndirectCommand・VkDispatchIndirectCommand と同じ順序で並ぶ。
#pragma once

#include <cstdint>

namespace NorvesLib::RHI
{
    struct DrawIndirectCommand
    {
        uint32_t VertexCount;
        uint32_t InstanceCount;
        uint32_t FirstVertex;
        uint32_t FirstInstance;
    };
    static_assert(sizeof(DrawIndirectCommand) == 16);

    struct DispatchIndirectCommand
    {
        uint32_t X;
        uint32_t Y;
        uint32_t Z;
    };
    static_assert(sizeof(DispatchIndirectCommand) == 12);
} // namespace NorvesLib::RHI
