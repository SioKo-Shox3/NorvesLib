#include "Asset/CookedSkeletalWireValidation.h"
#include "Resource/SkeletalLimits.h"
#include <limits>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        uint32_t ReadU32(const uint8_t* bytes)
        {
            return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
        }
        uint64_t ReadU64(const uint8_t* bytes)
        {
            return uint64_t(ReadU32(bytes)) | (uint64_t(ReadU32(bytes + 4)) << 32);
        }
        bool ValidSection(const CookedSkeletalWireSection& section, uint64_t fileSize, uint64_t headerSize)
        {
            return section.Offset >= headerSize && section.Offset % 16 == 0 && section.Offset <= fileSize && section.Size <= fileSize - section.Offset;
        }
    }

    CookedSkeletalWireStatus ResolveCookedSkeletalWireProfile(uint16_t major, uint16_t minor,
        uint32_t headerSize, CookedSkeletalWireProfile& outProfile) noexcept
    {
        if (major != 0 || minor > 2)
        {
            return CookedSkeletalWireStatus::UnsupportedVersion;
        }
        const uint32_t expected = minor == 2 ? CookedSkeletalFormatV02::HeaderSize : CookedSkeletalFormatV0::HeaderSize;
        if (headerSize != expected)
        {
            return CookedSkeletalWireStatus::InvalidHeaderSize;
        }
        outProfile = {minor, expected, minor == 2, minor == 2};
        return CookedSkeletalWireStatus::Success;
    }

    CookedSkeletalWireStatus ValidateCookedSkeletalWireCounts(uint16_t minor, const CookedSkeletalWireCounts& counts) noexcept
    {
        if (minor > 2)
        {
            return CookedSkeletalWireStatus::UnsupportedVersion;
        }
        const uint64_t all[] = {counts.Vertices, counts.Indices, counts.Joints, counts.Clips, counts.Channels, counts.Samples};
        for (const auto value : all)
        {
            if (value == 0 || value > UINT32_MAX)
            {
                return CookedSkeletalWireStatus::InvalidCount;
            }
        }
        if (counts.Indices % 3 != 0 || counts.Joints > Skeletal::LegacyMaximumJointCount ||
            counts.Clips > counts.Channels || counts.Channels > counts.Samples || (minor < 2 && counts.Clips != 1))
        {
            return CookedSkeletalWireStatus::InvalidCount;
        }
        if (minor == 2)
        {
            if (counts.Submeshes == 0 || counts.Submeshes > Skeletal::MaximumSubmeshCount ||
                counts.MaterialSlots == 0 || counts.MaterialSlots > Skeletal::MaximumMaterialSlotCount || counts.Submeshes > counts.Indices / 3)
            {
                return CookedSkeletalWireStatus::InvalidCount;
            }
        }
        else if (counts.Submeshes != 0 || counts.MaterialSlots != 0)
        {
            return CookedSkeletalWireStatus::InvalidCount;
        }
        return CookedSkeletalWireStatus::Success;
    }

    CookedSkeletalWireStatus ValidateCookedSkeletalWireSections(uint16_t minor, uint64_t fileSize,
        Container::Span<const CookedSkeletalWireSection> sections, const CookedSkeletalWireCounts& counts) noexcept
    {
        const auto status = ValidateCookedSkeletalWireCounts(minor, counts);
        if (status != CookedSkeletalWireStatus::Success)
        {
            return status;
        }
        const uint64_t headerSize = minor == 2 ? CookedSkeletalFormatV02::HeaderSize : CookedSkeletalFormatV0::HeaderSize;
        const size_t sectionCount = minor == 2 ? 9 : 7;
        if (!sections.data() || sections.size() != sectionCount)
        {
            return CookedSkeletalWireStatus::InvalidInput;
        }
        if (fileSize < headerSize)
        {
            return CookedSkeletalWireStatus::InvalidSectionRange;
        }
        // 各数量は先行検査済みu32なので、u64積のoverflowは起きない。
        const uint64_t sizes[] = {counts.Vertices * 64, counts.Indices * 4, counts.Joints * 80,
            counts.Clips * 32, counts.Channels * 32, counts.Samples * 32, counts.Submeshes * 64, counts.MaterialSlots * 64};
        uint64_t expectedOffset = headerSize;
        for (size_t index = 0; index < sectionCount; ++index)
        {
            const auto& section = sections[index];
            if (section.Offset != expectedOffset || !ValidSection(section, fileSize, headerSize) ||
                (index + 1 < sectionCount && section.Size != sizes[index]))
            {
                return CookedSkeletalWireStatus::InvalidSectionRange;
            }
            const uint64_t end = section.Offset + section.Size; // ValidSectionでfileSize内と確認済み。
            if (index + 1 == sectionCount)
            {
                if (end != fileSize)
                {
                    return CookedSkeletalWireStatus::InvalidSectionRange;
                }
            }
            else
            {
                if (end > UINT64_MAX - 15)
                {
                    return CookedSkeletalWireStatus::InvalidSectionRange;
                }
                expectedOffset = (end + 15) & ~uint64_t{15};
            }
        }
        return CookedSkeletalWireStatus::Success;
    }

    CookedSkeletalWireStatus ReadCookedSkeletalV02Extension(Container::Span<const uint8_t> extension,
        uint64_t fileSize, CookedSkeletalV02Extension& outExtension) noexcept
    {
        if (!extension.data() || extension.size() != CookedSkeletalFormatV02::ExtensionSize)
        {
            return CookedSkeletalWireStatus::InvalidInput;
        }
        const auto* bytes = extension.data();
        if (ReadU32(bytes) != CookedSkeletalFormatV02::SubmeshRecordSize || ReadU32(bytes + 4) != CookedSkeletalFormatV02::MaterialSlotRecordSize)
        {
            return CookedSkeletalWireStatus::InvalidRecordSize;
        }
        if (ReadU64(bytes + 48) != 0 || ReadU32(bytes + 56) != 0 || ReadU32(bytes + 60) != 0)
        {
            return CookedSkeletalWireStatus::UnsupportedExtraVertex;
        }
        CookedSkeletalV02Extension result;
        result.Submeshes = {ReadU64(bytes + 8), ReadU64(bytes + 16)};
        result.MaterialSlots = {ReadU64(bytes + 24), ReadU64(bytes + 32)};
        result.SubmeshCount = ReadU32(bytes + 40);
        result.MaterialSlotCount = ReadU32(bytes + 44);
        if (result.SubmeshCount == 0 || result.SubmeshCount > Skeletal::MaximumSubmeshCount ||
            result.MaterialSlotCount == 0 || result.MaterialSlotCount > Skeletal::MaximumMaterialSlotCount)
        {
            return CookedSkeletalWireStatus::InvalidCount;
        }
        if (!ValidSection(result.Submeshes, fileSize, CookedSkeletalFormatV02::HeaderSize) ||
            !ValidSection(result.MaterialSlots, fileSize, CookedSkeletalFormatV02::HeaderSize) ||
            result.Submeshes.Size != uint64_t(result.SubmeshCount) * 64 ||
            result.MaterialSlots.Size != uint64_t(result.MaterialSlotCount) * 64 ||
            result.MaterialSlots.Offset != result.Submeshes.Offset + result.Submeshes.Size)
        {
            return CookedSkeletalWireStatus::InvalidSectionRange;
        }
        outExtension = result;
        return CookedSkeletalWireStatus::Success;
    }

    CookedSkeletalWireStatus TryComputeCookedSkeletalWireHash(uint16_t major, uint16_t minor, uint32_t headerSize,
        Container::Span<const uint8_t> blob, uint64_t& outHash) noexcept
    {
        CookedSkeletalWireProfile profile;
        const auto status = ResolveCookedSkeletalWireProfile(major, minor, headerSize, profile);
        if (status != CookedSkeletalWireStatus::Success)
        {
            return status;
        }
        if (!blob.data() || blob.size() < profile.HeaderSize)
        {
            return CookedSkeletalWireStatus::InvalidInput;
        }
        const uint8_t* payload = blob.data() + profile.HeaderSize;
        const size_t size = blob.size() - profile.HeaderSize;
        if (minor == 0)
        {
            outHash = ComputeCookedSkeletalPayloadHash(payload, size);
        }
        else if (minor == 1)
        {
            outHash = ComputeCookedSkeletalV01Hash(blob.data() + 192, payload, size);
        }
        else
        {
            outHash = ComputeCookedSkeletalV02Hash(blob.data() + 192, blob.data() + 256, payload, size);
        }
        return CookedSkeletalWireStatus::Success;
    }
} // namespace NorvesLib::Core::Asset
