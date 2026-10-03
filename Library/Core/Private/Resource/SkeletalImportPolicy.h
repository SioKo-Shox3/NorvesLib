#pragma once

#include "Resource/SkeletalImportOptions.h"
#include <cstddef>

namespace NorvesLib::Core::Skeletal
{
    inline constexpr uint32_t SkeletalReductionAlgorithmVersion = 1;
    struct CanonicalSkeletalImportPolicy
    {
        uint8_t Bytes[25] = {};
        size_t Size = 0;
        bool bValid = false;
    };
    struct SkeletalImportPolicyHash
    {
        bool bValid = false;
        uint64_t Value = 0;
    };
    // Strictでは未使用閾値を指定させず、defaultだけを既存経路として受理する。
    [[nodiscard]] bool IsValidSkeletalGltfDecodeOptions(const SkeletalGltfDecodeOptions& options) noexcept;
    // Strict既定はvalid/Size0。ReduceはSRED+schemaLE32+policyU8+warn/fail binary64LE。
    [[nodiscard]] CanonicalSkeletalImportPolicy EncodeSkeletalImportPolicy(const SkeletalGltfDecodeOptions& options) noexcept;
    // StrictはalgorithmVersionにも触れず旧state不変。Reduceはu64len+bytes+u32algorithmを連結する。
    [[nodiscard]] SkeletalImportPolicyHash AppendSkeletalImportPolicyHash(uint64_t state,
        const SkeletalGltfDecodeOptions& options, uint32_t algorithmVersion=SkeletalReductionAlgorithmVersion) noexcept;
} // namespace NorvesLib::Core::Skeletal
