// final始点の依存と要求を保持したまま、stageだけをcookして捕捉する契約を検証する。
#include "Tools/AssetCook/CookOutputPlan.h"
#include "Tools/AssetCook/CookOwnedState.h"
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
namespace PlanTest
{
    using namespace NorvesLib::Tools::AssetCook;
    using PlanText = NorvesLib::Core::Container::AnsiString;
    using PlanBytes = NorvesLib::Core::Container::VariableArray<uint8_t>;
    using Manifest = NorvesLib::Core::Asset::AssetManifest;
    PlanBytes Read(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        CHECK(f);
        PlanBytes b;
        char c;
        while (f.get(c))
        {
            b.push_back(static_cast<uint8_t>(c));
        }
        CHECK(f.eof());
        return b;
    }
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
    Manifest Parse(NorvesLib::Core::Container::Span<const uint8_t> bytes)
    {
        NorvesLib::Core::Container::String text;
        text.reserve(bytes.size());
        for (const auto c : bytes)
        {
            text.push_back(static_cast<NorvesLib::Core::Container::String::value_type>(c));
        }
        Manifest out;
        CHECK(out.LoadFromJsonText(text));
        return out;
    }
    Manifest Load(const std::filesystem::path& path)
    {
        return Parse(Read(path));
    }
    SingleAssetCookRequest Raw(const std::filesystem::path& root, const std::filesystem::path& source)
    {
        SingleAssetCookRequest r;
        r.InputPath = source;
        r.PackagePath = root / "final/Cooked/raw.nvpkg";
        r.ManifestPath = root / "final/manifest.json";
        r.LogicalPath = "Test/raw";
        r.Kind = "raw";
        r.EntryName = "__asset__";
        r.EntryTypeText = "Raw";
        r.Format = "raw.v0";
        r.Variant = "default";
        return r;
    }
    CookPreparedPlan Final(const SingleAssetCookRequest& request, uint64_t revision = 7)
    {
        CookPreparedPlan plan;
        PlanText error;
        CHECK(PrepareCookOutputPlan(request, revision, nullptr, plan, error));
        return plan;
    }
    CookPreparedPlan Stage(const CookPreparedPlan& final, const std::filesystem::path& path)
    {
        CHECK(std::filesystem::create_directory(path));
        CookPreparedPlan plan;
        PlanText error;
        CHECK(PrepareCookStagingPlan(final, path, plan, error));
        return plan;
    }
    Manifest Cook(const CookPreparedPlan& stage)
    {
        PlanText error;
        if (!CookSingleAsset(stage.Context.Request, error))
        {
            std::fprintf(stderr, "cook: %s\n", error.c_str());
            CHECK(false);
        }
        return Load(stage.Context.Request.ManifestPath);
    }
    void RefuseCapture(const CookPreparedPlan& final, const CookPreparedPlan& stage, const Manifest& manifest)
    {
        CookOutputRecord held;
        held.DependencyFingerprint = 123;
        held.CookerRevision = 456;
        PlanText error;
        CHECK(!CaptureStagedCookOutputRecord(final, stage, manifest, held, error));
        CHECK(!error.empty() && held.DependencyFingerprint == 123 && held.CookerRevision == 456);
    }
    void RefuseStage(const CookPreparedPlan& final, const std::filesystem::path& path)
    {
        CookPreparedPlan held;
        held.Context.Request.LogicalPath = "held";
        held.Context.Dependencies.Fingerprint = 123;
        PlanText error;
        CHECK(!PrepareCookStagingPlan(final, path, held, error));
        CHECK(!error.empty() && held.Context.Request.LogicalPath == "held" &&
              held.Context.Dependencies.Fingerprint == 123);
    }
    void Freshness(const std::filesystem::path& root, const SingleAssetCookRequest& request,
                   const std::filesystem::path& dependency, const char* label)
    {
        const auto final = Final(request);
        const auto stage = Stage(final, root / label);
        const auto before = Cook(stage);
        CookOutputRecord record;
        PlanText error;
        CHECK(CaptureStagedCookOutputRecord(final, stage, before, record, error));
        const auto original = Read(dependency);
        auto changed = original;
        changed.push_back(0);
        Write(dependency, changed);
        const auto newer = Cook(stage);
        RefuseCapture(final, stage, newer);
        const auto freshFinal = Final(request);
        RefuseCapture(final, stage, before);
        Write(dependency, original);
        const auto restored = Cook(stage);
        CHECK(CaptureStagedCookOutputRecord(final, stage, restored, record, error));
        RefuseCapture(freshFinal, stage, restored);
    }
} // namespace PlanTest
int main()
{
    using namespace PlanTest;
#if defined(_WIN32)
    char name[100];
    std::snprintf(name, sizeof(name), "norves-plan-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
#else
    const char* name = "norves-plan-native";
#endif
    const auto root = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(root));
    const auto source = root / "raw.bin";
    TextFile(source, "abc");
    const auto request = Raw(root, source);
    const auto timestamp = std::filesystem::last_write_time(source);
    const auto final = Final(request);
    CHECK(!std::filesystem::exists(root / "final") && std::filesystem::last_write_time(source) == timestamp);
    CHECK(final.Outputs.size() == 1 && final.Outputs[0].TargetPath == request.PackagePath &&
          final.Outputs[0].ExpectedIdentity.CookedPackage == "Cooked/raw.nvpkg");
    RefuseStage(final, root / "not-created-stage");
    const auto stage = Stage(final, root / "stage");
    CHECK(std::filesystem::is_empty(root / "stage") && !std::filesystem::exists(root / "final"));
    CHECK(stage.Context.Request.InputPath == final.Context.Request.InputPath &&
          stage.Context.Dependencies.Fingerprint == final.Context.Dependencies.Fingerprint);
    const auto manifest = Cook(stage);
    CookOutputRecord record;
    PlanText error;
    CHECK(CaptureStagedCookOutputRecord(final, stage, manifest, record, error));
    CHECK(!std::filesystem::exists(root / "final"));
    CookOwnedState state;
    state.Binding.OwnerId = "0123456789abcdef0123456789abcdef";
    state.Binding.RuntimeRootIdentity = (root / "final").generic_string().c_str();
    state.Binding.ManifestName = "manifest.json";
    CookOwnedRecord owned;
    const auto& primary = record.Outputs[0].Reference;
    owned.PrimaryKey = {primary.LogicalPath, primary.Kind, primary.Variant};
    owned.Record = record;
    state.Records.push_back(owned);
    PlanText saved;
    CHECK(SerializeCookOwnedState(state, saved, error));
    CHECK(!std::strstr(saved.c_str(), (root / "stage").generic_string().c_str()));
    CookOwnedState loaded;
    CHECK(ParseCookOwnedState({reinterpret_cast<const uint8_t*>(saved.data()), saved.size()}, state.Binding, loaded,
                              error));
    CHECK(loaded.Records[0].Record.DependencyFingerprint == final.Context.Dependencies.Fingerprint);
    RefuseStage(final, root / "stage");
    CHECK(std::filesystem::create_directory(root / "final"));
    RefuseStage(final, root / "final");
    CHECK(std::filesystem::create_directory(root / "final/inside"));
    RefuseStage(final, root / "final/inside");
    RefuseStage(final, root);
#if defined(_WIN32)
    wchar_t shortFinal[32768]{};
    const DWORD shortCount = GetShortPathNameW((root / "final").c_str(), shortFinal, 32768);
    CHECK(shortCount > 0 && shortCount < 32768);
    RefuseStage(final, std::filesystem::path(shortFinal) / "inside");
    const auto link = root / "stage-link", linkTarget = root / "empty-target";
    CHECK(std::filesystem::create_directory(linkTarget));
    char junctionCommand[4096];
    std::snprintf(junctionCommand, sizeof(junctionCommand), "cmd /c mklink /J \"%s\" \"%s\" >nul",
                  link.string().c_str(), linkTarget.string().c_str());
    CHECK(std::system(junctionCommand) == 0);
    RefuseStage(final, link);
    CHECK(std::filesystem::is_empty(linkTarget));
    CHECK(std::filesystem::remove(link));

    std::printf("staged_short_alias_distinct=%d\n",
                std::filesystem::path(shortFinal).native() != (root / "final").native());
#endif

    // 元planの意味を改めた別の有効planをcookしても、旧finalの成果物としては受理しない。
    for (unsigned i = 0; i < 6; ++i)
    {
        auto changed = request;
        if (i == 0)
        {
            changed.LogicalPath = "Test/other";
        }
        if (i == 1)
        {
            changed.Variant = "other";
        }
        if (i == 2)
        {
            changed.EntryName = "__other__";
        }
        if (i == 3)
        {
            changed.EntryTypeText = "Cust";
        }
        if (i == 4)
        {
            TextFile(root / "other.bin", "abc");
            changed.InputPath = root / "other.bin";
        }
        const auto otherFinal = Final(changed, i == 5 ? 8 : 7);
        char label[40];
        std::snprintf(label, sizeof(label), "semantic-%u", i);
        const auto otherStage = Stage(otherFinal, root / label);
        const auto otherManifest = Cook(otherStage);
        RefuseCapture(final, otherStage, otherManifest);
    }
    // sourceの新しいbytesを実cookしても、準備済みの古い始点を置き換えない。
    TextFile(source, "new source");
    const auto newer = Cook(stage);
    RefuseCapture(final, stage, newer);
    CHECK(std::filesystem::create_directory(root / "stale-prepare"));
    RefuseStage(final, root / "stale-prepare");
    CHECK(std::filesystem::is_empty(root / "stale-prepare"));
    TextFile(source, "abc");
    const auto restored = Cook(stage);
    CHECK(CaptureStagedCookOutputRecord(final, stage, restored, record, error));
    auto tampered = stage;
    tampered.Outputs[0].ExpectedIdentity.CookedPackage = "Cooked/other.nvpkg";
    RefuseCapture(final, tampered, restored);
    tampered = stage;
    tampered.Outputs[0].TargetPath = root / "elsewhere";
    RefuseCapture(final, tampered, restored);
    const char* empty = "{\"version\":1,\"assets\":[]}";
    RefuseCapture(final, stage, Parse({reinterpret_cast<const uint8_t*>(empty), std::strlen(empty)}));
    // 単体writerに別keyを追記させ、有効だが余分なrowを持つfragmentを拒否する。
    auto extra = stage.Context.Request;
    extra.InputPath = source;
    extra.LogicalPath = "Test/foreign";
    extra.PackagePath = stage.Context.Request.PackagePath.parent_path() / "foreign.nvpkg";
    CHECK(CookSingleAsset(extra, error));
    const auto extraManifest = Load(stage.Context.Request.ManifestPath);
    CHECK(extraManifest.GetReferenceCount() == 2);
    RefuseCapture(final, stage, extraManifest);
    const auto glb = root / "dog.glb";
    std::filesystem::copy_file("Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb", glb);
    auto model = Raw(root, glb);
    model.Kind = "model";
    model.EntryTypeText = "Msh0";
    model.Format = "nvmesh.v0.mesh3d.pnt.u32.clustered";
    model.PackagePath = root / "model-final/Cooked/dog.nvpkg";
    model.ManifestPath = root / "model-final/manifest.json";
    auto sidecar = glb;
    sidecar += ".import.json";
    TextFile(sidecar, "{\"version\":1}");
    const auto modelFinal = Final(model);
    CHECK(modelFinal.Outputs.size() == 4);
    const auto modelStage = Stage(modelFinal, root / "model-stage");
    const auto modelManifest = Cook(modelStage);
    CHECK(CaptureStagedCookOutputRecord(modelFinal, modelStage, modelManifest, record, error));
    TextFile(sidecar, "{ \"version\":1 }\n");
    const auto recooked = Cook(modelStage);
    CHECK(recooked.Resolve(model.LogicalPath, NorvesLib::Core::Asset::AssetKind::Model).Reference.SourceHash ==
          modelManifest.Resolve(model.LogicalPath, NorvesLib::Core::Asset::AssetKind::Model).Reference.SourceHash);
    RefuseCapture(modelFinal, modelStage, recooked);
    CHECK(std::filesystem::remove(sidecar));
    const auto absentFinal = Final(model);
    const auto absentStage = Stage(absentFinal, root / "absent-sidecar");
    TextFile(sidecar, "{\"version\":1}");
    RefuseCapture(absentFinal, absentStage, Cook(absentStage));
    CHECK(std::filesystem::remove(sidecar));
    std::filesystem::copy("Assets/Models/Rendering3DTestSilverGltf", root / "external",
                          std::filesystem::copy_options::recursive);
    model.InputPath = root / "external/Rendering3DTestSilverGltf.gltf";
    model.PackagePath = root / "external-final/Cooked/model.nvpkg";
    model.ManifestPath = root / "external-final/manifest.json";
    CHECK(Final(model).Outputs.size() == 1);
    Freshness(root, model, root / "external/Rendering3DTestSilverGltf.bin", "external-buffer");
    Freshness(root, model, root / "external/textures/silver_albedo.png", "external-image");
    CHECK(!std::filesystem::exists(root / "external-final") && !std::filesystem::exists(root / "model-final"));
    CHECK(Read(source).size() == 3);
    std::filesystem::remove_all(root);
    std::puts(
        "COOK_STAGED_PLAN result=pass authoritative_inventory_original_snapshot_checked_mapping_exact_fragment_hold");
    return 0;
}
