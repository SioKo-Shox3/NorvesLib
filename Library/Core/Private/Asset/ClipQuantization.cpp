#include "ClipQuantization.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace NorvesLib::Core::Skeletal
{
    namespace
    {
        constexpr double InvSqrt2 = 0.70710678118654752440;
        constexpr double RadiansToDegrees = 57.295779513082320877;

        void NormalizeRotation(const SkeletalValue& value, double out[4]) noexcept
        {
            out[0] = value.X;
            out[1] = value.Y;
            out[2] = value.Z;
            out[3] = value.W;
            const double inverseLength = 1.0 / std::sqrt(
                out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
            for (double& component : Container::Span<double>(out, 4))
            {
                component *= inverseLength;
            }
        }

        uint32_t CanonicalizeRotation(const SkeletalValue& value, double out[4]) noexcept
        {
            NormalizeRotation(value, out);
            uint32_t largest = 0;
            for (uint32_t i = 1; i < 4; ++i)
            {
                if (std::fabs(out[i]) > std::fabs(out[largest]))
                {
                    largest = i;
                }
            }
            if (out[largest] < 0.0)
            {
                for (double& component : Container::Span<double>(out, 4))
                {
                    component = -component;
                }
            }
            return largest;
        }

        double QuantizationPosition(double component, uint32_t maximum) noexcept
        {
            return std::clamp((component / InvSqrt2 + 1.0) * 0.5 * maximum, 0.0, double(maximum));
        }

        SkeletalValue DecodeSmallestThree(uint64_t bits, uint32_t largest, uint32_t codeBits) noexcept
        {
            const uint32_t maximum = (1u << codeBits) - 1;
            double components[4]{};
            double sum = 0.0;
            uint32_t stored = 0;
            for (uint32_t i = 0; i < 4; ++i)
            {
                if (i == largest)
                {
                    continue;
                }
                const auto code = uint32_t((bits >> ((2 - stored) * codeBits)) & maximum);
                components[i] = (double(code) / maximum * 2.0 - 1.0) * InvSqrt2;
                sum += components[i] * components[i];
                ++stored;
            }
            components[largest] = std::sqrt(std::max(0.0, 1.0 - sum));
            const double inverseLength = 1.0 / std::sqrt(sum + components[largest] * components[largest]);
            return {float(components[0] * inverseLength), float(components[1] * inverseLength),
                    float(components[2] * inverseLength), float(components[3] * inverseLength)};
        }

        bool SameValueBits(const SkeletalValue& a, const SkeletalValue& b) noexcept
        {
            return std::bit_cast<uint32_t>(a.X) == std::bit_cast<uint32_t>(b.X) &&
                   std::bit_cast<uint32_t>(a.Y) == std::bit_cast<uint32_t>(b.Y) &&
                   std::bit_cast<uint32_t>(a.Z) == std::bit_cast<uint32_t>(b.Z) &&
                   std::bit_cast<uint32_t>(a.W) == std::bit_cast<uint32_t>(b.W);
        }
    } // 無名名前空間

    uint32_t EncodeRotationSmallest3_32(const SkeletalValue& rotation) noexcept
    {
        double normalized[4];
        const uint32_t largest = CanonicalizeRotation(rotation, normalized);
        uint32_t lower[3], upper[3];
        uint32_t stored = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (i == largest)
            {
                continue;
            }
            const double position = QuantizationPosition(normalized[i], 1023);
            lower[stored] = uint32_t(std::floor(position));
            upper[stored] = uint32_t(std::ceil(position));
            ++stored;
        }

        double bestDot = -1.0;
        uint32_t bestBits = 0;
        // 下限だけの組から始め、同値の候補では先に試した組を残す。
        for (uint32_t candidate = 0; candidate < 8; ++candidate)
        {
            uint32_t bits = largest << 30;
            for (uint32_t i = 0; i < 3; ++i)
            {
                const uint32_t code = (candidate & (1u << i)) ? upper[i] : lower[i];
                bits |= code << ((2 - i) * 10);
            }
            double decoded[4];
            NormalizeRotation(DecodeRotationSmallest3_32(bits), decoded);
            double dot = 0.0;
            for (uint32_t i = 0; i < 4; ++i)
            {
                dot += normalized[i] * decoded[i];
            }
            dot = std::fabs(dot);
            if (dot > bestDot)
            {
                bestDot = dot;
                bestBits = bits;
            }
        }
        return bestBits;
    }

    SkeletalValue DecodeRotationSmallest3_32(uint32_t bits) noexcept
    {
        return DecodeSmallestThree(bits, bits >> 30, 10);
    }

    uint64_t EncodeRotationSmallest3_48(const SkeletalValue& rotation) noexcept
    {
        double normalized[4];
        const uint32_t largest = CanonicalizeRotation(rotation, normalized);
        uint64_t bits = uint64_t(largest) << 46;
        uint32_t stored = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (i == largest)
            {
                continue;
            }
            const auto code = uint64_t(std::round(QuantizationPosition(normalized[i], 32767)));
            bits |= code << ((2 - stored) * 15);
            ++stored;
        }
        return bits;
    }

    bool IsValidRotationSmallest3_48(uint64_t bits) noexcept
    {
        return (bits & ((uint64_t{1} << 45) | 0xffff000000000000ull)) == 0;
    }

    SkeletalValue DecodeRotationSmallest3_48(uint64_t bits) noexcept
    {
        return DecodeSmallestThree(bits, uint32_t((bits >> 46) & 3), 15);
    }

    double RotationAngleDifferenceDegrees(const SkeletalValue& a, const SkeletalValue& b) noexcept
    {
        double first[4], second[4];
        NormalizeRotation(a, first);
        NormalizeRotation(b, second);
        double dot = 0.0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            dot += first[i] * second[i];
        }
        return 2.0 * std::acos(std::min(1.0, std::fabs(dot))) * RadiansToDegrees;
    }

    TranslationQuantRange ComputeTranslationQuantRange(Container::Span<const SkeletalAnimationSample> samples) noexcept
    {
        TranslationQuantRange range;
        if (samples.empty())
        {
            return range;
        }
        const auto& first = samples[0].Value;
        range.Min[0] = first.X;
        range.Min[1] = first.Y;
        range.Min[2] = first.Z;
        float maximum[3] = {first.X, first.Y, first.Z};
        for (const auto& sample : samples)
        {
            const float components[3] = {sample.Value.X, sample.Value.Y, sample.Value.Z};
            for (uint32_t i = 0; i < 3; ++i)
            {
                range.Min[i] = std::min(range.Min[i], components[i]);
                maximum[i] = std::max(maximum[i], components[i]);
            }
        }
        for (uint32_t i = 0; i < 3; ++i)
        {
            range.Extent[i] = maximum[i] - range.Min[i];
        }
        return range;
    }

    void EncodeTranslation16(const SkeletalValue& value, const TranslationQuantRange& range, uint16_t out[3]) noexcept
    {
        const float components[3] = {value.X, value.Y, value.Z};
        for (uint32_t i = 0; i < 3; ++i)
        {
            out[i] = range.Extent[i] == 0.0f ? 0 : uint16_t(std::clamp(
                std::round((double(components[i]) - range.Min[i]) / range.Extent[i] * 65535.0), 0.0, 65535.0));
        }
    }

    SkeletalValue DecodeTranslation16(const uint16_t codes[3], const TranslationQuantRange& range) noexcept
    {
        float components[3];
        for (uint32_t i = 0; i < 3; ++i)
        {
            components[i] = float(double(range.Min[i]) + double(range.Extent[i]) * (codes[i] / 65535.0));
        }
        return {components[0], components[1], components[2], 0.0f};
    }

    ClipChannelEncoding ChooseClipChannelEncoding(const SkeletalAnimationChannel& channel) noexcept
    {
        if (channel.Samples.empty())
        {
            return ClipChannelEncoding::F32;
        }
        bool bConstant = true;
        for (const auto& sample : channel.Samples)
        {
            if (!SameValueBits(channel.Samples[0].Value, sample.Value))
            {
                bConstant = false;
                break;
            }
        }
        if (bConstant)
        {
            return ClipChannelEncoding::Const;
        }
        if (channel.Path == SkeletalAnimationPath::Rotation)
        {
            for (const auto& sample : channel.Samples)
            {
                if (RotationAngleDifferenceDegrees(sample.Value,
                    DecodeRotationSmallest3_32(EncodeRotationSmallest3_32(sample.Value))) > 0.1)
                {
                    return ClipChannelEncoding::RotSmallest3_48;
                }
            }
            return ClipChannelEncoding::RotSmallest3_32;
        }
        return channel.Path == SkeletalAnimationPath::Translation ? ClipChannelEncoding::TransQuant16 :
                                                                  ClipChannelEncoding::F32;
    }
} // 名前空間 NorvesLib::Core::Skeletal
