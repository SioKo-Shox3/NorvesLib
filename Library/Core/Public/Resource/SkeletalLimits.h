#pragma once

#include <cstddef>

namespace NorvesLib::Core::Skeletal
{
    // 既存glTF/cooker/NVSKEL 0.xが共有する上限。新版の上限変更で旧形式を緩めない。
    inline constexpr std::size_t LegacyMaximumJointCount = 128;
}
