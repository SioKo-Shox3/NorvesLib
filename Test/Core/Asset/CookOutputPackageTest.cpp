// 実単体cookのpackageとmanifestを使い、増分用の型検査・全体hash・失敗保持を検証する。
#include "Tools/AssetCook/CookOutputPackage.h"
#include "Tools/AssetCook/SingleAssetCook.h"
#include "FileStream/Package.h"
#include "M9LooseFixture.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    using Bytes = Container::VariableArray<uint8_t>;
    Bytes Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        CHECK(file);
        Bytes bytes;
        char c;
        while (file.get(c))
        {
            bytes.push_back(static_cast<uint8_t>(c));
        }
        CHECK(file.eof());
        return bytes;
    }
    void Write(const std::filesystem::path& path, Container::Span<const uint8_t> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        CHECK(file);
        if (!bytes.empty())
        {
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        file.close();
        CHECK(!file.fail());
    }
    void Put(Bytes& bytes, size_t offset, uint64_t value, size_t count)
    {
        CHECK(offset + count <= bytes.size());
        for (size_t i = 0; i < count; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    }
    uint64_t Get(const Bytes& bytes, size_t offset, size_t count)
    {
        CHECK(offset + count <= bytes.size());
        uint64_t value = 0;
        for (size_t i = 0; i < count; ++i)
        {
            value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);
        }
        return value;
    }
    Bytes SkeletalBuffer()
    {
        const auto writeFloat = [](Bytes& bytes, size_t offset, float value)
        {
            Put(bytes, offset, std::bit_cast<uint32_t>(value), 4);
        };
        const auto writeShort = [](Bytes& bytes, size_t offset, uint16_t value)
        {
            Put(bytes, offset, value, 2);
        };
        const auto writeMatrix = [&](Bytes& bytes, size_t offset, float inverseY)
        {
            for (const size_t d : {0, 5, 10, 15})
            {
                writeFloat(bytes, offset + d * 4, 1.0f);
            }
            writeFloat(bytes, offset + 13 * 4, inverseY);
        };
        return NorvesLib::Tests::AssetFixtures::BuildM9LooseBuffer<Bytes>(writeFloat, writeShort, writeMatrix);
    }
    void Reject(const AssetCookedReference& row, Container::Span<const uint8_t> bytes)
    {
        CookOutputPackageFingerprint out{99, 101};
        Container::AnsiString error = "held";
        CHECK(!ValidateCookOutputPackage(row, bytes, out, error));
        CHECK(out.Size == 99 && out.ContentHash == 101 && !error.empty());
    }
    CookOutputPackageFingerprint Accept(const AssetCookedReference& row, const Bytes& bytes)
    {
        CookOutputPackageFingerprint out;
        Container::AnsiString error = "held";
        if (!ValidateCookOutputPackage(row, bytes, out, error))
        {
            std::fprintf(stderr, "verify %s failed: %s\n", row.Format.c_str(), error.c_str());
            CHECK(false);
        }
        CHECK(error.empty() && out.Size == bytes.size());
        CHECK(out.ContentHash == ComputeAssetPackagePayloadHash(bytes.data(), bytes.size()));
        return out;
    }
    struct Cooked
    {
        AssetCookedReference Row;
        Bytes Package;
    };
    Cooked Cook(const std::filesystem::path& root, const char* name, const std::filesystem::path& source,
                const char* kind, const char* type, const char* format)
    {
        SingleAssetCookRequest r;
        r.InputPath = source;
        r.PackagePath = root / name / "Cooked/result.nvpkg";
        r.ManifestPath = root / name / "manifest.json";
        r.LogicalPath = "Test/asset";
        r.Kind = kind;
        r.EntryName = "__asset__";
        r.EntryTypeText = type;
        r.Format = format;
        r.Variant = "default";
        Container::AnsiString error;
        if (!CookSingleAsset(r, error))
        {
            std::fprintf(stderr, "cook failed: %s\n", error.c_str());
            CHECK(false);
        }
        const auto textBytes = Read(r.ManifestPath);
        Container::String text;
        text.reserve(textBytes.size());
        for (auto byte : textBytes)
        {
            text.push_back(static_cast<Container::String::value_type>(byte));
        }
        AssetManifest manifest;
        CHECK(manifest.LoadFromJsonText(text));
        CHECK(manifest.GetReferenceCount() > 0);
        if (std::strcmp(name, "mesh") == 0)
        {
            CHECK(manifest.GetReferenceCount() == 4);
        }
        Cooked result;
        for (size_t i = 0; i < manifest.GetReferenceCount(); ++i)
        {
            const auto& row = manifest.GetReference(i);
            const auto path = r.ManifestPath.parent_path() / std::filesystem::path(row.CookedPackage.c_str());
            auto package = Read(path);
            const auto original = package;
            Accept(row, package);
            CHECK(package.size() == original.size() &&
                  std::memcmp(package.data(), original.data(), package.size()) == 0);
            if (row.Kind != AssetKind::Raw)
            {
                // 外側のhashを更新した壊れた内側payloadも型parserで拒否する。
                auto broken = package;
                const auto table = Get(broken, AssetPackageFormatV1::HeaderOffset::EntryTableOffset, 8);
                const auto data =
                    Get(broken, static_cast<size_t>(table) + AssetPackageFormatV1::EntryOffset::DataOffset, 8);
                const auto size =
                    Get(broken, static_cast<size_t>(table) + AssetPackageFormatV1::EntryOffset::StoredSize, 8);
                CHECK(size > 0 && data + size <= broken.size());
                broken[static_cast<size_t>(data)] ^= 0x80;
                auto resealed = row;
                resealed.CookedHash = ComputeAssetPackagePayloadHash(broken.data() + data, static_cast<size_t>(size));
                Put(broken, static_cast<size_t>(table) + AssetPackageFormatV1::EntryOffset::PayloadHash,
                    resealed.CookedHash, 8);
                FileStream::Package validWrapper;
                CHECK(validWrapper.LoadFromMemory(broken));
                Reject(resealed, broken);
            }
            if (row.LogicalPath == r.LogicalPath)
            {
                result.Row = row;
                result.Package = std::move(package);
            }
        }
        CHECK(!result.Package.empty());
        return result;
    }
    // 正しく解析できる2entry package。単一entry制約をparser失敗とは独立に検証する。
    Bytes TwoEntries()
    {
        using namespace AssetPackageFormatV1;
        Bytes bytes(249, 0);
        std::memcpy(bytes.data(), Magic, MagicSize);
        Put(bytes, HeaderOffset::HeaderSize, HeaderSize, 4);
        Put(bytes, HeaderOffset::VersionMajor, VersionMajor, 2);
        Put(bytes, HeaderOffset::EndianMarker, EndianMarker, 4);
        Put(bytes, HeaderOffset::EntryRecordSize, EntryRecordSize, 4);
        Put(bytes, HeaderOffset::PackageSize, bytes.size(), 8);
        Put(bytes, HeaderOffset::EntryCount, 2, 4);
        Put(bytes, HeaderOffset::EntryTableOffset, 96, 8);
        Put(bytes, HeaderOffset::EntryTableSize, 128, 8);
        Put(bytes, HeaderOffset::NameTableOffset, 224, 8);
        Put(bytes, HeaderOffset::NameTableSize, 2, 8);
        Put(bytes, HeaderOffset::BlobDataOffset, 232, 8);
        Put(bytes, HeaderOffset::Alignment, 8, 4);
        bytes[224] = 'a';
        bytes[225] = 'b';
        bytes[232] = 1;
        bytes[248] = 2;
        for (size_t i = 0; i < 2; ++i)
        {
            const size_t table = 96 + i * 64, data = 232 + i * 16;
            Put(bytes, table + EntryOffset::NameOffset, 224 + i, 8);
            Put(bytes, table + EntryOffset::NameSize, 1, 4);
            Put(bytes, table + EntryOffset::Type, RawEntryType, 4);
            Put(bytes, table + EntryOffset::DataOffset, data, 8);
            Put(bytes, table + EntryOffset::StoredSize, 1, 8);
            Put(bytes, table + EntryOffset::UncompressedSize, 1, 8);
            Put(bytes, table + EntryOffset::PayloadHash, ComputeAssetPackagePayloadHash(bytes.data() + data, 1), 8);
        }
        return bytes;
    }
} // namespace
int main()
{
#if defined(_WIN32)
    char name[96];
    std::snprintf(name, sizeof(name), "norves-output-package-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
#else
    const char* name = "norves-output-package-native-required";
#endif
    const auto root = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(root));
    const uint8_t rawSource[] = {1, 2, 3};
    Write(root / "raw.bin", {rawSource, 3});
    const auto raw = Cook(root, "raw", root / "raw.bin", "raw", "Raw", "raw.v0");
    const auto custom = Cook(root, "custom", root / "raw.bin", "raw", "Cust", "raw.v0");
    Accept(custom.Row, custom.Package);
    Write(root / "empty.bin", {});
    const auto empty = Cook(root, "empty", root / "empty.bin", "raw", "Raw", "raw.v0");
    Accept(empty.Row, empty.Package);
    auto row = raw.Row;
    row.CookedVersion = 1;
    Reject(row, raw.Package);
    row = raw.Row;
    row.EntryName = "missing";
    Reject(row, raw.Package);
    row = raw.Row;
    row.EntryType = 0;
    Reject(row, raw.Package);
    row = raw.Row;
    row.CookedHash ^= 1;
    Reject(row, raw.Package);
    row = raw.Row;
    row.Kind = AssetKind::Unknown;
    Reject(row, raw.Package);
    row = raw.Row;
    row.bHasSkeletalMetadata = true;
    Reject(row, raw.Package);
    row = raw.Row;
    row.Format = "raw.v1";
    Reject(row, raw.Package);
    Reject(raw.Row, {});
    Reject(raw.Row, {static_cast<const uint8_t*>(nullptr), std::numeric_limits<size_t>::max()});
    Reject(raw.Row, {rawSource, 3});
    auto corrupt = raw.Package;
    corrupt.pop_back();
    Reject(raw.Row, corrupt);
    corrupt = raw.Package;
    corrupt.back() ^= 0x80;
    Reject(raw.Row, corrupt);
    corrupt = raw.Package;
    Put(corrupt, AssetPackageFormatV1::HeaderOffset::EntryTableOffset, corrupt.size(), 8);
    Reject(raw.Row, corrupt);
    // ASCII名9byteの直後からblobまでのpaddingだけを変更。payload hashは不変でも全体印は変わる。
    const auto namesEnd = Get(raw.Package, AssetPackageFormatV1::HeaderOffset::NameTableOffset, 8) +
                          Get(raw.Package, AssetPackageFormatV1::HeaderOffset::NameTableSize, 8);
    const auto blobStart = Get(raw.Package, AssetPackageFormatV1::HeaderOffset::BlobDataOffset, 8);
    CHECK(namesEnd < blobStart);
    auto padding = raw.Package;
    padding[static_cast<size_t>(namesEnd)] = 0x53;
    CHECK(Accept(raw.Row, padding).ContentHash != Accept(raw.Row, raw.Package).ContentHash);
    const auto two = TwoEntries();
    FileStream::Package twoPackage;
    CHECK(twoPackage.LoadFromMemory(two) && twoPackage.GetEntryCount() == 2);
    row = raw.Row;
    row.EntryName = "a";
    row.CookedHash = ComputeAssetPackagePayloadHash(two.data() + 232, 1);
    Reject(row, two);
    // hash文字列表現や無関係なsource/logical metadataはこのbyte検査の責務外。
    row = raw.Row;
    row.CookedHashHex = "ignored";
    row.SourceHash = 123;
    row.SourceHashHex = "ignored";
    row.CookedPackage = "other/path";
    row.LogicalPath = "other";
    row.Variant = "other";
    CHECK(Accept(row, raw.Package).ContentHash == Accept(raw.Row, raw.Package).ContentHash);
    const char* formats[] = {"nvtex.v0.rgba8.srgb", "nvtex.v0.rgba8.linear", "nvtex.v0.rg8.linear",
                             "nvtex.v0.r8.linear"};
    const char* labels[] = {"srgb", "linear", "rg", "r"};
    for (size_t i = 0; i < 4; ++i)
    {
        const auto texture = Cook(root, labels[i], "Assets/Models/Rendering3DTestSilverGltf/textures/silver_albedo.png",
                                  "texture", "Tex0", formats[i]);
        for (size_t j = 0; j < 4; ++j)
        {
            if (i != j)
            {
                row = texture.Row;
                row.Format = formats[j];
                Reject(row, texture.Package);
            }
        }
        row = texture.Row;
        row.Kind = AssetKind::Audio;
        row.Format = "nvaud.v0.pcm16";
        Reject(row, texture.Package);
    }
    const char* wavHex =
        "524946462C00000057415645666D7420100000000100010044AC0000885801000200100064617461080000000000010002000300";
    Bytes wav;
    for (size_t i = 0; i < std::strlen(wavHex); i += 2)
    {
        const auto digit = [](char c)
        {
            return c >= 'A' ? c - 'A' + 10 : c - '0';
        };
        wav.push_back(static_cast<uint8_t>(digit(wavHex[i]) * 16 + digit(wavHex[i + 1])));
    }
    Write(root / "tone.wav", wav);
    const auto audio = Cook(root, "audio", root / "tone.wav", "audio", "Aud0", "nvaud.v0.pcm16");
    row = audio.Row;
    row.Format = "nvaud.v0.float32";
    Reject(row, audio.Package);
    const auto mesh = Cook(root, "mesh", "Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb", "model", "Msh0",
                           "nvmesh.v0.mesh3d.pnt.u32.clustered");
    row = mesh.Row;
    row.Format = "nvmesh.v1.mesh3d.pnt.u32.clustered";
    Reject(row, mesh.Package);
    std::filesystem::copy("Assets/Models/M9Skinned", root / "skeletal", std::filesystem::copy_options::recursive);
    const auto buffer = SkeletalBuffer();
    Write(root / "skeletal/fixture.bin", buffer);
    const auto skeletalFixture =
        Cook(root, "skel", root / "skeletal/ValidU8Float.gltf", "model", "Skl0", "nvskel.v0.skinned.pnujiw.u32");
    row = skeletalFixture.Row;
    row.bHasSkeletalMetadata = false;
    Reject(row, skeletalFixture.Package);
    row = skeletalFixture.Row;
    row.SkeletalMetadata.bHasSubmeshCounts = false;
    Reject(row, skeletalFixture.Package);
    for (int i = 0; i < 6; ++i)
    {
        row = skeletalFixture.Row;
        auto& m = row.SkeletalMetadata;
        uint32_t* counts[] = {&m.VertexCount, &m.IndexCount,   &m.JointCount,
                              &m.ClipCount,   &m.SubmeshCount, &m.MaterialSlotCount};
        ++*counts[i];
        Reject(row, skeletalFixture.Package);
    }
    std::filesystem::remove_all(root);
    std::puts("CookOutputPackageTest PASS: actual_cooks_all_profiles_derived_images_hash_shape_metadata_failure_hold");
    return 0;
}
