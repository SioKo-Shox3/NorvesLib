#pragma once
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/GeometryTypes.h"
#include <cstdint>
namespace NorvesLib::Core::Skeletal
{
    struct SkeletalVertex;
}
namespace NorvesLib::Core::Animation
{
    struct PoseBoundsSettings
    {
        // 0は全ての正の重みを含む保守的な境界。正の値は見た目で確認する近似モード。
        float WeightThreshold = 0;
        float RelativePadding = 0;
    };
    struct JointBindBounds
    {
        uint32_t JointIndex = 0;
        Math::AABB Bounds = Math::AABB::CreateInvalid();
    };
    struct FallbackBindBounds
    {
        uint64_t RequiredJointCount = UINT64_MAX;
        Math::AABB Bounds = Math::AABB::CreateInvalid();
    };
    struct MeshPoseBounds
    {
        Container::VariableArray<JointBindBounds> Joints;
        Container::VariableArray<FallbackBindBounds> Fallbacks;
        float MaximumWeightSum = 1;
        bool bHasVertices = false;
        bool bNeedsExactFallback = false;
        size_t AllocatedBytes() const noexcept
        {
            return Joints.capacity() * sizeof(JointBindBounds) + Fallbacks.capacity() * sizeof(FallbackBindBounds);
        }
    };
    [[nodiscard]] bool IsValidPoseBoundsSettings(const PoseBoundsSettings&) noexcept;
    void BuildMeshPoseBounds(Container::Span<const Skeletal::SkeletalVertex>, float weightThreshold, MeshPoseBounds&);
    // 法線を計算しない純関数。無効な影響を除外し、有効重み合計<=EPSILONなら元の位置。
    [[nodiscard]] Math::Vector3 SkinPosition(const Skeletal::SkeletalVertex&,
                                             Container::Span<const Math::Matrix4x4> palette);
    [[nodiscard]] bool ComputeExactBounds(Container::Span<const Skeletal::SkeletalVertex>,
                                          Container::Span<const Math::Matrix4x4> palette, bool requireFinite,
                                          Math::AABB& out);
    [[nodiscard]] bool ApplyPoseBoundsPadding(float relativePadding, Math::AABB& bounds);
    // 非affine・overflowなど、角の変換だけで包含を保証できない場合はfalse。callerは正確な境界へ戻す。
    [[nodiscard]] bool TransformJointBounds(Container::Span<const JointBindBounds>, const Math::AABB& fallback,
                                            Container::Span<const Math::Matrix4x4> palette, float relativePadding,
                                            Math::AABB& out, float maximumWeightSum = 1);
} // namespace NorvesLib::Core::Animation
