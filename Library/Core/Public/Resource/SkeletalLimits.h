#pragma once

#include <cstddef>

namespace NorvesLib::Core::Skeletal
{
    // 既存glTF/cooker/NVSKEL 0.xが共有する上限。新版の上限変更で旧形式を緩めない。
    inline constexpr std::size_t LegacyMaximumJointCount = 128;
    // 1mesh内のprimitive/材質slot上限。Rendering層を参照しない共通profile。
    inline constexpr std::size_t MaximumSubmeshCount = 8;
    inline constexpr std::size_t MaximumMaterialSlotCount = 8;
}
