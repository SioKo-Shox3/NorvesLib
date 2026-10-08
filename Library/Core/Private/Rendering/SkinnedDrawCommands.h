#pragma once

#include "Rendering/FramePacket.h"

namespace NorvesLib::Core::Rendering
{
    // 描画threadのmutable Resourceに触らず、proxyとimmutable leaseから範囲commandを生成する。
    [[nodiscard]] CommandRange AppendSkinnedDrawCommands(FramePacket* packet,
        const Container::VariableArray<SkinnedMeshProxy>& proxies);
} // namespace NorvesLib::Core::Rendering
