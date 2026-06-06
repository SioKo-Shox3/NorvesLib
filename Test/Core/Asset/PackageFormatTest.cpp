#include "Asset/AssetPackageFormat.h"
#include "FileStream/Package.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <tchar.h>
#include <vector>

using NorvesLib::Core::Asset::AssetBlob;
using NorvesLib::Core::Asset::AssetPackageCompression;
using NorvesLib::Core::Asset::AssetPackageFourCC;
using NorvesLib::Core::Asset::MakeAssetPackageFourCC;
using NorvesLib::Core::Container::Span;
using NorvesLib::FileStream::Package;
using NorvesLib::FileStream::PackageEntry;
using NorvesLib::FileStream::PackageFormat;
using namespace NorvesLib::Core::Asset::AssetPackageFormatV1;

namespace
{
    struct TestPackageEntry
    {
        std::string Name;
        AssetPackageFourCC Type = 0;
        std::vector<uint8_t> Payload;
        AssetPackageCompression Compression = AssetPackageCompression::None;
    };

    size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    uint64_t ComputeFnv1a64(const uint8_t *data, size_t size)
    {
        uint64_t hash = Fnv1a64OffsetBasis;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(data[index]);
            hash *= Fnv1a64Prime;
        }
        return hash;
    }

    uint64_t ComputeFnv1a64(const std::vector<uint8_t> &bytes)
    {
        return ComputeFnv1a64(bytes.data(), bytes.size());
    }

    void WriteLe16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    std::vector<uint8_t> BuildPackage(const std::vector<TestPackageEntry> &entries)
    {
        const size_t alignment = MinimumAlignment;
        const size_t entryTableOffset = HeaderSize;
        const size_t entryTableSize = entries.size() * EntryRecordSize;
        const size_t nameTableOffset = AlignUp(entryTableOffset + entryTableSize, alignment);

        size_t nameTableSize = 0;
        for (const TestPackageEntry &entry : entries)
        {
            nameTableSize += entry.Name.size();
        }

        const size_t blobDataOffset = AlignUp(nameTableOffset + nameTableSize, alignment);
        size_t packageSize = blobDataOffset;
        for (const TestPackageEntry &entry : entries)
        {
            packageSize = AlignUp(packageSize, alignment);
            packageSize += entry.Payload.size();
        }

        std::vector<uint8_t> bytes(packageSize, 0);
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::EntryRecordSize, static_cast<uint32_t>(EntryRecordSize));
        WriteLe64(bytes, HeaderOffset::PackageSize, static_cast<uint64_t>(packageSize));
        WriteLe32(bytes, HeaderOffset::EntryCount, static_cast<uint32_t>(entries.size()));
        WriteLe32(bytes, HeaderOffset::Flags, 0);
        WriteLe64(bytes, HeaderOffset::EntryTableOffset, static_cast<uint64_t>(entryTableOffset));
        WriteLe64(bytes, HeaderOffset::EntryTableSize, static_cast<uint64_t>(entryTableSize));
        WriteLe64(bytes, HeaderOffset::NameTableOffset, static_cast<uint64_t>(nameTableOffset));
        WriteLe64(bytes, HeaderOffset::NameTableSize, static_cast<uint64_t>(nameTableSize));
        WriteLe64(bytes, HeaderOffset::BlobDataOffset, static_cast<uint64_t>(blobDataOffset));
        WriteLe32(bytes, HeaderOffset::Alignment, static_cast<uint32_t>(alignment));
        WriteLe32(bytes, HeaderOffset::Reserved0, 0);
        WriteLe64(bytes, HeaderOffset::Reserved1, 0);

        size_t nameCursor = nameTableOffset;
        size_t dataCursor = blobDataOffset;
        for (size_t index = 0; index < entries.size(); ++index)
        {
            const TestPackageEntry &entry = entries[index];
            const size_t recordOffset = entryTableOffset + index * EntryRecordSize;
            const size_t nameOffset = nameCursor;
            for (const char character : entry.Name)
            {
                bytes[nameCursor++] = static_cast<uint8_t>(character);
            }

            dataCursor = AlignUp(dataCursor, alignment);
            const size_t dataOffset = dataCursor;
            for (const uint8_t value : entry.Payload)
            {
                bytes[dataCursor++] = value;
            }

            WriteLe64(bytes, recordOffset + EntryOffset::NameOffset, static_cast<uint64_t>(nameOffset));
            WriteLe32(bytes, recordOffset + EntryOffset::NameSize, static_cast<uint32_t>(entry.Name.size()));
            WriteLe32(bytes, recordOffset + EntryOffset::Type, entry.Type);
            WriteLe32(bytes, recordOffset + EntryOffset::Compression, static_cast<uint32_t>(entry.Compression));
            WriteLe32(bytes, recordOffset + EntryOffset::Flags, 0);
            WriteLe64(bytes, recordOffset + EntryOffset::DataOffset, static_cast<uint64_t>(dataOffset));
            WriteLe64(bytes, recordOffset + EntryOffset::StoredSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes, recordOffset + EntryOffset::UncompressedSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes, recordOffset + EntryOffset::PayloadHash, ComputeFnv1a64(entry.Payload));
            WriteLe64(bytes, recordOffset + EntryOffset::Reserved0, 0);
        }

        return bytes;
    }

    bool LoadBytes(Package &package, const std::vector<uint8_t> &bytes, const TCHAR *source = _T("memory.nvpkg"))
    {
        return package.LoadFromMemory(Span<const uint8_t>(bytes.data(), bytes.size()), source);
    }

    size_t FirstEntryRecordOffset()
    {
        return HeaderSize;
    }

    void AssertBlobBytes(const AssetBlob &blob, const std::vector<uint8_t> &expected)
    {
        assert(blob.IsValid());
        assert(blob.GetSize() == expected.size());
        for (size_t index = 0; index < expected.size(); ++index)
        {
            assert(blob.GetData()[index] == expected[index]);
        }
    }
}

int main()
{
    std::cout << "PackageFormatTest start\n";

    const AssetPackageFourCC textureType = MakeAssetPackageFourCC('T', 'e', 'x', '0');
    const AssetPackageFourCC modelType = MakeAssetPackageFourCC('M', 'o', 'd', '0');
    const AssetPackageFourCC unknownType = MakeAssetPackageFourCC('U', 'n', 'k', 'n');

    {
        const std::vector<TestPackageEntry> sourceEntries = {
            {"textures/a.nvtex", textureType, {1, 2, 3, 4}},
            {"models/a.nvmesh", modelType, {5, 6, 7}}
        };
        const std::vector<uint8_t> bytes = BuildPackage(sourceEntries);

        Package package;
        assert(LoadBytes(package, bytes));
        assert(package.IsLoaded());
        assert(package.GetFormat() == PackageFormat::V1);
        assert(package.GetEntryCount() == 2);
        assert(package.GetDataSize() == bytes.size());
        assert(package.GetDataSpan().size() == bytes.size());
        assert(package.GetData() != nullptr);

        PackageEntry textureEntry;
        assert(package.FindEntry("textures/a.nvtex", textureType, textureEntry));
        assert(textureEntry.Name == "textures/a.nvtex");
        assert(textureEntry.Type == textureType);
        AssertBlobBytes(package.OpenEntry(textureEntry), sourceEntries[0].Payload);

        PackageEntry modelEntry;
        assert(package.GetEntry(1, modelEntry));
        assert(modelEntry.Name == "models/a.nvmesh");
        AssertBlobBytes(package.OpenEntry("models/a.nvmesh", modelType), sourceEntries[1].Payload);
    }

    {
        const std::vector<TestPackageEntry> sourceEntries = {
            {"unknown/type.bin", unknownType, {9, 8, 7}}
        };
        Package package;
        assert(LoadBytes(package, BuildPackage(sourceEntries)));

        PackageEntry entry;
        assert(package.FindEntry("unknown/type.bin", unknownType, entry));
        assert(entry.Type == unknownType);
        AssertBlobBytes(package.OpenEntry(entry), sourceEntries[0].Payload);
    }

    {
        const std::vector<uint8_t> rawBytes = {0xaa, 0xbb, 0xcc};
        Package package;
        assert(LoadBytes(package, rawBytes, _T("raw.bin")));
        assert(package.GetFormat() == PackageFormat::Raw);
        assert(package.GetEntryCount() == 1);
        assert(package.GetDataSize() == rawBytes.size());
        assert(package.GetDataSpan().size() == rawBytes.size());
        assert(package.GetData()[0] == rawBytes[0]);

        PackageEntry rawEntry;
        assert(package.FindEntry(RawEntryName, RawEntryType, rawEntry));
        assert(rawEntry.StoredSize == rawBytes.size());
        AssertBlobBytes(package.OpenEntry(rawEntry), rawBytes);
    }

    {
        Package package;
        const std::vector<uint8_t> partialMagic = {'N', 'V', 'P', 'K'};
        assert(!LoadBytes(package, partialMagic));
        assert(package.GetLoadState() == NorvesLib::FileStream::PackageLoadState::Failed);
    }

    {
        Package package;
        const std::vector<uint8_t> corruptMagic = {'N', 'V', 'P', 'K', 'x'};
        assert(!LoadBytes(package, corruptMagic));
        assert(package.GetFormat() == PackageFormat::None);
    }

    {
        Package package;
        std::vector<uint8_t> magicOnly(Magic, Magic + MagicSize);
        assert(!LoadBytes(package, magicOnly));
        assert(package.GetFormat() == PackageFormat::None);
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/version.bin", textureType, {1}}});
        WriteLe16(bytes, HeaderOffset::VersionMajor, 2);
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/endian.bin", textureType, {1}}});
        WriteLe32(bytes, HeaderOffset::EndianMarker, 0x04030201u);
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/header.bin", textureType, {1}}});
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize - 8));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/table.bin", textureType, {1}}});
        WriteLe64(bytes, HeaderOffset::EntryTableOffset, static_cast<uint64_t>(bytes.size() + 8));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/table-before-header.bin", textureType, {1}}});
        WriteLe64(bytes, HeaderOffset::EntryTableOffset, static_cast<uint64_t>(HeaderSize - MinimumAlignment));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/table-name-overlap.bin", textureType, {1}}});
        WriteLe64(bytes, HeaderOffset::NameTableOffset, static_cast<uint64_t>(HeaderSize + MinimumAlignment));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/name-blob-overlap.bin", textureType, {1}}});
        WriteLe64(bytes, HeaderOffset::BlobDataOffset, static_cast<uint64_t>(HeaderSize + EntryRecordSize));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/blob-out-of-range.bin", textureType, {1}}});
        WriteLe64(bytes, HeaderOffset::BlobDataOffset, static_cast<uint64_t>(bytes.size() + MinimumAlignment));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/name.bin", textureType, {1}}});
        WriteLe64(bytes, FirstEntryRecordOffset() + EntryOffset::NameOffset, static_cast<uint64_t>(bytes.size() + 8));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/data.bin", textureType, {1}}});
        WriteLe64(bytes, FirstEntryRecordOffset() + EntryOffset::DataOffset, static_cast<uint64_t>(bytes.size() + 8));
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/alignment.bin", textureType, {1}}});
        const uint64_t alignedDataOffset = static_cast<uint64_t>(HeaderSize + EntryRecordSize + AlignUp(std::string("bad/alignment.bin").size(), MinimumAlignment));
        WriteLe64(bytes, FirstEntryRecordOffset() + EntryOffset::DataOffset, alignedDataOffset + 1);
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        const std::vector<TestPackageEntry> sourceEntries = {
            {"duplicate.bin", textureType, {1}},
            {"duplicate.bin", textureType, {2}}
        };
        Package package;
        assert(!LoadBytes(package, BuildPackage(sourceEntries)));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/compression.bin", textureType, {1}}});
        WriteLe32(bytes, FirstEntryRecordOffset() + EntryOffset::Compression, 1);
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        std::vector<uint8_t> bytes = BuildPackage({{"bad/hash.bin", textureType, {1, 2, 3}}});
        WriteLe64(bytes, FirstEntryRecordOffset() + EntryOffset::PayloadHash, 0);
        Package package;
        assert(!LoadBytes(package, bytes));
    }

    {
        const std::vector<TestPackageEntry> sourceEntries = {
            {"empty.bin", textureType, {}}
        };
        Package package;
        assert(LoadBytes(package, BuildPackage(sourceEntries)));

        PackageEntry entry;
        assert(package.FindEntry("empty.bin", textureType, entry));
        assert(entry.StoredSize == 0);
        assert(entry.PayloadHash == ZeroSizePayloadHash);

        AssetBlob blob = package.OpenEntry(entry);
        assert(blob.IsValid());
        assert(blob.IsEmpty());
        assert(blob.GetSize() == 0);
    }

    {
        const std::vector<TestPackageEntry> sourceEntries = {
            {"lifetime.bin", textureType, {42, 43, 44}}
        };
        AssetBlob retainedBlob;
        {
            Package package;
            assert(LoadBytes(package, BuildPackage(sourceEntries)));

            PackageEntry entry;
            assert(package.FindEntry("lifetime.bin", textureType, entry));
            retainedBlob = package.OpenEntry(entry);
            assert(retainedBlob.IsValid());
            package.Unload();
            AssertBlobBytes(retainedBlob, sourceEntries[0].Payload);
        }
        AssertBlobBytes(retainedBlob, sourceEntries[0].Payload);
    }

    std::cout << "PackageFormatTest passed\n";
    return 0;
}
