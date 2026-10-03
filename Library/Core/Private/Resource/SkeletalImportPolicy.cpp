#include "Resource/SkeletalImportPolicy.h"
#include "Asset/AssetPackageFormat.h"
#include <bit>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    bool IsValidSkeletalGltfDecodeOptions(const SkeletalGltfDecodeOptions& options) noexcept
    {
        if (!std::isfinite(options.WarnDroppedWeight) || !std::isfinite(options.FailDroppedWeight) ||
            options.WarnDroppedWeight<0 || options.WarnDroppedWeight>options.FailDroppedWeight || options.FailDroppedWeight>1)
        {
            return false;
        }
        if (options.InfluencePolicy==SkeletalInfluencePolicy::Strict)
        {
            return options.WarnDroppedWeight==DefaultWarnDroppedWeight && options.FailDroppedWeight==DefaultFailDroppedWeight;
        }
        return options.InfluencePolicy==SkeletalInfluencePolicy::ReduceToFour;
    }
    CanonicalSkeletalImportPolicy EncodeSkeletalImportPolicy(const SkeletalGltfDecodeOptions& options) noexcept
    {
        if (!IsValidSkeletalGltfDecodeOptions(options))
        {
            return {};
        }
        CanonicalSkeletalImportPolicy result;
        result.bValid=true;
        if (options.InfluencePolicy==SkeletalInfluencePolicy::Strict)
        {
            return result;
        }
        static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);
        result.Size=sizeof(result.Bytes);
        result.Bytes[0]='S'; result.Bytes[1]='R'; result.Bytes[2]='E'; result.Bytes[3]='D';
        result.Bytes[4]=1; // schema version 1、残り3byteはzero初期化済み。
        result.Bytes[8]=static_cast<uint8_t>(options.InfluencePolicy);
        const double values[]={options.WarnDroppedWeight,options.FailDroppedWeight};
        for (size_t field=0;field<2;++field)
        {
            const uint64_t bits=std::bit_cast<uint64_t>(values[field]==0 ? 0.0 : values[field]);
            for (size_t byte=0;byte<8;++byte)
            {
                result.Bytes[9+field*8+byte]=static_cast<uint8_t>(bits>>(byte*8));
            }
        }
        return result;
    }
    SkeletalImportPolicyHash AppendSkeletalImportPolicyHash(uint64_t state,
        const SkeletalGltfDecodeOptions& options,uint32_t algorithmVersion) noexcept
    {
        const auto encoded=EncodeSkeletalImportPolicy(options);
        if (!encoded.bValid)
        {
            return {};
        }
        if (encoded.Size==0)
        {
            return {true,state};
        }
        uint64_t hash=state;
        const auto append=[&](uint8_t byte)
        {
            hash=(hash^byte)*Asset::AssetPackageFormatV1::Fnv1a64Prime;
        };
        for (size_t byte=0;byte<8;++byte)
        {
            append(static_cast<uint8_t>(static_cast<uint64_t>(encoded.Size)>>(byte*8)));
        }
        for (size_t byte=0;byte<encoded.Size;++byte)
        {
            append(encoded.Bytes[byte]);
        }
        for (size_t byte=0;byte<4;++byte)
        {
            append(static_cast<uint8_t>(algorithmVersion>>(byte*8)));
        }
        return {true,hash};
    }
} // namespace NorvesLib::Core::Skeletal
