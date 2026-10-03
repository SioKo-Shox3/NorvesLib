#pragma once

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    enum class SkeletalInfluencePolicy : uint8_t { Strict=0, ReduceToFour=1 };
    inline constexpr double DefaultWarnDroppedWeight = 0.01;
    inline constexpr double DefaultFailDroppedWeight = 0.25;
    struct SkeletalGltfDecodeOptions
    {
        SkeletalInfluencePolicy InfluencePolicy = SkeletalInfluencePolicy::Strict;
        double WarnDroppedWeight = DefaultWarnDroppedWeight;
        double FailDroppedWeight = DefaultFailDroppedWeight;
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
    };
} // namespace NorvesLib::Core::Skeletal
