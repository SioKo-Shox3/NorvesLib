#include "Resource/SkeletalImportPolicy.h"
#include "Asset/AssetPackageFormat.h"
#include <bit>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    bool IsValidSkeletalGltfDecodeOptions(const SkeletalGltfDecodeOptions& options) noexcept
    {
        if (options.MorphPolicy != SkeletalMorphPolicy::Reject && options.MorphPolicy != SkeletalMorphPolicy::Drop)
        {
            return false;
        }
        if (!std::isfinite(options.WarnDroppedWeight) || !std::isfinite(options.FailDroppedWeight) ||
            options.WarnDroppedWeight < 0 || options.WarnDroppedWeight > options.FailDroppedWeight || options.FailDroppedWeight > 1)
        {
            return false;
        }
        if (options.InfluencePolicy == SkeletalInfluencePolicy::Strict)
        {
            if (options.WarnDroppedWeight != DefaultWarnDroppedWeight || options.FailDroppedWeight != DefaultFailDroppedWeight)
            {
                return false;
            }
        }
        else if (options.InfluencePolicy != SkeletalInfluencePolicy::ReduceToFour)
        {
            return false;
        }
        if (options.CubicSplinePolicy == SkeletalCubicSplinePolicy::Reject)
        {
            return options.CubicTranslationToleranceMeters == DefaultCubicTranslationToleranceMeters &&
                options.CubicRotationToleranceRadians == DefaultCubicRotationToleranceRadians &&
                options.CubicScaleTolerance == DefaultCubicScaleTolerance && options.CubicMaximumDepth == DefaultCubicMaximumDepth &&
                options.CubicMaximumSamplesPerChannel == DefaultCubicMaximumSamplesPerChannel &&
                options.CubicMaximumSamplesPerAsset == DefaultCubicMaximumSamplesPerAsset;
        }
        if (options.CubicSplinePolicy != SkeletalCubicSplinePolicy::Bake)
        {
            return false;
        }
        return std::isfinite(options.CubicTranslationToleranceMeters) && options.CubicTranslationToleranceMeters > CubicVectorNumericErrorFloor &&
            std::isfinite(options.CubicRotationToleranceRadians) && options.CubicRotationToleranceRadians > CubicRotationNumericErrorBudget &&
            options.CubicRotationToleranceRadians <= 3.14159265358979323846 &&
            std::isfinite(options.CubicScaleTolerance) && options.CubicScaleTolerance > CubicVectorNumericErrorFloor &&
            options.CubicMaximumDepth <= MaximumCubicSubdivisionDepth &&
            options.CubicMaximumSamplesPerChannel >= 2 && options.CubicMaximumSamplesPerChannel <= MaximumCubicSamplesPerChannel &&
            options.CubicMaximumSamplesPerAsset >= 2 && options.CubicMaximumSamplesPerAsset <= MaximumCubicSamplesPerAsset;
    }

    CanonicalSkeletalImportPolicy EncodeSkeletalImportPolicy(const SkeletalGltfDecodeOptions& options) noexcept
    {
        if (!IsValidSkeletalGltfDecodeOptions(options))
        {
            return {};
        }
        // Dropだけ既存canonicalを包む。内側をRejectへ戻すので再帰は一段に限る。
        if (options.MorphPolicy == SkeletalMorphPolicy::Drop)
        {
            auto innerOptions = options;
            innerOptions.MorphPolicy = SkeletalMorphPolicy::Reject;
            const auto inner = EncodeSkeletalImportPolicy(innerOptions);
            if (!inner.bValid || inner.Size > 66)
            {
                return {};
            }
            CanonicalSkeletalImportPolicy result;
            result.bValid = true;
            result.Size = 17 + inner.Size;
            result.Bytes[0] = 'S';
            result.Bytes[1] = 'M';
            result.Bytes[2] = 'D';
            result.Bytes[3] = 'P';
            result.Bytes[4] = 1; // schema u32 LE。残りはゼロ初期化済み。
            result.Bytes[8] = static_cast<uint8_t>(options.MorphPolicy);
            for (size_t byte = 0; byte < 4; ++byte)
            {
                result.Bytes[9 + byte] = static_cast<uint8_t>(static_cast<uint32_t>(inner.Size) >> (byte * 8));
                result.Bytes[13 + inner.Size + byte] = static_cast<uint8_t>(SkeletalMorphDropAlgorithmVersion >> (byte * 8));
            }
            for (size_t byte = 0; byte < inner.Size; ++byte)
            {
                result.Bytes[13 + byte] = inner.Bytes[byte];
            }
            return result;
        }
        CanonicalSkeletalImportPolicy result;
        result.bValid = true;
        const bool bBake = options.CubicSplinePolicy == SkeletalCubicSplinePolicy::Bake;
        if (!bBake && options.InfluencePolicy == SkeletalInfluencePolicy::Strict)
        {
            return result;
        }
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        const auto writeU32 = [&](size_t offset, uint32_t value)
        {
            for (size_t byte = 0; byte < 4; ++byte)
            {
                result.Bytes[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
            }
        };
        const auto writeDouble = [&](size_t offset, double value)
        {
            const uint64_t bits = std::bit_cast<uint64_t>(value == 0 ? 0.0 : value);
            for (size_t byte = 0; byte < 8; ++byte)
            {
                result.Bytes[offset + byte] = static_cast<uint8_t>(bits >> (byte * 8));
            }
        };
        result.Size = bBake ? 66 : 25;
        result.Bytes[0] = 'S';
        result.Bytes[1] = bBake ? 'C' : 'R';
        result.Bytes[2] = bBake ? 'B' : 'E';
        result.Bytes[3] = bBake ? 'K' : 'D';
        writeU32(4, 1);
        result.Bytes[8] = static_cast<uint8_t>(options.InfluencePolicy);
        writeDouble(9, options.WarnDroppedWeight);
        writeDouble(17, options.FailDroppedWeight);
        if (bBake)
        {
            result.Bytes[25] = static_cast<uint8_t>(options.CubicSplinePolicy);
            writeDouble(26, options.CubicTranslationToleranceMeters);
            writeDouble(34, options.CubicRotationToleranceRadians);
            writeDouble(42, options.CubicScaleTolerance);
            writeU32(50, options.CubicMaximumDepth);
            writeU32(54, options.CubicMaximumSamplesPerChannel);
            writeU32(58, options.CubicMaximumSamplesPerAsset);
            writeU32(62, SkeletalCubicBakeAlgorithmVersion);
        }
        return result;
    }

    SkeletalImportPolicyHash AppendSkeletalImportPolicyHash(uint64_t state,
        const SkeletalGltfDecodeOptions& options, uint32_t algorithmVersion) noexcept
    {
        const auto encoded = EncodeSkeletalImportPolicy(options);
        if (!encoded.bValid)
        {
            return {};
        }
        if (encoded.Size == 0)
        {
            return {true, state};
        }
        uint64_t hash = state;
        const auto append = [&](uint8_t byte)
        {
            hash = (hash ^ byte) * Asset::AssetPackageFormatV1::Fnv1a64Prime;
        };
        for (size_t byte = 0; byte < 8; ++byte)
        {
            append(static_cast<uint8_t>(static_cast<uint64_t>(encoded.Size) >> (byte * 8)));
        }
        for (size_t byte = 0; byte < encoded.Size; ++byte)
        {
            append(encoded.Bytes[byte]);
        }
        for (size_t byte = 0; byte < 4; ++byte)
        {
            append(static_cast<uint8_t>(algorithmVersion >> (byte * 8)));
        }
        return {true, hash};
    }
} // namespace NorvesLib::Core::Skeletal
