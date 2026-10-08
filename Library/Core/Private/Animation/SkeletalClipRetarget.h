#pragma once
#include "Animation/SkeletalRoleProfile.h"
#include "Animation/SkeletalClipProcessing.h"
#include "Resource/RigAuthoring.h"
namespace NorvesLib::Core::Animation
{
    struct SkeletalClipRetargetSettings
    {
        SkeletalRestCorrectionMode RestMode = SkeletalRestCorrectionMode::Explicit;
        Bvh::Vector3d UpHint{0, 1, 0};
        double MaximumRestErrorRadians = 0.08726646259971647;
        double RootScale = 1;
        bool bAutoRootHeight = false;
        uint32_t LoopExcludedRoles = 0;
        SkeletalClipProcessingSettings Processing;
    };
    struct SkeletalRetargetJoint
    {
        Container::AnsiString Name;
        int32_t ParentIndex = -1;
        Skeletal::SkeletalRestTransform Rest;
    };
    struct SkeletalRetargetClipSource
    {
        Container::VariableArray<SkeletalRetargetJoint> Joints;
        Skeletal::SkeletalAnimationClip Clip;
        Skeletal::RigRootFrame RootFrame = Skeletal::IdentityRigRootFrame();
        double IntervalSeconds = 0;
        uint32_t DroppedTranslationChannels = 0;
    };
    struct SkeletalRestCorrectionReport
    {
        uint32_t SourceJoint = 0, TargetJoint = 0;
        Bvh::Matrix3d Correction;
        double BeforeRadians = 0, AfterRadians = 0;
        bool bDirectionMissing = false;
    };
    struct SkeletalClipRetargetReport
    {
        SkeletalClipProcessingReport Processing;
        Container::VariableArray<SkeletalRestCorrectionReport> Corrections;
        uint32_t UnmappedSource = 0, UnmappedTarget = 0, IgnoredTranslationChannels = 0;
        double RootScale = 1;
        bool bKeyErrorMeasured = false;
    };
    [[nodiscard]] bool MakeBvhRetargetClipSource(const Bvh::BvhDocument&, const SkeletalBvhClipSettings&,
                                                 const Container::String& name, SkeletalRetargetClipSource&,
                                                 Container::AnsiString& error);
    [[nodiscard]] bool MakeGltfRetargetClipSource(const Skeletal::RigAuthoringCpu&, size_t clipIndex,
                                                  SkeletalRetargetClipSource&, Container::AnsiString& error);
    // 元作者rest→現在の作者restへの明示変換。targetのIBMやt=0からrestを復元しない。
    [[nodiscard]] bool RetargetSkeletalClip(const SkeletalRetargetClipSource&, const Skeletal::RigAuthoringCpu& target,
                                            const SkeletalRoleProfile&, const SkeletalClipRetargetSettings&,
                                            Skeletal::SkeletalAnimationClip&, SkeletalClipRetargetReport&,
                                            Container::AnsiString& error);
} // namespace NorvesLib::Core::Animation
