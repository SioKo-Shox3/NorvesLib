#pragma once
// BVH/glTFの写像後poseを共通処理する。入力の補間/FK/写像はcallbackが同じ時刻で行う。
#include "Animation/RigBindingTypes.h"
namespace NorvesLib::Core::Animation
{
    enum class SkeletalLoopSelection : uint8_t
    {
        None,
        Auto,
        Range
    };
    struct SkeletalClipProcessingSettings
    {
        double OutputFps = 30;
        SkeletalLoopSelection Loop = SkeletalLoopSelection::Auto;
        double RangeStart = 0, RangeEnd = 0;
        double MinimumPeriod = 0.15, MaximumPeriod = 3;
        double LoopRmsThresholdRadians = 0.05, MinimumMotionRadians = 0.05;
        bool bExtractRootMotion = true;
        // 動画由来の動きにだけ明示して使う。0の閾値/radiusは従来経路を維持する。
        double SpikeThresholdRadians = 0, TimeScale = 1;
        uint32_t SpikeWindowRadius = 2, SmoothingRadius = 0;
        bool bAverageCycles = false;
        bool bAnalyzeContacts = false, bGenerateFootMarkers = false, bDeriveRootMotion = false;
        double DesiredGroundSpeed = 0;
        uint32_t MaximumSamples = 65536, MaximumPoseEvaluations = 262144;
        uint64_t MaximumOutputKeys = uint64_t{1} << 20;
    };
    struct SkeletalClipPoseSource
    {
        Container::String Name;
        double DurationSeconds = 0, SourceIntervalSeconds = 0;
        Container::Span<const uint32_t> JointIndices;
        // 空なら同重み。0重みは周期判定から除外するだけで出力からは除かない。
        Container::Span<const double> RotationWeights;
        uint32_t RootJoint = UINT32_MAX;
        Skeletal::SkeletalRestTransform RootRest;
        Skeletal::RigRootFrame RootFrame = Skeletal::IdentityRigRootFrame();
        void* Context = nullptr;
        // 回転はJointIndices順、列quaternionの絶対local。根の位置も同じtarget author frame内。
        bool (*Sample)(double, Container::Span<Skeletal::SkeletalValue>, Skeletal::SkeletalPosition&, void*) = nullptr;
    };
    struct SkeletalClipProcessingReport
    {
        bool bLoopDetected = false, bRangeSelected = false;
        double StartSeconds = 0, EndSeconds = 0, PeriodSeconds = 0;
        double RecurrenceRmsRadians = 0, SeamBeforeRadians = 0, SeamAfterRadians = 0;
        double SeamVelocityDifferenceRadiansPerSecond = 0;
        double PlanarDistanceMeters = 0, AverageSpeedMetersPerSecond = 0;
        uint32_t PoseEvaluations = 0, OutputSamples = 0;
        uint32_t ReplacedSpikes = 0, SmoothedChannels = 0, AveragedCycles = 1;
    };
    enum class SkeletalClipProcessingStatus : uint8_t
    {
        Success,
        InvalidInput,
        InvalidSettings,
        LimitExceeded,
        SourceRejected,
        InvalidPose,
        DegenerateHeading
    };
    // 成功時のみclip/reportを更新する。最終キーを正確な区間長に置き、蓄積root motionは閉じない。
    [[nodiscard]] SkeletalClipProcessingStatus ProcessSkeletalClip(const SkeletalClipPoseSource&,
                                                                   const SkeletalClipProcessingSettings&,
                                                                   Skeletal::SkeletalAnimationClip&,
                                                                   SkeletalClipProcessingReport&);
} // namespace NorvesLib::Core::Animation
