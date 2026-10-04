#pragma once

#include "Asset/AssetPackageFormat.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    namespace CookedSkeletalFormatV0
    {
        inline constexpr uint8_t Magic[8] = {'N', 'V', 'S', 'K', 'E', 'L', 'v', '0'};
        inline constexpr size_t MagicSize = sizeof(Magic);
        inline constexpr uint16_t VersionMajor = 0;
        inline constexpr uint16_t LegacyVersionMinor = 0;
        inline constexpr uint16_t VersionMinor = 1;
        inline constexpr uint32_t EndianMarker = 0x01020304u;
        inline constexpr size_t HeaderSize = 256;
        inline constexpr size_t VertexRecordSize = 64;
        inline constexpr size_t JointRecordSize = 80;
        inline constexpr size_t ClipRecordSize = 32;
        inline constexpr size_t ChannelRecordSize = 32;
        inline constexpr size_t SampleRecordSize = 32;
        inline constexpr size_t SectionAlignment = 16;
        inline constexpr AssetPackageFourCC EntryType = MakeAssetPackageFourCC('S', 'k', 'l', '0');
        inline constexpr const char* FormatName = "nvskel.v0.skinned.pnujiw.u32";

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;
            inline constexpr size_t HeaderSize = 8;
            inline constexpr size_t VersionMajor = 12;
            inline constexpr size_t VersionMinor = 14;
            inline constexpr size_t EndianMarker = 16;
            inline constexpr size_t VertexRecordSize = 20;
            inline constexpr size_t JointRecordSize = 24;
            inline constexpr size_t ClipRecordSize = 28;
            inline constexpr size_t ChannelRecordSize = 32;
            inline constexpr size_t SampleRecordSize = 36;
            inline constexpr size_t FileSize = 40;
            inline constexpr size_t VertexOffset = 48;
            inline constexpr size_t VertexSize = 56;
            inline constexpr size_t IndexOffset = 64;
            inline constexpr size_t IndexSize = 72;
            inline constexpr size_t JointOffset = 80;
            inline constexpr size_t JointSize = 88;
            inline constexpr size_t ClipOffset = 96;
            inline constexpr size_t ClipSize = 104;
            inline constexpr size_t ChannelOffset = 112;
            inline constexpr size_t ChannelSize = 120;
            inline constexpr size_t SampleOffset = 128;
            inline constexpr size_t SampleSize = 136;
            inline constexpr size_t StringOffset = 144;
            inline constexpr size_t StringSize = 152;
            inline constexpr size_t PayloadHash = 160;
            inline constexpr size_t VertexCount = 168;
            inline constexpr size_t IndexCount = 172;
            inline constexpr size_t JointCount = 176;
            inline constexpr size_t ClipCount = 180;
            inline constexpr size_t ChannelCount = 184;
            inline constexpr size_t SampleCount = 188;
            inline constexpr size_t MeshNodeGlobalTransform = 192;
        } // namespace HeaderOffset

        namespace VertexOffset
        {
            inline constexpr size_t Position = 0;
            inline constexpr size_t Normal = 12;
            inline constexpr size_t TexCoord = 24;
            inline constexpr size_t JointIndices = 32;
            inline constexpr size_t JointWeights = 48;
        } // namespace VertexOffset

        namespace JointOffset
        {
            inline constexpr size_t ParentIndex = 0;
            inline constexpr size_t NameOffset = 4;
            inline constexpr size_t NameSize = 8;
            inline constexpr size_t InverseBindMatrix = 16;
        } // namespace JointOffset

        namespace ClipOffset
        {
            inline constexpr size_t NameOffset = 0;
            inline constexpr size_t NameSize = 8;
            inline constexpr size_t Duration = 12;
            inline constexpr size_t ChannelOffset = 16;
            inline constexpr size_t ChannelCount = 20;
        } // namespace ClipOffset

        namespace ChannelOffset
        {
            inline constexpr size_t JointIndex = 0;
            inline constexpr size_t Path = 4;
            inline constexpr size_t Interpolation = 8;
            inline constexpr size_t SampleOffset = 12;
            inline constexpr size_t SampleCount = 16;
        } // namespace ChannelOffset

        namespace SampleOffset
        {
            inline constexpr size_t Time = 0;
            inline constexpr size_t Value = 4;
        } // namespace SampleOffset
    } // namespace CookedSkeletalFormatV0

    [[nodiscard]] constexpr uint64_t ComputeCookedSkeletalPayloadHash(const uint8_t* data, size_t size) noexcept
    {
        return ComputeAssetPackagePayloadHash(data, size);
    }

    [[nodiscard]] constexpr uint64_t ComputeCookedSkeletalV01Hash(const uint8_t* meshNodeGlobalTransform,
                                                                  const uint8_t* payload,
                                                                  size_t payloadSize) noexcept
    {
        uint64_t hash = AssetPackageFormatV1::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < sizeof(float) * 16; ++index)
        {
            hash ^= static_cast<uint64_t>(meshNodeGlobalTransform[index]);
            hash *= AssetPackageFormatV1::Fnv1a64Prime;
        }
        for (size_t index = 0; index < payloadSize; ++index)
        {
            hash ^= static_cast<uint64_t>(payload[index]);
            hash *= AssetPackageFormatV1::Fnv1a64Prime;
        }
        return hash;
    }

    // 0.2はGR32の表とGR82 Stage Aの複数clipを同じwireへ載せる。
    // CookedSkeletalFormatV0は旧0.1定義として固定。現writer/cacheはV02::VersionMinorを使う。
    namespace CookedSkeletalFormatV02
    {
        inline constexpr uint16_t VersionMajor = 0;
        inline constexpr uint16_t VersionMinor = 2;
        inline constexpr size_t HeaderSize = 320;
        inline constexpr size_t ExtensionSize = 64;
        inline constexpr size_t SubmeshRecordSize = 64;
        inline constexpr size_t MaterialSlotRecordSize = 64;
        namespace HeaderOffset
        {
            inline constexpr size_t SubmeshRecordSize = 256;
            inline constexpr size_t MaterialSlotRecordSize = 260;
            inline constexpr size_t SubmeshOffset = 264;
            inline constexpr size_t SubmeshSize = 272;
            inline constexpr size_t MaterialSlotOffset = 280;
            inline constexpr size_t MaterialSlotSize = 288;
            inline constexpr size_t SubmeshCount = 296;
            inline constexpr size_t MaterialSlotCount = 300;
            inline constexpr size_t ExtraVertexOffset = 304;
            inline constexpr size_t ExtraVertexRecordSize = 312;
            inline constexpr size_t ExtraVertexFlags = 316;
        }
        namespace SubmeshOffset
        {
            inline constexpr size_t IndexOffset = 0;
            inline constexpr size_t IndexCount = 4;
            inline constexpr size_t VertexOffset = 8;
            inline constexpr size_t VertexCount = 12;
            inline constexpr size_t MaterialIndex = 16;
            inline constexpr size_t Flags = 20;
            inline constexpr size_t BoundsCenter = 24;
            inline constexpr size_t BoundsRadius = 36;
            inline constexpr size_t Reserved = 40;
        }
        namespace MaterialSlotOffset
        {
            inline constexpr size_t NameOffset = 0;
            inline constexpr size_t NameLength = 8;
            inline constexpr size_t NameReserved = 12;
            inline constexpr size_t Reserved = 16;
        }
        inline constexpr uint32_t SubmeshFlagNoShadow = 1;
    }

    // 呼出前にtransform64B/extension64B/payloadの長さを検査する。raw hash単体は範囲を検証しない。
    [[nodiscard]] constexpr uint64_t ComputeCookedSkeletalV02Hash(const uint8_t* meshNodeGlobalTransform,
        const uint8_t* extension, const uint8_t* payload, size_t payloadSize) noexcept
    {
        uint64_t hash = AssetPackageFormatV1::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < 64; ++index)
        {
            hash = (hash ^ meshNodeGlobalTransform[index]) * AssetPackageFormatV1::Fnv1a64Prime;
        }
        for (size_t index = 0; index < CookedSkeletalFormatV02::ExtensionSize; ++index)
        {
            hash = (hash ^ extension[index]) * AssetPackageFormatV1::Fnv1a64Prime;
        }
        for (size_t index = 0; index < payloadSize; ++index)
        {
            hash = (hash ^ payload[index]) * AssetPackageFormatV1::Fnv1a64Prime;
        }
        return hash;
    }
} // namespace NorvesLib::Core::Asset
