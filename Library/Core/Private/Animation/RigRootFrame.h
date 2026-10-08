#pragma once
// profile2の静的親frame。有限な正similarityだけを扱い、任意affineには広げない。
#include "Animation/RigBindingTypes.h"
namespace NorvesLib::Core::Skeletal
{
    [[nodiscard]] bool IsSupportedRigImportProfile(RigImportProfile) noexcept;
    [[nodiscard]] bool IsValidRigRootFrame(const RigRootFrame&, RigImportProfile) noexcept;
    void CanonicalizeRigRootFrameZero(RigRootFrame&) noexcept;
    [[nodiscard]] bool SameRigRootFrame(const RigRootFrame&, const RigRootFrame&) noexcept;
    [[nodiscard]] uint64_t RigRootFrameHash(const RigRootFrame&) noexcept;
    [[nodiscard]] double RigRootFrameMaximumDifference(const RigRootFrame&, const RigRootFrame&) noexcept;
} // namespace NorvesLib::Core::Skeletal
