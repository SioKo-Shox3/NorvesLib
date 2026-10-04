#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    // 既存glTF/cooker/NVSKEL 0.xが共有する上限。新版の上限変更で旧形式を緩めない。
    inline constexpr std::size_t LegacyMaximumJointCount = 128;
    // 1mesh内のprimitive/材質slot上限。Rendering層を参照しない共通profile。
    inline constexpr std::size_t MaximumSubmeshCount = 8;
    inline constexpr std::size_t MaximumMaterialSlotCount = 8;
    // 任意metadataが存在するときだけ使用する。省略時の0は「未知」で、既定1とは解釈しない。
    [[nodiscard]] constexpr bool IsValidSkeletalTableMetadata(uint64_t submeshes, uint64_t slots, uint64_t indices) noexcept
    {
        return submeshes > 0 && submeshes <= MaximumSubmeshCount && slots > 0 && slots <= MaximumMaterialSlotCount &&
            indices > 0 && indices <= UINT32_MAX && indices % 3 == 0 && submeshes <= indices / 3;
    }
}
