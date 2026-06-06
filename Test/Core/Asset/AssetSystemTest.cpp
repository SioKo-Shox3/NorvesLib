#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetSystem.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tchar.h>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Asset;
using NorvesLib::Core::Container::AnsiString;
using NorvesLib::Core::Container::String;
using namespace NorvesLib::Core::Asset::AssetPackageFormatV1;

namespace
{
    struct TestPackageEntry
    {
        std::string Name;
        AssetPackageFourCC Type = 0;
        std::vector<uint8_t> Payload;
    };

    std::filesystem::path CreateTestRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("NorvesLibAssetSystemTest_" + std::to_string(now));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    std::string ToAssetString(const std::filesystem::path &path)
    {
        return path.generic_string();
    }

    std::string ToStdString(const AnsiString &text)
    {
        return std::string(text.data(), text.size());
    }

    String ToCoreString(const std::string &text)
    {
#if defined(UNICODE)
        std::wstring wide;
        wide.reserve(text.size());
        for (const char character : text)
        {
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }
        return String(wide.c_str());
#else
        return String(text.c_str());
#endif
    }

    size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
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
            WriteLe32(bytes, recordOffset + EntryOffset::Compression, static_cast<uint32_t>(AssetPackageCompression::None));
            WriteLe32(bytes, recordOffset + EntryOffset::Flags, 0);
            WriteLe64(bytes, recordOffset + EntryOffset::DataOffset, static_cast<uint64_t>(dataOffset));
            WriteLe64(bytes, recordOffset + EntryOffset::StoredSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes, recordOffset + EntryOffset::UncompressedSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes, recordOffset + EntryOffset::PayloadHash, ComputeAssetPackagePayloadHash(entry.Payload.data(), entry.Payload.size()));
            WriteLe64(bytes, recordOffset + EntryOffset::Reserved0, 0);
        }

        return bytes;
    }

    void WriteBinaryFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        assert(output.is_open());
        if (!bytes.empty())
        {
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
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

    String BuildManifest(std::string logicalPath = "Textures/Silver.png",
                         std::string cookedPackage = "Cooked/Textures.nvpkg",
                         std::string entryName = "Textures/Silver.nvtex",
                         AssetPackageFourCC entryType = MakeAssetPackageFourCC('T', 'e', 'x', '0'),
                         uint64_t cookedHash = 0,
                         std::string variant = "default")
    {
        const std::string cookedHashText = ToStdString(FormatAssetHashHex(cookedHash));
        const std::string entryTypeText = ToStdString(FormatAssetPackageFourCCText(entryType));
        const std::string json =
            "{"
            "\"version\":1,"
            "\"assets\":["
            "{"
            "\"logical_path\":\"" + logicalPath + "\","
            "\"kind\":\"texture\","
            "\"source_hash\":\"0000000000000001\","
            "\"variant\":\"" + variant + "\","
            "\"format\":\"nvtex.v0.rgba8.srgb\","
            "\"cooked_package\":\"" + cookedPackage + "\","
            "\"entry_name\":\"" + entryName + "\","
            "\"entry_type\":\"" + entryTypeText + "\","
            "\"cooked_hash\":\"" + cookedHashText + "\","
            "\"cooked_version\":0"
            "}"
            "]"
            "}";
        return ToCoreString(json);
    }

    AssetSystem CreateSystem(const std::filesystem::path &root)
    {
        return AssetSystem(ToAssetString(root).c_str());
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "AssetSystemTest start\n";

    const std::filesystem::path root = CreateTestRoot();
    const std::vector<uint8_t> loosePayload = {9, 8, 7};
    const std::vector<uint8_t> cookedPayload = {1, 2, 3, 4};
    const uint64_t cookedHash = ComputeAssetPackagePayloadHash(cookedPayload.data(), cookedPayload.size());
    const AssetPackageFourCC textureType = MakeAssetPackageFourCC('T', 'e', 'x', '0');

    WriteBinaryFile(root / "Textures" / "Silver.png", loosePayload);
    WriteBinaryFile(root / "Cooked" / "Textures.nvpkg",
                    BuildPackage({{"Textures/Silver.nvtex", textureType, cookedPayload}}));

    {
        AssetSystem system = CreateSystem(root);
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture);
        assert(result.Succeeded());
        assert(result.Status == AssetResolveStatus::SuccessLoose);
        assert(result.Source == AssetResolveSource::Loose);
        assert(result.ManifestStatus == AssetManifestResolveStatus::LooseFallbackManifestMissing);
        assert(result.NormalizedLogicalPath == "Textures/Silver.png");
        assert(result.LooseReadStatus == AssetReadStatus::Success);
        AssertBlobBytes(result.Blob, loosePayload);
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/Textures.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash)));
        const AssetResolveResult result = system.ResolveAsset("Assets/Textures/Silver.png", AssetKind::Texture);
        assert(result.Succeeded());
        assert(result.Status == AssetResolveStatus::SuccessCooked);
        assert(result.Source == AssetResolveSource::Cooked);
        assert(result.ManifestStatus == AssetManifestResolveStatus::CookedReferenceFound);
        assert(result.PackageReadStatus == AssetReadStatus::Success);
        assert(result.Entry.Name == "Textures/Silver.nvtex");
        AssertBlobBytes(result.Blob, cookedPayload);

        AssetBlob retainedBlob = result.Blob;
        AssertBlobBytes(retainedBlob, cookedPayload);
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/Textures.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash)));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture, "debug");
        assert(result.Succeeded());
        assert(result.Status == AssetResolveStatus::SuccessLoose);
        assert(result.Source == AssetResolveSource::Loose);
        assert(result.ManifestStatus == AssetManifestResolveStatus::LooseFallbackVariantMissing);
        AssertBlobBytes(result.Blob, loosePayload);
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(!system.LoadManifestFromJsonText(ToCoreString("{"), "broken.manifest.json"));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture);
        assert(!result.Succeeded());
        assert(result.Status == AssetResolveStatus::InvalidManifest);
        assert(result.ManifestStatus == AssetManifestResolveStatus::InvalidManifest);
        assert(!result.Blob.IsValid());
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/Missing.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash)));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture);
        assert(!result.Succeeded());
        assert(result.Status == AssetResolveStatus::CookedPackageReadFailed);
        assert(result.ManifestStatus == AssetManifestResolveStatus::CookedReferenceFound);
        assert(result.PackageReadStatus == AssetReadStatus::FileNotFound || result.PackageReadStatus == AssetReadStatus::OpenFailed);
        assert(!result.RequiresExplicitLog());
        assert(!result.Blob.IsValid());
    }

    {
        WriteBinaryFile(root / "Cooked" / "Corrupt.nvpkg", {'N', 'V', 'P', 'K', 'x'});
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/Corrupt.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash)));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture);
        assert(!result.Succeeded());
        assert(result.Status == AssetResolveStatus::CookedPackageParseFailed);
        assert(result.PackageReadStatus == AssetReadStatus::Success);
        assert(!result.Blob.IsValid());
    }

    {
        WriteBinaryFile(root / "Cooked" / "EntryMissing.nvpkg",
                        BuildPackage({{"Textures/Other.nvtex", textureType, cookedPayload}}));
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/EntryMissing.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash)));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture);
        assert(!result.Succeeded());
        assert(result.Status == AssetResolveStatus::CookedEntryMissing);
        assert(!result.Blob.IsValid());
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/Textures.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash + 1)));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture);
        assert(!result.Succeeded());
        assert(result.Status == AssetResolveStatus::CookedEntryHashMismatch);
        assert(result.FallbackDecision.FailureKind == AssetCookedFailureKind::EntryHashMismatch);
        assert(!result.RequiresExplicitLog());
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/Silver.png",
                                                             "Cooked/Textures.nvpkg",
                                                             "Textures/Silver.nvtex",
                                                             textureType,
                                                             cookedHash + 1)));
        const AssetResolveResult result = system.ResolveAsset("Textures/Silver.png",
                                                              AssetKind::Texture,
                                                              AssetManifest::DefaultVariant,
                                                              AssetFallbackMode::DebugAllowLooseFallback);
        assert(result.Succeeded());
        assert(result.Status == AssetResolveStatus::SuccessLoose);
        assert(result.Source == AssetResolveSource::DebugLooseFallback);
        assert(result.RequiresExplicitLog());
        assert(result.FallbackDecision.FailureKind == AssetCookedFailureKind::EntryHashMismatch);
        assert(result.FallbackDecision.LogicalPath == "Textures/Silver.png");
        assert(result.FallbackDecision.CookedPackage == "Cooked/Textures.nvpkg");
        assert(result.FallbackDecision.EntryName == "Textures/Silver.nvtex");
        AssertBlobBytes(result.Blob, loosePayload);
    }

    {
        AssetSystem system = CreateSystem(root);
        assert(system.LoadManifestFromJsonText(BuildManifest("Textures/MissingLoose.png",
                                                             "Cooked/Missing.nvpkg",
                                                             "Textures/MissingLoose.nvtex",
                                                             textureType,
                                                             cookedHash)));
        const AssetResolveResult result = system.ResolveAsset("Textures/MissingLoose.png",
                                                              AssetKind::Texture,
                                                              AssetManifest::DefaultVariant,
                                                              AssetFallbackMode::DebugAllowLooseFallback);
        assert(!result.Succeeded());
        assert(result.Status == AssetResolveStatus::LooseReadFailed);
        assert(result.Source == AssetResolveSource::DebugLooseFallback);
        assert(result.RequiresExplicitLog());
        assert(result.FallbackDecision.FailureKind == AssetCookedFailureKind::PackageMissing);
        assert(result.LooseReadStatus == AssetReadStatus::FileNotFound || result.LooseReadStatus == AssetReadStatus::OpenFailed);
        assert(!result.Blob.IsValid());
    }

    {
        AssetSystem system = CreateSystem(root);
        const AssetResolveResult unknownKind = system.ResolveAsset("Textures/Silver.png", AssetKind::Unknown);
        assert(unknownKind.Status == AssetResolveStatus::InvalidRequest);

        const AssetResolveResult emptyLogical = system.ResolveAsset("", AssetKind::Texture);
        assert(emptyLogical.Status == AssetResolveStatus::InvalidRequest);

        const AssetResolveResult emptyVariant = system.ResolveAsset("Textures/Silver.png", AssetKind::Texture, "");
        assert(emptyVariant.Status == AssetResolveStatus::InvalidRequest);

        const AssetResolveResult absolute = system.ResolveAsset("C:/tmp/Silver.png", AssetKind::Texture);
        assert(absolute.Status == AssetResolveStatus::InvalidRequest);

        const AssetResolveResult unc = system.ResolveAsset("//server/share/Silver.png", AssetKind::Texture);
        assert(unc.Status == AssetResolveStatus::InvalidRequest);

        const AssetResolveResult driveRelative = system.ResolveAsset("C:tmp/Silver.png", AssetKind::Texture);
        assert(driveRelative.Status == AssetResolveStatus::InvalidRequest);

        const AssetResolveResult escape = system.ResolveAsset("../Silver.png", AssetKind::Texture);
        assert(escape.Status == AssetResolveStatus::InvalidRequest);
    }

    std::filesystem::remove_all(root);

    std::cout << "AssetSystemTest passed\n";
    return 0;
}
