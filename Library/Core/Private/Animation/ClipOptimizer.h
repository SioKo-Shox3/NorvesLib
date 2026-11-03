#pragma once

#include "Animation/SkeletalRestPose.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    struct ClipOptimizeSettings
    {
        double MaxVertexErrorMeters = 0.002;
        double MaxJointErrorMeters = 0.001;
        double MaxRotationErrorDegrees = 0.1;
    };

    struct ClipOptimizeRig
    {
        Container::Span<const Skeletal::SkeletalJoint> Joints;
        Container::Span<const Skeletal::SkeletalRestTransform> DefaultLocal;
        Container::Span<const Skeletal::SkeletalVertex> Vertices;
    };

    struct ClipJointErrorReport
    {
        uint32_t JointIndex = 0;
        double MaxPositionErrorMeters = 0;
        double MaxRotationErrorDegrees = 0;
    };

    struct ClipCompressionClipReport
    {
        Container::String Name;
        double DurationSeconds = 0;
        uint64_t KeysBefore = 0, KeysAfter = 0, BytesBefore = 0, BytesAfter = 0;
        uint32_t ChannelsBefore = 0, ChannelsAfter = 0, RemovedChannels = 0, ConstantChannels = 0;
        double MaxVertexErrorMeters = 0;
        uint32_t Attempts = 0;
        double ToleranceScale = 1;
        Container::VariableArray<ClipJointErrorReport> Joints;
    };

    struct ClipCompressionReport
    {
        ClipOptimizeSettings Settings;
        Container::VariableArray<ClipCompressionClipReport> Clips;
        bool bSucceeded = false;
        Container::String FailureReason;
    };

    // 旧形式の評価経路と同じ行列分解で、単位メッシュ変換時の既定ローカルを求める。
    [[nodiscard]] bool BuildLegacyClipOptimizeDefaultLocal(
        Container::Span<const Skeletal::SkeletalJoint> joints,
        Container::VariableArray<Skeletal::SkeletalRestTransform>& out);

    // 成功時だけ出力を置き換え、成否にかかわらず報告を更新する。
    [[nodiscard]] bool OptimizeSkeletalClips(const ClipOptimizeRig& rig,
                                             Container::Span<const Skeletal::SkeletalAnimationClip> clips,
                                             Container::VariableArray<Skeletal::SkeletalAnimationClip>& out,
                                             const ClipOptimizeSettings& settings, ClipCompressionReport& report);

    [[nodiscard]] bool WriteClipCompressionReportJson(const ClipCompressionReport& report,
                                                       Container::String& outJson);
} // namespace NorvesLib::Core::Animation
