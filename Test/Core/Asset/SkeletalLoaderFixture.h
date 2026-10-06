#pragma once
// CookedSkeletalAssetTestの独立0.1/0.2 literalを共有依存なしで再利用する。旧試験は変更しない。
#include "Asset/CookedSkeletalFormat.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <bit>
#include <cstring>
namespace NorvesLib::Tests::SkeletalLoaderFixture
{
    namespace Asset = Core::Asset;
    namespace Container = Core::Container;
    using ByteArray = Container::VariableArray<uint8_t>;
    namespace GoldenWire
    {
        constexpr size_t HeaderSize = 256;
        constexpr size_t VertexOffset = 256;
        constexpr size_t VertexSize = 192;
        constexpr size_t IndexOffset = 448;
        constexpr size_t IndexSize = 12;
        constexpr size_t JointOffset = 464;
        constexpr size_t JointSize = 160;
        constexpr size_t ClipOffset = 624;
        constexpr size_t ClipSize = 32;
        constexpr size_t ChannelOffset = 656;
        constexpr size_t ChannelSize = 64;
        constexpr size_t SampleOffset = 720;
        constexpr size_t SampleSize = 128;
        constexpr size_t StringOffset = 848;
        constexpr size_t StringSize = 13;
        constexpr size_t FileSize = 861;
        constexpr uint8_t Magic[8] = {'N', 'V', 'S', 'K', 'E', 'L', 'v', '0'};
    } // namespace GoldenWire

    void WriteLe16(ByteArray& bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(ByteArray& bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(ByteArray& bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    void WriteFloat(ByteArray& bytes, size_t offset, float value)
    {
        WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
    }

    void WriteVertex(ByteArray& bytes, size_t offset, float x, float y, float u, float v, uint32_t joint0,
                     uint32_t joint1, float weight0, float weight1)
    {
        WriteFloat(bytes, offset + 0, x);
        WriteFloat(bytes, offset + 4, y);
        WriteFloat(bytes, offset + 8, 0.0f);
        WriteFloat(bytes, offset + 12, 0.0f);
        WriteFloat(bytes, offset + 16, 0.0f);
        WriteFloat(bytes, offset + 20, 1.0f);
        WriteFloat(bytes, offset + 24, u);
        WriteFloat(bytes, offset + 28, v);
        WriteLe32(bytes, offset + 32, joint0);
        WriteLe32(bytes, offset + 36, joint1);
        WriteLe32(bytes, offset + 40, 0);
        WriteLe32(bytes, offset + 44, 0);
        WriteFloat(bytes, offset + 48, weight0);
        WriteFloat(bytes, offset + 52, weight1);
        WriteFloat(bytes, offset + 56, 0.0f);
        WriteFloat(bytes, offset + 60, 0.0f);
    }

    void WriteMatrix(ByteArray& bytes, size_t offset, float inverseY)
    {
        WriteFloat(bytes, offset + 0 * sizeof(float), 1.0f);
        WriteFloat(bytes, offset + 5 * sizeof(float), 1.0f);
        WriteFloat(bytes, offset + 10 * sizeof(float), 1.0f);
        WriteFloat(bytes, offset + 13 * sizeof(float), inverseY);
        WriteFloat(bytes, offset + 15 * sizeof(float), 1.0f);
    }

    void WriteSample(ByteArray& bytes, size_t offset, float time, float x, float y, float z, float w)
    {
        WriteFloat(bytes, offset + 0, time);
        WriteFloat(bytes, offset + 4, x);
        WriteFloat(bytes, offset + 8, y);
        WriteFloat(bytes, offset + 12, z);
        WriteFloat(bytes, offset + 16, w);
    }

    ByteArray BuildGoldenSkeletal()
    {
        ByteArray bytes(GoldenWire::FileSize, 0);
        std::memcpy(bytes.data(), GoldenWire::Magic, sizeof(GoldenWire::Magic));
        WriteLe32(bytes, 8, static_cast<uint32_t>(GoldenWire::HeaderSize));
        WriteLe16(bytes, 12, 0);
        WriteLe16(bytes, 14, 1);
        WriteLe32(bytes, 16, 0x01020304u);
        WriteLe32(bytes, 20, 64);
        WriteLe32(bytes, 24, 80);
        WriteLe32(bytes, 28, 32);
        WriteLe32(bytes, 32, 32);
        WriteLe32(bytes, 36, 32);
        WriteLe64(bytes, 40, GoldenWire::FileSize);
        WriteLe64(bytes, 48, GoldenWire::VertexOffset);
        WriteLe64(bytes, 56, GoldenWire::VertexSize);
        WriteLe64(bytes, 64, GoldenWire::IndexOffset);
        WriteLe64(bytes, 72, GoldenWire::IndexSize);
        WriteLe64(bytes, 80, GoldenWire::JointOffset);
        WriteLe64(bytes, 88, GoldenWire::JointSize);
        WriteLe64(bytes, 96, GoldenWire::ClipOffset);
        WriteLe64(bytes, 104, GoldenWire::ClipSize);
        WriteLe64(bytes, 112, GoldenWire::ChannelOffset);
        WriteLe64(bytes, 120, GoldenWire::ChannelSize);
        WriteLe64(bytes, 128, GoldenWire::SampleOffset);
        WriteLe64(bytes, 136, GoldenWire::SampleSize);
        WriteLe64(bytes, 144, GoldenWire::StringOffset);
        WriteLe64(bytes, 152, GoldenWire::StringSize);
        WriteLe32(bytes, 168, 3);
        WriteLe32(bytes, 172, 3);
        WriteLe32(bytes, 176, 2);
        WriteLe32(bytes, 180, 1);
        WriteLe32(bytes, 184, 2);
        WriteLe32(bytes, 188, 4);
        WriteMatrix(bytes, 192, 0.0f);
        WriteFloat(bytes, 192 + 12 * sizeof(float), 5.0f);

        WriteVertex(bytes, 256, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1, 0.75f, 0.25f);
        WriteVertex(bytes, 320, 1.0f, 0.0f, 1.0f, 0.0f, 0, 1, 0.5f, 0.5f);
        WriteVertex(bytes, 384, 0.0f, 1.0f, 0.0f, 1.0f, 1, 0, 1.0f, 0.0f);
        WriteLe32(bytes, 448, 0);
        WriteLe32(bytes, 452, 2);
        WriteLe32(bytes, 456, 1);

        WriteLe32(bytes, 464, 0xffffffffu);
        WriteLe32(bytes, 468, 0);
        WriteLe32(bytes, 472, 4);
        WriteMatrix(bytes, 480, 0.0f);
        WriteLe32(bytes, 544, 0);
        WriteLe32(bytes, 548, 4);
        WriteLe32(bytes, 552, 5);
        WriteMatrix(bytes, 560, -1.0f);

        WriteLe64(bytes, 624, 9);
        WriteLe32(bytes, 632, 4);
        WriteFloat(bytes, 636, 2.0f);
        WriteLe32(bytes, 640, 0);
        WriteLe32(bytes, 644, 2);
        WriteLe32(bytes, 656, 1);
        WriteLe32(bytes, 660, 0);
        WriteLe32(bytes, 664, 0);
        WriteLe32(bytes, 668, 0);
        WriteLe32(bytes, 672, 2);
        WriteLe32(bytes, 688, 0);
        WriteLe32(bytes, 692, 1);
        WriteLe32(bytes, 696, 1);
        WriteLe32(bytes, 700, 2);
        WriteLe32(bytes, 704, 2);
        WriteSample(bytes, 720, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        WriteSample(bytes, 752, 2.0f, 0.0f, 3.0f, 0.0f, 0.0f);
        WriteSample(bytes, 784, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        WriteSample(bytes, 816, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f);

        constexpr char names[] = "RootChildWave";
        std::memcpy(bytes.data() + 848, names, sizeof(names) - 1);
        WriteLe64(bytes, 160,
                  Asset::ComputeCookedSkeletalV01Hash(bytes.data() + 192, bytes.data() + 256, bytes.size() - 256));
        return bytes;
    }

    void RecomputeSkeletalHash(ByteArray& bytes)
    {
        const uint64_t hash =
            bytes[14] == 0 ? Asset::ComputeCookedSkeletalPayloadHash(bytes.data() + 256, bytes.size() - 256)
            : bytes[14] == 2
                ? Asset::ComputeCookedSkeletalV02Hash(bytes.data() + 192, bytes.data() + 256, bytes.data() + 320,
                                                      bytes.size() - 320)
                : Asset::ComputeCookedSkeletalV01Hash(bytes.data() + 192, bytes.data() + 256, bytes.size() - 256);
        WriteLe64(bytes, 160, hash);
    }

    Asset::AssetBlob MakeBlob(const ByteArray& bytes)
    {
        return Asset::AssetBlob::CopyBytes(Container::Span<const uint8_t>(bytes.data(), bytes.size()), "memory.nvskel");
    }

    // writerと独立した0.2配置。2clipは同joint/pathを別々に所有する。
    ByteArray BuildGoldenSkeletalV02()
    {
        const auto old = BuildGoldenSkeletal();
        ByteArray bytes(1436, 0);
        std::memcpy(bytes.data(), old.data(), 256);
        WriteLe32(bytes, 8, 320);
        WriteLe16(bytes, 14, 2);
        WriteLe64(bytes, 40, 1436);
        const uint64_t offsets[] = {320, 512, 544, 704, 768, 896, 1408};
        const uint64_t sizes[] = {192, 24, 160, 64, 128, 256, 28};
        for (size_t index = 0; index < 7; ++index)
        {
            WriteLe64(bytes, 48 + index * 16, offsets[index]);
            WriteLe64(bytes, 56 + index * 16, sizes[index]);
        }
        WriteLe32(bytes, 172, 6);
        WriteLe32(bytes, 180, 2);
        WriteLe32(bytes, 184, 4);
        WriteLe32(bytes, 188, 8);
        WriteLe32(bytes, 256, 64);
        WriteLe32(bytes, 260, 64);
        WriteLe64(bytes, 264, 1152);
        WriteLe64(bytes, 272, 128);
        WriteLe64(bytes, 280, 1280);
        WriteLe64(bytes, 288, 128);
        WriteLe32(bytes, 296, 2);
        WriteLe32(bytes, 300, 2);
        std::memcpy(bytes.data() + 320, old.data() + 256, 192);
        std::memcpy(bytes.data() + 512, old.data() + 448, 12);
        std::memcpy(bytes.data() + 524, old.data() + 448, 12);
        std::memcpy(bytes.data() + 544, old.data() + 464, 160);
        std::memcpy(bytes.data() + 704, old.data() + 624, 32);
        std::memcpy(bytes.data() + 736, old.data() + 624, 32);
        WriteLe64(bytes, 736, 13);
        WriteLe32(bytes, 744, 7);
        WriteFloat(bytes, 748, 1.0f);
        WriteLe32(bytes, 752, 2);
        std::memcpy(bytes.data() + 768, old.data() + 656, 64);
        std::memcpy(bytes.data() + 832, old.data() + 656, 64);
        WriteLe32(bytes, 844, 4);
        WriteLe32(bytes, 876, 6);
        std::memcpy(bytes.data() + 896, old.data() + 720, 128);
        std::memcpy(bytes.data() + 1024, old.data() + 720, 128);
        WriteFloat(bytes, 1056, 1.0f);
        WriteFloat(bytes, 1120, 1.0f);
        for (size_t index = 0; index < 2; ++index)
        {
            const size_t record = 1152 + index * 64;
            WriteLe32(bytes, record, static_cast<uint32_t>(index * 3));
            WriteLe32(bytes, record + 4, 3);
            WriteLe32(bytes, record + 12, 3);
            WriteLe32(bytes, record + 16, static_cast<uint32_t>(index));
            WriteLe32(bytes, record + 20, static_cast<uint32_t>(index));
            WriteFloat(bytes, record + 24, 0.5f);
            WriteFloat(bytes, record + 36, 2.0f);
            WriteLe64(bytes, 1280 + index * 64, 20 + index * 4);
            WriteLe32(bytes, 1288 + index * 64, 4);
        }
        const uint8_t names[] = {'R',  'o',  'o',  't',  'C',  'h',  'i', 'l', 'd', 'W', 'a', 'v', 'e', 0xe9,
                                 0xaa, 0xa8, 0xf0, 0x9f, 0x90, 0xba, 'B', 'o', 'd', 'y', 'E', 'y', 'e', 's'};
        std::memcpy(bytes.data() + 1408, names, sizeof(names));
        RecomputeSkeletalHash(bytes);
        return bytes;
    }

    ByteArray BuildThreeClips()
    {
        const auto old = BuildGoldenSkeletalV02();
        ByteArray bytes(1664, 0);
        std::memcpy(bytes.data(), old.data(), 320);
        const uint64_t offsets[] = {320, 512, 544, 704, 800, 992, 1632};
        const uint64_t sizes[] = {192, 24, 160, 96, 192, 384, 32};
        for (size_t i = 0; i < 7; ++i)
        {
            WriteLe64(bytes, 48 + i * 16, offsets[i]);
            WriteLe64(bytes, 56 + i * 16, sizes[i]);
        }
        WriteLe64(bytes, 40, bytes.size());
        WriteLe32(bytes, 180, 3);
        WriteLe32(bytes, 184, 6);
        WriteLe32(bytes, 188, 12);
        WriteLe64(bytes, 264, 1376);
        WriteLe64(bytes, 280, 1504);
        std::memcpy(bytes.data() + 320, old.data() + 320, 384);
        for (size_t i = 0; i < 3; ++i)
        {
            const size_t source = i == 1 ? 1 : 0;
            std::memcpy(bytes.data() + 704 + i * 32, old.data() + 704 + source * 32, 32);
            WriteLe32(bytes, 720 + i * 32, static_cast<uint32_t>(i * 2));
            std::memcpy(bytes.data() + 800 + i * 64, old.data() + 768 + source * 64, 64);
            std::memcpy(bytes.data() + 992 + i * 128, old.data() + 896 + source * 128, 128);
        }
        for (size_t i = 0; i < 6; ++i)
        {
            WriteLe32(bytes, 812 + i * 32, static_cast<uint32_t>(i * 2));
        }
        WriteLe64(bytes, 768, 28);
        WriteLe32(bytes, 776, 4);
        std::memcpy(bytes.data() + 1376, old.data() + 1152, 256);
        std::memcpy(bytes.data() + 1632, old.data() + 1408, 28);
        std::memcpy(bytes.data() + 1660, "Idle", 4);
        RecomputeSkeletalHash(bytes);
        return bytes;
    }

    ByteArray Package(const ByteArray& payload, uint32_t type, uint64_t& hash)
    {
        namespace W = Asset::AssetPackageFormatV1;
        ByteArray bytes(168 + payload.size(), 0);
        std::memcpy(bytes.data(), W::Magic, W::MagicSize);
        WriteLe32(bytes, W::HeaderOffset::HeaderSize, W::HeaderSize);
        WriteLe16(bytes, W::HeaderOffset::VersionMajor, W::VersionMajor);
        WriteLe32(bytes, W::HeaderOffset::EndianMarker, W::EndianMarker);
        WriteLe32(bytes, W::HeaderOffset::EntryRecordSize, W::EntryRecordSize);
        WriteLe64(bytes, W::HeaderOffset::PackageSize, bytes.size());
        WriteLe32(bytes, W::HeaderOffset::EntryCount, 1);
        WriteLe64(bytes, W::HeaderOffset::EntryTableOffset, 96);
        WriteLe64(bytes, W::HeaderOffset::EntryTableSize, 64);
        WriteLe64(bytes, W::HeaderOffset::NameTableOffset, 160);
        WriteLe64(bytes, W::HeaderOffset::NameTableSize, 1);
        WriteLe64(bytes, W::HeaderOffset::BlobDataOffset, 168);
        WriteLe32(bytes, W::HeaderOffset::Alignment, 8);
        hash = Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size());
        WriteLe64(bytes, 96 + W::EntryOffset::NameOffset, 160);
        WriteLe32(bytes, 96 + W::EntryOffset::NameSize, 1);
        WriteLe32(bytes, 96 + W::EntryOffset::Type, type);
        WriteLe64(bytes, 96 + W::EntryOffset::DataOffset, 168);
        WriteLe64(bytes, 96 + W::EntryOffset::StoredSize, payload.size());
        WriteLe64(bytes, 96 + W::EntryOffset::UncompressedSize, payload.size());
        WriteLe64(bytes, 96 + W::EntryOffset::PayloadHash, hash);
        bytes[160] = 'a';
        if (!payload.empty())
        {
            std::memcpy(bytes.data() + 168, payload.data(), payload.size());
        }
        return bytes;
    }
    Container::String CoreText(Container::AnsiStringView text)
    {
        using Char = Container::String::value_type;
        const Container::Span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        const auto size = Asset::MeasureSkeletalNameDecoding<Char>(2, bytes);
        if (!size.Succeeded())
        {
            return {};
        }
        Container::VariableArray<Char> units(size.CodeUnitCount + 1, Char{});
        if (!Asset::DecodeSkeletalWireName<Char>(2, bytes, {units.data(), size.CodeUnitCount}).Succeeded())
        {
            return {};
        }
        return Container::String(units.data());
    }
} // namespace NorvesLib::Tests::SkeletalLoaderFixture
