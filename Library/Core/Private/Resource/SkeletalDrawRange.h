#pragma once

#include "Container/Span.h"
#include "Resource/SkeletalSubMesh.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    struct SkeletalDrawRange
    {
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
    };
    // 全表の妥当性はimmutable lease生成時に検査済み。ここでは選択recordとcommandを照合する。
    // 旧空表のcount=0/start=0だけは全index指定。失敗時outは保持する。
    [[nodiscard]] bool ResolveSkeletalDrawRange(Container::Span<const SkeletalSubMesh> submeshes,
        uint64_t totalIndices, uint32_t submeshIndex, uint32_t materialSlot, uint32_t indexOffset,
        uint32_t indexCount, uint32_t vertexOffset, bool bShadow, bool bCastShadow, SkeletalDrawRange& out) noexcept;
} // namespace NorvesLib::Core::Skeletal
