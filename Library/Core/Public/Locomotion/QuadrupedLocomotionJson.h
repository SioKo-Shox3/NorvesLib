#pragma once
#include "Container/Span.h"
#include "Container/String.h"
#include "Locomotion/QuadrupedLocomotionParams.h"
namespace NorvesLib::Core::Locomotion
{
    // 完全なlocomotion.v1文書を読む。未知/重複field・欠落・非有限値・矛盾値を拒否する。
    // UTF8 BOMは任意。失敗時outは保持し、errorだけ更新する。
    bool ParseQuadrupedLocomotionJson(Container::Span<const uint8_t> bytes, QuadrupedLocomotionParams& out,
                                      Container::String& error);
} // namespace NorvesLib::Core::Locomotion
