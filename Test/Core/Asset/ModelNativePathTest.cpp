// native locatorの静的・骨格cook、外部依存、sidecarと失敗保持を検証する。
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/ModelInspection.h"
#include "Tools/AssetCook/NativeCookPath.h"
#include "Tools/AssetCook/AssetCookOutput.h"
#include "Tools/AssetCook/CookCacheDecision.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/GltfBufferSet.h"
#include "M9LooseFixture.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    using Bytes = Container::VariableArray<uint8_t>;
    using NativeText = Container::AnsiString;
    constexpr const char* MeshFormat = "nvmesh.v0.mesh3d.pnt.u32.clustered";
    constexpr const char* RigFormat = "nvskel.v0.skinned.pnujiw.u32";
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
    void WriteText(const std::filesystem::path& path, const char* text)
    {
        Write(path, {reinterpret_cast<const uint8_t*>(text), std::strlen(text)});
    }
    bool Equal(const Bytes& a, const Bytes& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
    void Put(Bytes& bytes, size_t offset, uint64_t value, size_t count)
    {
        CHECK(offset + count <= bytes.size());
        for (size_t i = 0; i < count; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    }
    Bytes RigBuffer()
    {
        const auto wf = [](Bytes& b, size_t o, float v)
        {
            Put(b, o, std::bit_cast<uint32_t>(v), 4);
        };
        const auto ws = [](Bytes& b, size_t o, uint16_t v)
        {
            Put(b, o, v, 2);
        };
        const auto wm = [&](Bytes& b, size_t o, float y)
        {
            for (size_t d : {0, 5, 10, 15})
            {
                wf(b, o + d * 4, 1);
            }
            wf(b, o + 13 * 4, y);
        };
        return NorvesLib::Tests::AssetFixtures::BuildM9LooseBuffer<Bytes>(wf, ws, wm);
    }
    Bytes RigGlb(const Bytes& json, const Bytes& bin)
    {
        NativeText text;
        text.append(reinterpret_cast<const char*>(json.data()), json.size());
        const char* needle = "\"uri\": \"fixture.bin\",";
        const auto found = std::strstr(text.c_str(), needle);
        CHECK(found);
        const size_t prefix = static_cast<size_t>(found - text.c_str());
        NativeText embedded;
        embedded.append(text.data(), prefix);
        embedded.append(text.data() + prefix + std::strlen(needle), text.size() - prefix - std::strlen(needle));
        while (embedded.size() % 4)
        {
            embedded.push_back(' ');
        }
        const size_t binSize = (bin.size() + 3) & ~size_t(3);
        Bytes result(12 + 8 + embedded.size() + 8 + binSize, 0);
        Put(result, 0, 0x46546c67, 4);
        Put(result, 4, 2, 4);
        Put(result, 8, result.size(), 4);
        Put(result, 12, embedded.size(), 4);
        Put(result, 16, 0x4e4f534a, 4);
        std::memcpy(result.data() + 20, embedded.data(), embedded.size());
        const size_t offset = 20 + embedded.size();
        Put(result, offset, binSize, 4);
        Put(result, offset + 4, 0x004e4942, 4);
        std::memcpy(result.data() + offset + 8, bin.data(), bin.size());
        return result;
    }
    AssetManifest LoadManifest(const std::filesystem::path& path)
    {
        const auto bytes = Read(path);
        Container::String text;
        text.reserve(bytes.size());
        for (auto b : bytes)
        {
            text.push_back(static_cast<Container::String::value_type>(b));
        }
        AssetManifest result;
        CHECK(result.LoadFromJsonText(text));
        return result;
    }
    void CheckStatic(const std::filesystem::path& path, const Bytes& bytes, const std::filesystem::path& ascii)
    {
        NativeText error;
        MeshCookResult cooked, baseline;
        ModelCookFingerprint fingerprint;
        ModelInspection inspection;
        const auto narrow = ascii.generic_string();
        CHECK(CookGltfToNvmesh(bytes.data(), bytes.size(), MeshFormat, narrow.c_str(), "Test/model", baseline, error));
        CHECK(CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model", cooked, error));
        CHECK(FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model",
                                                   fingerprint, error));
        CHECK(InspectGltfModelNativePath(bytes.data(), bytes.size(), path, inspection, error));
        CHECK(Equal(cooked.NvmeshBytes, baseline.NvmeshBytes));
        CHECK(cooked.SourceHash == baseline.SourceHash && cooked.SourceHash == fingerprint.SourceHash);
        CHECK(cooked.EmbeddedImages.size() == baseline.EmbeddedImages.size());
        CHECK(cooked.bHasImportSettings == fingerprint.bHasImportSettings &&
              cooked.ImportSettingsPath == fingerprint.ImportSettingsPath);
        const auto sidecar = std::filesystem::path(path).concat(".import.json");
        WriteText(sidecar, "{\"version\":1}");
        CHECK(CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model", cooked, error));
        CHECK(FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model",
                                                   fingerprint, error));
        CHECK(cooked.bHasImportSettings && cooked.ImportSettingsPath == sidecar);
        CHECK(cooked.SourceHash == fingerprint.SourceHash &&
              cooked.ImportSettingsHash == fingerprint.ImportSettingsHash);
        const auto overridePath = path.parent_path() / std::filesystem::path(u8"設定\U0001f43a.json");
        WriteText(overridePath, "{\"version\":1}");
        AssetImport::ImportSettingsFileOptions options;
        options.OverridePath = overridePath;
        CHECK(CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model", cooked, error,
                                         &options));
        CHECK(FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model",
                                                   fingerprint, error, &options));
        CHECK(cooked.ImportSettingsPath == overridePath && fingerprint.ImportSettingsPath == overridePath);
        CHECK(cooked.SourceHash == fingerprint.SourceHash);
        const auto held = cooked.NvmeshBytes;
        const auto heldHash = cooked.SourceHash;
        std::filesystem::remove(overridePath);
        options.bRequired = true;
        CHECK(!CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model", cooked, error,
                                          &options));
        CHECK(Equal(held, cooked.NvmeshBytes) && heldHash == cooked.SourceHash);
        WriteText(sidecar, "invalid ignored sidecar");
        options = {};
        options.bDisabled = true;
        CHECK(CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), MeshFormat, path, "Test/model", cooked, error,
                                         &options));
        CHECK(!cooked.bHasImportSettings && Equal(cooked.NvmeshBytes, baseline.NvmeshBytes));
        std::filesystem::remove(sidecar);
    }
    void CheckRig(const std::filesystem::path& path, const Bytes& bytes)
    {
        NativeText error;
        SkeletalCookResult cooked;
        ModelCookFingerprint fingerprint;
        Gltf::BufferSet buffers;
        CHECK(Skeletal::DecodeRigGltfNativePath(bytes, path, &buffers).Succeeded());
        CHECK(buffers.GetCount() == 1);
        CHECK(CookGltfToNvskelNativePath(bytes.data(), bytes.size(), RigFormat, path, cooked, error));
        CHECK(FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), RigFormat, path, "Test/model",
                                                   fingerprint, error));
        CHECK(cooked.SourceHash == fingerprint.SourceHash && cooked.JointCount == 2 && cooked.ClipCount == 1);
        const auto overridePath = path.parent_path() / std::filesystem::path(u8"骨格設定\U0001f43a.json");
        WriteText(overridePath, "{\"version\":1}");
        AssetImport::ImportSettingsFileOptions options;
        options.OverridePath = overridePath;
        CHECK(CookGltfToNvskelNativePath(bytes.data(), bytes.size(), RigFormat, path, cooked, error, &options));
        CHECK(FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), RigFormat, path, "Test/model",
                                                   fingerprint, error, &options));
        CHECK(cooked.ImportSettingsPath == overridePath && fingerprint.ImportSettingsPath == overridePath);
        CHECK(cooked.SourceHash == fingerprint.SourceHash &&
              cooked.ImportSettingsHash == fingerprint.ImportSettingsHash);
        std::filesystem::remove(overridePath);
#if defined(_WIN32)
        const wchar_t invalidUnits[] = {L'x', static_cast<wchar_t>(0xd800), L'z'};
#else
        const char invalidUnits[] = {'x', static_cast<char>(0xff), 'z'};
#endif
        const std::filesystem::path invalid(invalidUnits, invalidUnits + 3);
        CHECK(!Skeletal::DecodeRigGltfNativePath(bytes, invalid, &buffers).Succeeded());
        CHECK(buffers.GetCount() == 0);
        const auto held = cooked.NvskelBytes;
        CHECK(!CookGltfToNvskelNativePath(bytes.data(), bytes.size(), RigFormat, invalid, cooked, error));
        CHECK(Equal(held, cooked.NvskelBytes));
    }
    void CheckCache(const std::filesystem::path& source, const std::filesystem::path& output, bool bRig,
                    const std::filesystem::path& dependency = {})
    {
        SingleAssetCookRequest request;
        request.InputPath = source;
        request.PackagePath = output / "Cooked/model.nvpkg";
        request.ManifestPath = output / "manifest.json";
        request.LogicalPath = "Test/model";
        request.Kind = "model";
        request.EntryName = "__asset__";
        request.EntryTypeText = bRig ? "Skl0" : "Msh0";
        request.Format = bRig ? RigFormat : MeshFormat;
        request.Variant = "default";
        NativeText error;
        CookDecisionContext before;
        CookOutputRecord record;
        CHECK(DecideCookCache(request, 7, true, nullptr, nullptr, before, error) == CookDecision::Cook);
        CHECK(CookSingleAsset(before.Request, error));
        const auto manifest = LoadManifest(request.ManifestPath);
        CHECK(CaptureCookOutputRecord(before, manifest, record, error));
        CookDecisionContext after;
        CHECK(DecideCookCache(request, 7, true, &record, &manifest, after, error) == CookDecision::Skip);
        const auto sidecar = std::filesystem::path(source).concat(".import.json");
        WriteText(sidecar, "{\"version\":1}");
        CHECK(DecideCookCache(request, 7, true, &record, &manifest, after, error) == CookDecision::Cook);
        CHECK(CookSingleAsset(after.Request, error));
        const auto updatedManifest = LoadManifest(request.ManifestPath);
        CHECK(CaptureCookOutputRecord(after, updatedManifest, record, error));
        CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Skip);
        WriteText(sidecar, "{ \"version\":1 }\n");
        CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Cook);
        WriteText(sidecar, "{\"version\":1}");
        CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Skip);
        if (!dependency.empty())
        {
            const auto held = Read(dependency);
            auto changed = held;
            changed.push_back(0);
            Write(dependency, changed);
            CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Cook);
            Write(dependency, held);
            const auto moved = std::filesystem::path(dependency).concat(".held");
            std::filesystem::rename(dependency, moved);
            CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Error);
            std::filesystem::rename(moved, dependency);
            CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Skip);
        }
        const auto explicitSettings = source.parent_path() / std::filesystem::path(u8"増分設定\U0001f43a.json");
        WriteText(explicitSettings, "{\"version\":1}");
        request.ImportSettingsOverridePath = explicitSettings;
        CHECK(DecideCookCache(request, 7, true, &record, &updatedManifest, after, error) == CookDecision::Cook);
        CHECK(CookSingleAsset(after.Request, error));
        const auto explicitManifest = LoadManifest(request.ManifestPath);
        CHECK(CaptureCookOutputRecord(after, explicitManifest, record, error));
        CHECK(DecideCookCache(request, 7, true, &record, &explicitManifest, after, error) == CookDecision::Skip);
        std::filesystem::remove(explicitSettings);
        CHECK(DecideCookCache(request, 7, true, &record, &explicitManifest, after, error) == CookDecision::Error);
        request.ImportSettingsOverridePath.clear();
        std::filesystem::remove(sidecar);
        request.InputPath = source.parent_path() / std::filesystem::path(u8"不在\U0001f43a.gltf");
        CHECK(!CookSingleAsset(request, error));
        CHECK(!error.empty());
    }
} // namespace
int main()
{
#if defined(_WIN32)
    char name[100];
    std::snprintf(name, sizeof(name), "norves-native-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
#else
    const char* name = "norves-native-test";
#endif
    const auto root = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(root));
    const auto native = root / std::filesystem::path(u8"モデル\U0001f43a") / std::filesystem::path(u8"深い階層");
    CHECK(std::filesystem::create_directories(native));
    std::filesystem::copy("Assets/Models/Rendering3DTestSilverGltf", root / "ascii",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy(root / "ascii", native / "static", std::filesystem::copy_options::recursive);
    const auto ascii = root / "ascii/Rendering3DTestSilverGltf.gltf";
    const auto loose = native / "static" / std::filesystem::path(u8"銀\U0001f43a.gltf");
    std::filesystem::rename(native / "static/Rendering3DTestSilverGltf.gltf", loose);
    CheckStatic(loose, Read(loose), ascii);
    const auto glb = native / std::filesystem::path(u8"犬\U0001f43a.glb");
    const auto embedded = Read("Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb");
    Write(glb, embedded);
    const auto asciiGlb = root / "ascii.glb";
    Write(asciiGlb, embedded);
    CheckStatic(glb, embedded, asciiGlb);
    MeshCookResult emptyPath;
    NativeText error;
    CHECK(CookGltfToNvmesh(embedded.data(), embedded.size(), MeshFormat, {}, "Test/model", emptyPath, error));
    CHECK(emptyPath.EmbeddedImages.size() == 3);
    const auto skeleton = native / std::filesystem::path(u8"骨格\U0001f43a.gltf");
    const auto rigJson = Read("Assets/Models/M9Skinned/ValidU8Float.gltf");
    const auto rigBin = RigBuffer();
    Write(skeleton, rigJson);
    Write(native / "fixture.bin", rigBin);
    CheckRig(skeleton, rigJson);
    const auto skeletonGlb = native / std::filesystem::path(u8"骨格\U0001f43a.glb");
    const auto rigGlb = RigGlb(rigJson, rigBin);
    Write(skeletonGlb, rigGlb);
    CheckRig(skeletonGlb, rigGlb);
    CheckCache(loose, root / "out-static", false, native / "static/Rendering3DTestSilverGltf.bin");
    CheckCache(loose, root / "out-image", false, native / "static/textures/silver_albedo.png");
    CheckCache(glb, root / "out-glb", false);
    CheckCache(skeleton, root / "out-rig", true, native / "fixture.bin");
    CheckCache(skeletonGlb, root / "out-rig-glb", true);
    NativeText display;
    CHECK(NorvesLib::Tools::AssetCook::Detail::EncodeCookPathUtf8(glb, display));
    const auto expected = glb.generic_u8string();
    CHECK(display.size() == expected.size() && std::memcmp(display.data(), expected.data(), display.size()) == 0);
    const std::filesystem::path::value_type nulUnits[] = {'x', 0, 'z'};
    const std::filesystem::path nulPath(nulUnits, nulUnits + 3);
    const auto held = emptyPath.NvmeshBytes;
    CHECK(!CookGltfToNvmeshNativePath(embedded.data(), embedded.size(), MeshFormat, nulPath, "Test/model", emptyPath,
                                      error));
    CHECK(Equal(held, emptyPath.NvmeshBytes));
    ModelCookFingerprint heldFingerprint;
    heldFingerprint.SourceHash = 777;
    ModelInspection heldInspection;
    heldInspection.MaterialIndex = 99;
    CHECK(!FingerprintModelCookSourceNativePath(embedded.data(), embedded.size(), MeshFormat, nulPath, "Test/model",
                                                heldFingerprint, error));
    CHECK(heldFingerprint.SourceHash == 777);
    CHECK(!InspectGltfModelNativePath(embedded.data(), embedded.size(), nulPath, heldInspection, error));
    CHECK(heldInspection.MaterialIndex == 99);
    AssetImport::ImportSettingsFileOptions invalidOverride;
    invalidOverride.OverridePath = nulPath;
    CHECK(!CookGltfToNvmeshNativePath(embedded.data(), embedded.size(), MeshFormat, glb, "Test/model", emptyPath, error,
                                      &invalidOverride));
    CHECK(Equal(held, emptyPath.NvmeshBytes));
    display = "held";
    CHECK(!NorvesLib::Tools::AssetCook::Detail::EncodeCookPathUtf8(nulPath, display) && display == "held");
#if defined(_WIN32)
    const auto handle =
        CreateFileW(glb.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(handle != INVALID_HANDLE_VALUE);
    SingleAssetCookRequest request;
    request.InputPath = glb;
    request.PackagePath = root / "locked/model.nvpkg";
    request.ManifestPath = root / "locked/manifest.json";
    request.Kind = "model";
    request.Format = MeshFormat;
    request.EntryTypeText = "Msh0";
    request.EntryName = "__asset__";
    request.LogicalPath = "Test/model";
    request.Variant = "default";
    CHECK(!CookSingleAsset(request, error));
    CHECK(!error.empty());
    CHECK(CloseHandle(handle));
#endif
    std::filesystem::remove_all(root);
    std::puts("MODEL_NATIVE_PATH result=pass static_rig_loose_glb_sidecar_cache_unicode_hold");
    return 0;
}
