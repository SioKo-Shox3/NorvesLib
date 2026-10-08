#pragma once
// sample時刻を独立に計算する。累積float加算や暗黙のfps推測はしない。
#include "SkeletalFloatEnvironment.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Animation
{
    enum class SkeletalBvhClipTimeMode : uint8_t
    {
        Unspecified,
        HeaderFrameTime,
        OverrideFps
    };
    enum class SkeletalBvhClipTimeStatus : uint8_t
    {
        Success,
        InvalidInput,
        UnsupportedFloatEnvironment,
        UnrepresentableTime,
        NonIncreasingTime
    };
    struct SkeletalBvhClipTimeResult
    {
        SkeletalBvhClipTimeStatus Status = SkeletalBvhClipTimeStatus::InvalidInput;
        double IntervalSeconds = 0;
        double ExactSeconds = 0;
        float StoredSeconds = 0;
        double RoundingErrorSeconds = 0;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalBvhClipTimeStatus::Success;
        }
    };
    inline SkeletalBvhClipTimeResult ComputeSkeletalBvhClipTime(SkeletalBvhClipTimeMode mode, double headerInterval,
                                                                double sourceFps, uint32_t frameIndex,
                                                                bool bHasPrevious, float previous) noexcept
    {
        using Status = SkeletalBvhClipTimeStatus;
        if (!Detail::SupportedSkeletalFloatEnvironment() || !std::numeric_limits<float>::is_iec559 ||
            std::numeric_limits<float>::digits != 24)
        {
            return {Status::UnsupportedFloatEnvironment};
        }
        if ((frameIndex == 0) == bHasPrevious || (bHasPrevious && (!std::isfinite(previous) || previous < 0)))
        {
            return {Status::InvalidInput};
        }
        double interval = 0, time = 0;
        if (mode == SkeletalBvhClipTimeMode::HeaderFrameTime)
        {
            if (!std::isfinite(headerInterval) || headerInterval <= 0)
            {
                return {Status::InvalidInput};
            }
            interval = headerInterval;
            time = static_cast<double>(frameIndex) * headerInterval;
        }
        else if (mode == SkeletalBvhClipTimeMode::OverrideFps)
        {
            if (!std::isfinite(sourceFps) || sourceFps <= 0)
            {
                return {Status::InvalidInput};
            }
            interval = 1.0 / sourceFps;
            time = static_cast<double>(frameIndex) / sourceFps;
        }
        else
        {
            return {Status::InvalidInput};
        }
        if (!std::isfinite(interval) || interval <= 0 || !std::isfinite(time) || time < 0 ||
            time > static_cast<double>(std::numeric_limits<float>::max()))
        {
            return {Status::UnrepresentableTime};
        }
        const float stored = static_cast<float>(time);
        if (!std::isfinite(stored) || (time > 0 && stored == 0))
        {
            return {Status::UnrepresentableTime};
        }
        if (bHasPrevious && stored <= previous)
        {
            return {Status::NonIncreasingTime};
        }
        return {Status::Success, interval, time, stored, std::abs(static_cast<double>(stored) - time)};
    }
    namespace Detail
    {
        inline bool BvhClipCheckedAdd(uint64_t a, uint64_t b, uint64_t& out) noexcept
        {
            if (b > UINT64_MAX - a)
            {
                return false;
            }
            out = a + b;
            return true;
        }
        inline bool BvhClipCheckedMultiply(uint64_t a, uint64_t b, uint64_t& out) noexcept
        {
            if (a != 0 && b > UINT64_MAX / a)
            {
                return false;
            }
            out = a * b;
            return true;
        }
        // 生成と最終key検証でnativeを2回通す。二乗走査込みのchargeでありwall-clock保証ではない。
        inline bool ComputeSkeletalBvhClipWork(uint64_t source, uint64_t target, uint64_t mappings, uint64_t frames,
                                               uint64_t rawValues, uint64_t nameBytes, uint64_t& out) noexcept
        {
            uint64_t s2, t2, m2, linear, perFrame, frameWork, rawWork, namesWork, total;
            if (!BvhClipCheckedMultiply(source, source, s2) || !BvhClipCheckedMultiply(target, target, t2) ||
                !BvhClipCheckedMultiply(t2, 3, t2) || !BvhClipCheckedMultiply(mappings, mappings, m2) ||
                !BvhClipCheckedAdd(source, target, linear) || !BvhClipCheckedAdd(linear, mappings, linear) ||
                !BvhClipCheckedMultiply(linear, 64, linear) || !BvhClipCheckedAdd(s2, t2, perFrame) ||
                !BvhClipCheckedAdd(perFrame, m2, perFrame) || !BvhClipCheckedAdd(perFrame, linear, perFrame) ||
                !BvhClipCheckedMultiply(perFrame, frames, frameWork) ||
                !BvhClipCheckedMultiply(frameWork, 2, frameWork) || !BvhClipCheckedMultiply(rawValues, 2, rawWork) ||
                !BvhClipCheckedMultiply(nameBytes, 16, namesWork) || !BvhClipCheckedAdd(frameWork, rawWork, total) ||
                !BvhClipCheckedAdd(total, namesWork, total))
            {
                return false;
            }
            out = total;
            return true;
        }
    } // namespace Detail
} // namespace NorvesLib::Core::Animation
