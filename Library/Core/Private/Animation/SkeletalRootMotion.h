#pragma once
#include "Resource/SkeletalGltfData.h"
#include <cmath>
namespace NorvesLib::Core::Skeletal
{
    [[nodiscard]] inline bool IsValidSkeletalRootMotion(const SkeletalAnimationClip& clip, size_t joints)
    {
        if (clip.RootMotion.empty())
        {
            return clip.RootMotionJoint == UINT32_MAX;
        }
        if (clip.RootMotionJoint >= joints || clip.RootMotion.size() < 2 || clip.DurationSeconds <= 0 ||
            clip.RootMotion.front().TimeSeconds != 0 || clip.RootMotion.back().TimeSeconds != clip.DurationSeconds ||
            clip.RootMotion.front().TranslationX != 0 || clip.RootMotion.front().TranslationZ != 0 ||
            clip.RootMotion.front().YawRadians != 0)
        {
            return false;
        }
        float previous = -1;
        for (const auto& sample : clip.RootMotion)
        {
            if (!std::isfinite(sample.TimeSeconds) || sample.TimeSeconds <= previous ||
                sample.TimeSeconds > clip.DurationSeconds || !std::isfinite(sample.TranslationX) ||
                !std::isfinite(sample.TranslationZ) || !std::isfinite(sample.YawRadians))
            {
                return false;
            }
            previous = sample.TimeSeconds;
        }
        return true;
    }
} // namespace NorvesLib::Core::Skeletal
