// 集合を跨ぐ衝突、fresh依存、aliasと集約観測回数を検証する。
#include "Tools/AssetCook/CookOutputSetGuard.h"
#include "Tools/AssetCook/CookOutputSetGuardTestAccess.h"
#include "Tools/AssetCook/CookPathIdentity.h"
#include <algorithm>
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
namespace SetTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace API = NorvesLib::Tools::AssetCook::Detail;
    using SetText = NorvesLib::Core::Container::AnsiString;
    using SetBytes = NorvesLib::Core::Container::VariableArray<uint8_t>;
    using Plans = NorvesLib::Core::Container::VariableArray<CookPreparedPlan>;
    using Locators = NorvesLib::Core::Container::VariableArray<std::filesystem::path>;
    void Write(const std::filesystem::path& path, NorvesLib::Core::Container::Span<const uint8_t> bytes)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        CHECK(f);
        if (!bytes.empty())
        {
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        f.close();
        CHECK(!f.fail());
    }
    void TextFile(const std::filesystem::path& path, const char* text)
    {
        Write(path, {reinterpret_cast<const uint8_t*>(text), std::strlen(text)});
    }
    SetBytes Read(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        CHECK(f);
        SetBytes b;
        char c;
        while (f.get(c))
        {
            b.push_back(static_cast<uint8_t>(c));
        }
        CHECK(f.eof());
        return b;
    }
    SingleAssetCookRequest Request(const std::filesystem::path& root, const std::filesystem::path& source,
                                   const char* key, const char* target)
    {
        SingleAssetCookRequest r;
        r.InputPath = source;
        r.ManifestPath = root / "final/manifest.json";
        r.PackagePath = root / "final" / target;
        r.LogicalPath = key;
        r.Kind = "raw";
        r.EntryName = "__asset__";
        r.EntryTypeText = "Raw";
        r.Format = "raw.v0";
        r.Variant = "default";
        return r;
    }
    CookPreparedPlan Plan(const SingleAssetCookRequest& request)
    {
        CookPreparedPlan p;
        SetText error;
        if (!PrepareCookOutputPlan(request, 7, nullptr, p, error))
        {
            std::fprintf(stderr, "prepare: %s\n", error.c_str());
            CHECK(false);
        }
        return p;
    }
    API::CookOutputSetGuardStats Good(const Plans& plans, const Locators& controls = {})
    {
        SetText error;
        API::CookOutputSetGuardStats stats;
        if (!API::ValidateCookOutputSetForTest(plans, controls, stats, error))
        {
            std::fprintf(stderr, "set: %s\n", error.c_str());
            CHECK(false);
        }
        CHECK(error.empty());
        CHECK(stats.UniqueLocators == stats.AggregateIdentityObservations);
        return stats;
    }
    void Bad(const Plans& plans, const Locators& controls = {})
    {
        SetText error;
        CHECK(!ValidateCookOutputSet(plans, controls, error));
        CHECK(!error.empty());
    }
    void ObserveBad(const std::filesystem::path& path)
    {
        API::CookPathIdentity held;
        held.VolumeSerial = 123;
        held.Canonical = "held";
        SetText error;
        CHECK(!API::ObserveCookPathIdentity(path, held, error));
        CHECK(!error.empty() && held.VolumeSerial == 123 && held.Canonical == "held");
    }
#if defined(_WIN32)
    void Junction(const std::filesystem::path& link, const std::filesystem::path& target)
    {
        char command[4096];
        std::snprintf(command, sizeof(command), "cmd /c mklink /J \"%s\" \"%s\" >nul", link.string().c_str(),
                      target.string().c_str());
        CHECK(std::system(command) == 0);
    }
#endif
} // namespace SetTest
int main()
{
#if !defined(_WIN32)
    return 125;
#else
    using namespace SetTest;
    char name[100];
    std::snprintf(name, sizeof(name), "norves-set-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
    const auto root = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(root));
    const auto aSource = root / "a.bin", bSource = root / "b.bin";
    TextFile(aSource, "a");
    TextFile(bSource, "b");
    TextFile(root / "spec.json", "spec");
    auto aRequest = Request(root, aSource, "A", "Cooked/a.nvpkg"),
         bRequest = Request(root, bSource, "B", "Cooked/b.nvpkg");
    const auto a = Plan(aRequest), b = Plan(bRequest);
    const auto stamp = std::filesystem::last_write_time(aSource);
    const auto initial = Good({a, b}, {root / "spec.json"});
    CHECK(initial.LocatorOccurrences == 7 && initial.UniqueLocators == 6);
    CHECK(!std::filesystem::exists(root / "final") && std::filesystem::last_write_time(aSource) == stamp);
    // FINALは単一manifest、asset別stageは互いに独立したfragment。契約を混ぜない。
    {
        const auto first = root / "stage-first", second = root / "stage-second";
        CHECK(std::filesystem::create_directory(first) && std::filesystem::create_directory(second));
        CookPreparedPlan stageA, stageB;
        SetText error;
        CHECK(PrepareCookStagingPlan(a, first, stageA, error));
        CHECK(PrepareCookStagingPlan(b, second, stageB, error));
        Plans fragments{stageA, stageB};
        Locators controls{root / "spec.json"};
        CHECK(!ValidateCookOutputSet(fragments, controls, error) &&
              std::strstr(error.c_str(), "set_requires_one_manifest_path"));
        CHECK(ValidateCookStagingOutputSet(fragments, controls, error) && error.empty());
        Plans shared{a, b};
        CHECK(!ValidateCookStagingOutputSet(shared, controls, error));
        controls.push_back(stageA.Context.Request.ManifestPath);
        CHECK(!ValidateCookStagingOutputSet(fragments, controls, error));
        controls.pop_back();
        CHECK(std::filesystem::create_directory(first / "Cooked"));
        TextFile(stageA.Outputs[0].TargetPath, "protected stage source");
        auto cross = stageB.Context.Request;
        cross.InputPath = stageA.Outputs[0].TargetPath;
        Plans crossed{stageA, Plan(cross)};
        CHECK(!ValidateCookStagingOutputSet(crossed, controls, error));
    }
    auto changed = bRequest;
    changed.LogicalPath = "A";
    Bad({a, Plan(changed)});
    changed.Variant = "other";
    Good({a, Plan(changed)});
    // 不在manifestのcase-only差分は同一endpointと仮定しない。
    changed = bRequest;
    changed.ManifestPath = root / "final/MANIFEST.JSON";
    Bad({a, Plan(changed)});
    API::CookPathIdentity absentLower, absentUpper, presentA, presentB;
    SetText identityError;
    CHECK(API::ObserveCookPathIdentity(aRequest.ManifestPath, absentLower, identityError));
    CHECK(API::ObserveCookPathIdentity(changed.ManifestPath, absentUpper, identityError));
    CHECK(!API::SameCookManifestEndpoint(absentLower, absentUpper));
    CHECK(API::SameCookManifestEndpoint(absentLower, absentLower));
    CHECK(API::ObserveCookPathIdentity(aSource, presentA, identityError));
    CHECK(API::ObserveCookPathIdentity(bSource, presentB, identityError));
    CHECK(API::SameCookManifestEndpoint(presentA, presentA));
    // 観測済みの異なるIDを同じpath比較値に載せる値レベルの反証。
    // case-sensitive directory設定の成功を前提にしない。
    presentB.Canonical = presentA.Canonical;
    presentB.Components = presentA.Components;
    CHECK(API::CompareCookPhysicalPath(presentA, presentB) == 0);
    CHECK(!API::SameCookManifestEndpoint(presentA, presentB));
    auto absentVersion = presentA;
    absentVersion.bPresent = false;
    CHECK(!API::SameCookManifestEndpoint(presentA, absentVersion));
    changed = bRequest;
    changed.PackagePath = aRequest.PackagePath;
    Bad({a, Plan(changed)});
    changed.PackagePath = root / "final/Cooked/A.NVPKG";
    Bad({a, Plan(changed)});
    Plans prefixes = {Plan(Request(root, aSource, "P1", "a")), Plan(Request(root, aSource, "P2", "a!")),
                      Plan(Request(root, aSource, "P3", "a/x"))};
    unsigned order[] = {0, 1, 2};
    do
    {
        Bad({prefixes[order[0]], prefixes[order[1]], prefixes[order[2]]});
    } while (std::next_permutation(order, order + 3));
    Good({Plan(Request(root, aSource, "S1", "shared/a")), Plan(Request(root, aSource, "S2", "shared/b"))});
    Bad({Plan(Request(root, aSource, "Q", "a/x"))}, {root / "final/a", root / "final/a!"});
    Bad({a}, {aRequest.PackagePath});
    Bad({a}, {aRequest.ManifestPath});
    changed = bRequest;
    changed.ManifestPath = root / "another/manifest.json";
    changed.PackagePath = root / "another/b.nvpkg";
    Bad({a, Plan(changed)});
    CHECK(std::filesystem::create_directories(root / "final/Cooked"));
    TextFile(root / "final/foreign.txt", "unrelated");
    TextFile(root / "final/input.bin", "input");
    const auto sourcePlan = Plan(Request(root, root / "final/input.bin", "source", "Cooked/source.nvpkg"));
    const auto outputPlan = Plan(Request(root, aSource, "overwrite", "input.bin"));
    Bad({outputPlan, sourcePlan});
    auto hidden = sourcePlan;
    hidden.Context.Dependencies.Files.clear();
    Bad({outputPlan, hidden});
    hidden = sourcePlan;
    hidden.Context.Dependencies.Files[0].Path = root / "not-the-source";
    Bad({outputPlan, hidden});
    CHECK(Read(root / "final/input.bin").size() == 5 && Read(root / "final/foreign.txt").size() == 9);
    TextFile(root / "final/spec.json", "reserved");
    Bad({Plan(Request(root, aSource, "spec-output", "spec.json"))}, {root / "final/spec.json"});
    Bad({Plan(Request(root, aSource, "state-output", "future-state.json"))}, {root / "final/future-state.json"});
    std::filesystem::create_hard_link(aSource, root / "a-alias.bin");
    Good({a, Plan(Request(root, root / "a-alias.bin", "alias-input", "Cooked/alias-input.nvpkg"))});
    TextFile(root / "final/Cooked/linked.nvpkg", "unowned");
    std::filesystem::create_hard_link(root / "final/Cooked/linked.nvpkg", root / "unlisted.bin");
    Bad({Plan(Request(root, aSource, "linked-output", "Cooked/linked.nvpkg"))});
    CHECK(Read(root / "unlisted.bin").size() == 7);
    const auto unicode = root / "final" / std::filesystem::path(u8"犬\U0001f43a.bin");
    TextFile(unicode, "unicode");
    const auto unicodePlan = Plan(Request(root, unicode, "unicode", "Cooked/unicode.nvpkg"));
    Good({a, unicodePlan});
    std::filesystem::create_hard_link(unicode, root / "final/Cooked/unicode-link.nvpkg");
    Bad({unicodePlan, Plan(Request(root, aSource, "unicode-link", "Cooked/unicode-link.nvpkg"))});
    wchar_t shortRoot[32768]{};
    const DWORD shortSize = GetShortPathNameW((root / "final").c_str(), shortRoot, 32768);
    CHECK(shortSize && shortSize < 32768);
    changed = bRequest;
    changed.ManifestPath = std::filesystem::path(shortRoot) / "manifest.json";
    changed.PackagePath = std::filesystem::path(shortRoot) / "Cooked/a.nvpkg";
    Bad({a, Plan(changed)});
    changed.PackagePath = std::filesystem::path(shortRoot) / "Cooked/b-short.nvpkg";
    Good({a, Plan(changed)});
    std::printf("set_short_alias_distinct=%d\n",
                std::filesystem::path(shortRoot).native() != (root / "final").native());
    const auto link = root / "control-link", target = root / "control-target";
    CHECK(std::filesystem::create_directory(target));
    Junction(link, target);
    Bad({a}, {link / "future.json"});
    ObserveBad(link / "future.json");
    CHECK(std::filesystem::remove(link));
    ObserveBad(root / "final");
    HANDLE locked =
        CreateFileW(aSource.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    Bad({a});
    CHECK(CloseHandle(locked));
    // 現行static profileは内包画像3件。primary/derived・variant間の衝突も同じinventoryで検査する。
    const auto glb = root / "dog.glb";
    std::filesystem::copy_file("Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb", glb);
    auto model = Request(root, glb, "Model", "Cooked/model.nvpkg");
    model.Kind = "model";
    model.EntryTypeText = "Msh0";
    model.Format = "nvmesh.v0.mesh3d.pnt.u32.clustered";
    auto modelPlan = Plan(model);
    CHECK(modelPlan.Outputs.size() == 4);
    Good({a, modelPlan});
    changed = Request(root, aSource, "derived-overwrite", "Cooked/model.nvpkg.img0.nvpkg");
    Bad({modelPlan, Plan(changed)});
    auto variant = model;
    variant.Variant = "other";
    variant.PackagePath = root / "final/Cooked/model-other.nvpkg";
    Bad({modelPlan, Plan(variant)});
    changed =
        Request(root, aSource, modelPlan.Outputs[1].ExpectedIdentity.LogicalPath.c_str(), "Cooked/derived-key.nvpkg");
    changed.Kind = "texture";
    changed.EntryTypeText = "Tex0";
    changed.Format = "nvtex.v0.rgba8.srgb";
    const uint8_t ppm[] = {'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 1, 2, 3};
    Write(root / "tiny.ppm", ppm);
    changed.InputPath = root / "tiny.ppm";
    Bad({modelPlan, Plan(changed)});
    std::filesystem::copy("Assets/Models/Rendering3DTestSilverGltf", root / "final/model-source",
                          std::filesystem::copy_options::recursive);
    model.InputPath = root / "final/model-source/Rendering3DTestSilverGltf.gltf";
    const auto modelBytes = Read(model.InputPath);
    SetText text;
    text.append(reinterpret_cast<const char*>(modelBytes.data()), modelBytes.size());
    const auto images = std::strstr(text.c_str(), "\"images\"");
    CHECK(images);
    const auto imageEnd = std::strchr(images, ']');
    CHECK(imageEnd);
    const size_t prefix = static_cast<size_t>(imageEnd - text.c_str());
    SetText extended;
    extended.append(text.data(), prefix);
    extended.append(",{\"uri\":\"unused.bin\"}");
    extended.append(text.data() + prefix, text.size() - prefix);
    Write(model.InputPath, {reinterpret_cast<const uint8_t*>(extended.data()), extended.size()});
    TextFile(root / "final/model-source/unused.bin", "unused");
    modelPlan = Plan(model);
    CHECK(modelPlan.Outputs.size() == 1);
    for (const char* relative :
         {"model-source/Rendering3DTestSilverGltf.gltf", "model-source/Rendering3DTestSilverGltf.bin",
          "model-source/textures/silver_albedo.png", "model-source/textures/silver_normal-ogl.png",
          "model-source/textures/silver_arm.png", "model-source/unused.bin",
          "model-source/Rendering3DTestSilverGltf.gltf.import.json"})
    {
        const auto collision = Plan(Request(root, aSource, "dependency-output", relative));
        Bad({collision, modelPlan});
    }
    auto sidecar = model.InputPath;
    sidecar += ".import.json";
    TextFile(sidecar, "{\"version\":1}");
    Bad({modelPlan});
    modelPlan = Plan(model);
    Bad({Plan(Request(root, aSource, "present-sidecar", "model-source/Rendering3DTestSilverGltf.gltf.import.json")),
         modelPlan});
    CHECK(std::filesystem::remove(sidecar));
    // 観測後に入力が変われば全planの最終Stableで拒否する。
    struct Mutation
    {
        std::filesystem::path Path;
    } mutation{bSource};
    const auto mutate = [](void* data)
    {
        TextFile(static_cast<Mutation*>(data)->Path, "changed");
    };
    SetText error;
    API::CookOutputSetGuardStats stats;
    Plans pair = {a, b};
    CHECK(!API::ValidateCookOutputSetForTest(pair, {}, stats, error, mutate, &mutation));
    CHECK(!error.empty());
    TextFile(bSource, "b");
    Good(pair);
    // 同じ入力/manifest/controlは集約passで1回だけidentityを観測する。
    Plans many;
    many.reserve(MaximumCookSetPlans);
    for (size_t i = 0; i < MaximumCookSetPlans; ++i)
    {
        char key[50], package[70];
        std::snprintf(key, sizeof(key), "Many/%zu", i);
        std::snprintf(package, sizeof(package), "Many/%zu.nvpkg", i);
        many.push_back(Plan(Request(root, aSource, key, package)));
    }
    const auto large = Good(many, {root / "spec.json", root / "spec.json", root / "spec.json"});
    CHECK(large.LocatorOccurrences == 3 * MaximumCookSetPlans + 3 && large.UniqueLocators == MaximumCookSetPlans + 3);
    const auto first = many[0];
    many.push_back(first);
    Bad(many);
    Locators controls(MaximumCookSetProtectedOccurrences - 1, root / "spec.json");
    Good({a}, controls);
    controls.push_back(root / "spec.json");
    Bad({a}, controls);
    controls.clear();
    const auto longPath =
        root /
        std::filesystem::path(
            "long-segment-000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    auto deep = longPath;
    for (size_t i = 0; i < 15; ++i)
    {
        deep /= longPath.filename();
    }
    deep /= "file.json";
    controls.resize(12000, deep);
    CHECK(!ValidateCookOutputSet(Plans{a}, controls, error));
    CHECK(std::strstr(error.c_str(), "set_metadata_limit"));
    std::filesystem::path normalized;
    API::CookPathIdentity identity;
    auto components = std::filesystem::path(root.root_path());
    for (size_t i = 0; i < API::MaximumCookLocatorComponents; ++i)
    {
        components /= "p";
    }
    CHECK(API::NormalizeCookGuardLocator(components, normalized, error));
    components /= "p";
    CHECK(!API::NormalizeCookGuardLocator(components, normalized, error));
    for (const char* leaf : {"a.", "a ", "a:stream", "CON.txt", "COM1.txt", "a?/x"})
    {
        ObserveBad(root / leaf);
    }
    const wchar_t nul[] = {L'C', L':', L'\\', L'x', 0, L'y'};
    ObserveBad(std::filesystem::path(nul, nul + 6));
    const wchar_t invalid[] = {L'C', L':', L'\\', static_cast<wchar_t>(0xd800)};
    ObserveBad(std::filesystem::path(invalid, invalid + 4));
    CHECK(Read(aSource).size() == 1 && Read(root / "final/foreign.txt").size() == 9 &&
          !std::filesystem::exists(aRequest.ManifestPath));
    // 全てtest所有のrootだけを清掃する。guard自身は作成/書込/清掃を一切しない。
    std::filesystem::remove_all(root);
    std::puts(
        "COOK_OUTPUT_SET result=pass fresh_inventory_keys_components_file_ids_limits_linear_observations_read_only");
    return 0;
#endif
}
