#pragma once
// 時刻や外部形式を持たない、1frameの回転対応。数学は列ベクトル規約。
#include "SkeletalJointMapping.h"
#include "Resource/BvhEvaluate.h"

namespace NorvesLib::Core::Animation
{
    constexpr size_t SkeletalRetargetMaxJoints = 1024;
    enum class SkeletalRetargetRotationPolicy : uint8_t
    {
        Unspecified,
        PreserveHeadingHoldTranslations
    };
    enum class SkeletalRetargetStatus : uint8_t
    {
        Success,
        InvalidInput,
        LimitExceeded,
        InvalidPolicy,
        InvalidHierarchy,
        InvalidRoot,
        InvalidMapping,
        DuplicateSource,
        DuplicateTarget,
        InvalidRotation,
        UnsupportedFloatEnvironment,
        NonFiniteTransform,
        UnsupportedAffine,
        DegenerateScale,
        NonUniformScale,
        Shear,
        Reflection,
        BindReconstructionMismatch,
        FloatRealizationMismatch
    };
    struct SkeletalRetargetResult
    {
        SkeletalRetargetStatus Status = SkeletalRetargetStatus::InvalidInput;
        SkeletalJointMappingSide Side = SkeletalJointMappingSide::None;
        uint32_t JointIndex = UINT32_MAX;
        size_t EntryIndex = SIZE_MAX;
        double AngularErrorRadians = 0;
        bool bFloatRealizationChecked = false;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalRetargetStatus::Success;
        }
    };
    struct SkeletalRetargetSourceRotation
    {
        int32_t ParentIndex = -1;
        Bvh::Matrix3d WorldRotation;
    };
    struct SkeletalRetargetTargetRotation
    {
        int32_t ParentIndex = -1;
        Bvh::Matrix3d BindLocalRotation, BindWorldRotation;
    };
    struct SkeletalRetargetRotationValue
    {
        uint32_t TargetIndex = UINT32_MAX;
        float X = 0, Y = 0, Z = 0, W = 1;
    };
    struct SkeletalRetargetRotationRequest
    {
        Container::Span<const SkeletalRetargetSourceRotation> Source;
        Container::Span<const SkeletalRetargetTargetRotation> Target;
        Container::Span<const SkeletalJointMappingPair> Mappings;
        Container::Span<const Bvh::Matrix3d> Corrections;
        SkeletalJointMappingRoot Root;
        SkeletalSourceReusePolicy SourceReuse = SkeletalSourceReusePolicy::Unspecified;
        SkeletalRetargetRotationPolicy Policy = SkeletalRetargetRotationPolicy::Unspecified;
    };
    struct SkeletalRetargetRotationWork
    {
        Bvh::Matrix3d LocalRotation, WorldRotation;
        SkeletalRetargetRotationValue Value;
        int32_t MappingIndex = -1;
        bool bDone = false;
    };
    // 有効な領域を借用する。workは失敗時に部分更新され得る。outは成功時だけ入力mapping順で更新する。
    // work/outと入力、およびworkとoutの重なりは拒否する。入力は呼出し中不変。
    [[nodiscard]] SkeletalRetargetResult EvaluateSkeletalRotationFrame(
        const SkeletalRetargetRotationRequest& request, Container::Span<SkeletalRetargetRotationWork> work,
        Container::Span<SkeletalRetargetRotationValue> out) noexcept;
} // namespace NorvesLib::Core::Animation
