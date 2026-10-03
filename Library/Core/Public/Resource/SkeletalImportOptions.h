#pragma once

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    enum class SkeletalInfluencePolicy : uint8_t { Strict=0, ReduceToFour=1 };
    inline constexpr double DefaultWarnDroppedWeight = 0.01;
    inline constexpr double DefaultFailDroppedWeight = 0.25;
    enum class SkeletalCubicSplinePolicy : uint8_t
    {
        Reject = 0,
        Bake = 1
    };
    inline constexpr double DefaultCubicTranslationToleranceMeters = 0.001;
    inline constexpr double DefaultCubicRotationToleranceRadians = 0.0017453292519943296;
    inline constexpr double DefaultCubicScaleTolerance = 0.001;
    inline constexpr uint32_t DefaultCubicMaximumDepth = 20;
    inline constexpr uint32_t DefaultCubicMaximumSamplesPerChannel = 65536;
    inline constexpr uint32_t DefaultCubicMaximumSamplesPerAsset = 1048576;
    inline constexpr uint32_t MaximumCubicSubdivisionDepth = 24;
    inline constexpr uint32_t MaximumCubicSamplesPerChannel = 1048576;
    inline constexpr uint32_t MaximumCubicSamplesPerAsset = 4194304;
    inline constexpr double CubicRotationNumericErrorBudget = 256 * double(std::numeric_limits<float>::epsilon());
    inline constexpr double CubicVectorNumericErrorFloor = 64 * double(std::numeric_limits<float>::epsilon());
    enum class SkeletalMorphPolicy : uint8_t
    {
        Reject = 0,
        Drop = 1
    };
    struct SkeletalGltfDecodeOptions
    {
        SkeletalInfluencePolicy InfluencePolicy = SkeletalInfluencePolicy::Strict;
        SkeletalMorphPolicy MorphPolicy = SkeletalMorphPolicy::Reject;
        double WarnDroppedWeight = DefaultWarnDroppedWeight;
        double FailDroppedWeight = DefaultFailDroppedWeight;
        SkeletalCubicSplinePolicy CubicSplinePolicy = SkeletalCubicSplinePolicy::Reject;
        double CubicTranslationToleranceMeters = DefaultCubicTranslationToleranceMeters;
        double CubicRotationToleranceRadians = DefaultCubicRotationToleranceRadians;
        double CubicScaleTolerance = DefaultCubicScaleTolerance;
        uint32_t CubicMaximumDepth = DefaultCubicMaximumDepth;
        uint32_t CubicMaximumSamplesPerChannel = DefaultCubicMaximumSamplesPerChannel;
        uint32_t CubicMaximumSamplesPerAsset = DefaultCubicMaximumSamplesPerAsset;
    };
    // 比率は脱落weight/元weight総和。実ポーズの最大変形距離ではない。
    // ProcessedVertexCountは検査済みprefixで、失敗時も全資産の平均と誤認しない。
    struct SkeletalGltfDecodeReport
    {
        uint64_t TotalVertexCount = 0;
        uint64_t ProcessedVertexCount = 0;
        uint64_t ReducedVertexCount = 0;
        uint64_t MergedJointVertexCount = 0;
        uint64_t RenormalizedVertexCount = 0;
        uint64_t WarningVertexCount = 0;
        double MaximumDroppedWeight = 0;
        double MeanDroppedWeight = 0;
        uint64_t FailedVertexIndex = std::numeric_limits<uint64_t>::max();
        // 失敗頂点は正常prefixの平均/最大へ混ぜず、測定できた脱落量だけ別に返す。
        double FailedVertexDroppedWeight = 0;
        bool bHasFailedVertexDroppedWeight = false;
        bool bInfluenceScanComplete = false;
        // animationの正常prefix。Bake処理前の拒否はbCubicScanStarted=falseで区別する。
        uint64_t TotalAnimationChannelCount = 0;
        uint64_t ProcessedAnimationChannelCount = 0;
        uint64_t BakedCubicChannelCount = 0;
        uint64_t BakedCubicTranslationChannelCount = 0;
        uint64_t BakedCubicRotationChannelCount = 0;
        uint64_t BakedCubicScaleChannelCount = 0;
        uint64_t CubicInputKeyCount = 0;
        uint64_t CubicOutputKeyCount = 0;
        double MaximumCubicTranslationErrorMeters = 0;
        double MaximumCubicRotationErrorRadians = 0;
        double MaximumCubicScaleError = 0;
        uint64_t FailedAnimationChannelIndex = std::numeric_limits<uint64_t>::max();
        uint32_t FailedCubicBakeStatus = 0;
        bool bHasCubicBakeFailure = false;
        bool bCubicScanStarted = false;
        bool bCubicScanComplete = false;
        // 検証済みの除去対象数。scan完了前は未測定であり、ゼロ件と報告しない。
        // weight数は初期値配列の要素数、animationはweight channelの数。
        uint64_t DroppedMorphTargetCount = 0;
        uint64_t DroppedMorphMeshWeightCount = 0;
        uint64_t DroppedMorphNodeWeightCount = 0;
        uint64_t DroppedMorphAnimationChannelCount = 0;
        bool bMorphScanComplete = false;
    };
} // namespace NorvesLib::Core::Skeletal
