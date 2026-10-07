#include "Animation/RigRootFrame.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/RigV1Types.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace NorvesLib::Core::Skeletal
{
    bool IsSupportedRigImportProfile(RigImportProfile profile) noexcept
    {
        return profile == RigImportProfile::DirectTrs128 || IsStaticRootFrameProfile(profile);
    }
    bool SameRigRootFrame(const RigRootFrame& a, const RigRootFrame& b) noexcept
    {
        for (size_t i = 0; i < 16; ++i)
        {
            if (std::bit_cast<uint32_t>(a[i]) != std::bit_cast<uint32_t>(b[i]))
            {
                return false;
            }
        }
        return true;
    }
    void CanonicalizeRigRootFrameZero(RigRootFrame& frame) noexcept
    {
        for (auto& f : frame)
        {
            if (f == 0)
            {
                f = 0;
            }
        }
    }
    uint64_t RigRootFrameHash(const RigRootFrame& frame) noexcept
    {
        Container::FixedArray<uint8_t, 64> bytes{};
        for (size_t i = 0; i < 16; ++i)
        {
            const auto value = std::bit_cast<uint32_t>(frame[i]);
            for (unsigned n = 0; n < 4; ++n)
            {
                bytes[i * 4 + n] = uint8_t(value >> (n * 8));
            }
        }
        return RigBytesHash({bytes.data(), bytes.size()});
    }
    double RigRootFrameMaximumDifference(const RigRootFrame& a, const RigRootFrame& b) noexcept
    {
        double result = 0;
        for (size_t i = 0; i < 16; ++i)
        {
            result = std::max(result, std::abs(double(a[i]) - double(b[i])));
        }
        return result;
    }
    bool IsValidRigRootFrame(const RigRootFrame& frame, RigImportProfile profile) noexcept
    {
        if (!IsSupportedRigImportProfile(profile))
        {
            return false;
        }
        if (profile == RigImportProfile::DirectTrs128)
        {
            return SameRigRootFrame(frame, IdentityRigRootFrame());
        }
        for (float f : frame)
        {
            if (!std::isfinite(f) || (f == 0 && std::signbit(f)))
            {
                return false;
            }
        }
        if (frame[3] != 0 || frame[7] != 0 || frame[11] != 0 || frame[15] != 1)
        {
            return false;
        }
        double lengths[3]{};
        for (size_t row = 0; row < 3; ++row)
        {
            for (size_t col = 0; col < 3; ++col)
            {
                lengths[row] += double(frame[row * 4 + col]) * frame[row * 4 + col];
            }
        }
        const double average = (lengths[0] + lengths[1] + lengths[2]) / 3;
        if (!std::isfinite(average) || average <= 0)
        {
            return false;
        }
        constexpr double RoundingEnvelope = 1e-5;
        for (size_t i = 0; i < 3; ++i)
        {
            if (std::abs(lengths[i] - average) / average > RoundingEnvelope)
            {
                return false;
            }
            for (size_t j = i + 1; j < 3; ++j)
            {
                double dot = 0;
                for (size_t k = 0; k < 3; ++k)
                {
                    dot += double(frame[i * 4 + k]) * frame[j * 4 + k];
                }
                if (std::abs(dot) / average > RoundingEnvelope)
                {
                    return false;
                }
            }
        }
        const double determinant = double(frame[0]) * (double(frame[5]) * frame[10] - double(frame[6]) * frame[9]) -
                                   double(frame[1]) * (double(frame[4]) * frame[10] - double(frame[6]) * frame[8]) +
                                   double(frame[2]) * (double(frame[4]) * frame[9] - double(frame[5]) * frame[8]);
        if (!std::isfinite(determinant) || determinant <= 0)
        {
            return false;
        }
        const Math::Matrix4x4 matrix(frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7],
                                     frame[8], frame[9], frame[10], frame[11], frame[12], frame[13], frame[14],
                                     frame[15]);
        Math::Matrix4x4 inverse;
        return Animation::Detail::TryInverseMatrix(matrix, inverse);
    }
} // namespace NorvesLib::Core::Skeletal
