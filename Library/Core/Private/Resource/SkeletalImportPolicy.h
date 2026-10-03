#pragma once

#include "Resource/SkeletalImportOptions.h"
#include <cstddef>

namespace NorvesLib::Core::Skeletal
{
    inline constexpr uint32_t SkeletalReductionAlgorithmVersion = 1;
    inline constexpr uint32_t SkeletalCubicBakeAlgorithmVersion = 1;
    struct CanonicalSkeletalImportPolicy
    {
        uint8_t Bytes[66] = {};
        size_t Size = 0;
        bool bValid = false;
    };
    struct SkeletalImportPolicyHash
    {
        bool bValid = false;
        uint64_t Value = 0;
    };
    // 無効なpolicy/数値/資源上限と、Reject/Strictで使われない非既定指定を拒否する。
    [[nodiscard]] bool IsValidSkeletalGltfDecodeOptions(const SkeletalGltfDecodeOptions& options) noexcept;
    // Bake無しはStrict Size0 / Reduce SRED 25byteを維持。BakeはSCBK 66byte。
    [[nodiscard]] CanonicalSkeletalImportPolicy EncodeSkeletalImportPolicy(const SkeletalGltfDecodeOptions& options) noexcept;
    // Size0は旧state不変。それ以外はu64len+bytes+u32algorithmを連結する。
    [[nodiscard]] SkeletalImportPolicyHash AppendSkeletalImportPolicyHash(uint64_t state,
        const SkeletalGltfDecodeOptions& options, uint32_t algorithmVersion=SkeletalReductionAlgorithmVersion) noexcept;
} // namespace NorvesLib::Core::Skeletal
