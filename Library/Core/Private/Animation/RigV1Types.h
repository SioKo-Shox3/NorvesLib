#pragma once
// v1の有限profileと診断。legacy0.xの上限や値規則には使わない。
#include "Animation/RigBindingTypes.h"
#include "Container/PointerTypes.h"
#include "Container/Span.h"
namespace NorvesLib::Core::Skeletal
{
    struct RigTopologyJoint
    {
        Container::AnsiString Name;
        int32_t ParentIndex = -1;
    };
    struct RigTopology
    {
        uint64_t SkeletonId = 0;
        Container::VariableArray<RigTopologyJoint> Joints;
        Container::VariableArray<uint32_t> SourceToCanonical, CanonicalToSource;
        Container::VariableArray<uint8_t> CanonicalBytes;
    };
    [[nodiscard]] bool IsValidRigV1Limits(const RigV1Limits&) noexcept;
    [[nodiscard]] bool IsValidRigBindingPolicy(const RigBindingPolicy&) noexcept;
    [[nodiscard]] uint64_t RigBytesHash(Container::Span<const uint8_t>) noexcept;
    // 親番号は入力順に依らず名前順へ写す。失敗/確保例外でoutを変更しない。
    [[nodiscard]] RigV1Status BuildRigTopology(Container::Span<const SkeletalJoint>, const RigV1Limits&,
                                               RigTopology& out);
    [[nodiscard]] bool SameRigTopology(const RigTopology&, const RigTopology&) noexcept;
    [[nodiscard]] uint64_t RigRestHash(Container::Span<const SkeletalRestTransform>) noexcept;
} // namespace NorvesLib::Core::Skeletal
