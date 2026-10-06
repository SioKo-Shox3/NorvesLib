#pragma once
// 既存Samplerの区間算術と、確定済みkeyのO(1)評価を共有する。
#include "SkeletalSamplingMath.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core::Animation::Detail
{
    inline Skeletal::SkeletalValue SampleSkeletalChannelInterval(const Skeletal::SkeletalAnimationChannel& channel,
                                                                 const Skeletal::SkeletalAnimationSample& previous,
                                                                 const Skeletal::SkeletalAnimationSample& next,
                                                                 float timeSeconds)
    {
        if (channel.Interpolation == Skeletal::SkeletalAnimationInterpolation::Step)
        {
            return timeSeconds == next.TimeSeconds ? next.Value : previous.Value;
        }
        const float alpha = ComputeLinearAlpha(previous.TimeSeconds, next.TimeSeconds, timeSeconds);
        if (channel.Path == Skeletal::SkeletalAnimationPath::Rotation)
        {
            const Math::Quaternion rotation =
                Slerp(Math::Quaternion(previous.Value.X, previous.Value.Y, previous.Value.Z, previous.Value.W),
                      Math::Quaternion(next.Value.X, next.Value.Y, next.Value.Z, next.Value.W), alpha);
            return {rotation.x, rotation.y, rotation.z, rotation.w};
        }
        return {previous.Value.X + (next.Value.X - previous.Value.X) * alpha,
                previous.Value.Y + (next.Value.Y - previous.Value.Y) * alpha,
                previous.Value.Z + (next.Value.Z - previous.Value.Z) * alpha,
                previous.Value.W + (next.Value.W - previous.Value.W) * alpha};
    }
    // channelは時刻の厳密増加を検証済み。clipのclampがkey時刻を変えないことはcallerの責務。
    // 内部のLINEAR keyもraw値へ近道せず、実Samplerと同じSlerp/正規化/EPSILON分岐を通す。
    inline bool SampleSkeletalChannelAtKnownKey(const Skeletal::SkeletalAnimationChannel& channel, size_t index,
                                                Skeletal::SkeletalValue& out)
    {
        const auto& samples = channel.Samples;
        if (index >= samples.size())
        {
            return false;
        }
        const Skeletal::SkeletalValue candidate =
            index == 0 || index + 1 == samples.size()
                ? samples[index].Value
                : SampleSkeletalChannelInterval(channel, samples[index - 1], samples[index],
                                                samples[index].TimeSeconds);
        out = candidate;
        return true;
    }
} // namespace NorvesLib::Core::Animation::Detail
