// managed texture CLIの入力adapterと、source不在の明示復旧を検証する。
#include "Tools/AssetCook/CookManagedBootstrap.h"
#include "Tools/AssetCook/CookManagedUpdate.h"
#include "Tools/AssetCook/CookManagedUpdateTestAccess.h"
#include "Tools/AssetCook/CookManagedStoreInitialization.h"
#include "Tools/AssetCook/CookManagedTransactionIntent.h"
#include "Tools/AssetCook/CookCacheDecision.h"
#include "Text/JsonDocument.h"
#include "Tools/AssetCook/TextureAssetSetCook.h"
#include "Tools/AssetCook/NativeCookPath.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <utility>
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
namespace TextureCliTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace Access = NorvesLib::Tools::AssetCook::Detail;
    namespace Asset = NorvesLib::Core::Asset;
    using TestText = NorvesLib::Core::Container::AnsiString;
    using TestBytes = NorvesLib::Core::Container::VariableArray<uint8_t>;
    template <class T> using Array = NorvesLib::Core::Container::VariableArray<T>;
    using ByteView = NorvesLib::Core::Container::Span<const uint8_t>;
    using Point = Access::CookManagedUpdatePoint;
    using Probe = Access::CookManagedUpdateProbe;
    using Boot = CookManagedBootstrapResult;
    using Update = CookManagedUpdateResult;
    using Recovery = CookManagedRecoveryResult;
    ByteView BytesOf(const TestText& text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }
    TestBytes Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        CHECK(file);
        TestBytes bytes;
        char c;
        while (file.get(c))
        {
            bytes.push_back(static_cast<uint8_t>(c));
        }
        CHECK(file.eof());
        return bytes;
    }
    void Write(const std::filesystem::path& path, ByteView bytes)
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
    void Write(const std::filesystem::path& path, const char* text)
    {
        Write(path, {reinterpret_cast<const uint8_t*>(text), std::strlen(text)});
    }
#if defined(_WIN32)
    struct NativeId
    {
        uint64_t Volume = 0;
        NorvesLib::Core::Container::FixedArray<uint8_t, 16> File =
            NorvesLib::Core::Container::FixedArray<uint8_t, 16>(uint8_t{0});
    };
    NativeId Identity(const std::filesystem::path& path)
    {
        HANDLE h =
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        FILE_ID_INFO info{};
        CHECK(GetFileInformationByHandleEx(h, FileIdInfo, &info, sizeof(info)));
        CHECK(CloseHandle(h));
        NativeId id;
        id.Volume = info.VolumeSerialNumber;
        std::memcpy(id.File.data(), info.FileId.Identifier, 16);
        return id;
    }
    bool Same(const NativeId& a, const NativeId& b)
    {
        return a.Volume == b.Volume && std::memcmp(a.File.data(), b.File.data(), 16) == 0;
    }
    struct SnapshotNode
    {
        std::filesystem::path Relative;
        NativeId Id;
        uint64_t Size = 0, Hash = 0;
        bool bDirectory = false;
    };
    Array<SnapshotNode> Snapshot(const std::filesystem::path& root)
    {
        Array<SnapshotNode> out;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            SnapshotNode row;
            row.Relative = entry.path().lexically_relative(root);
            row.Id = Identity(entry.path());
            row.bDirectory = entry.is_directory();
            if (!row.bDirectory)
            {
                const auto bytes = Read(entry.path());
                row.Size = bytes.size();
                row.Hash = Asset::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
            }
            out.push_back(std::move(row));
        }
        std::sort(out.begin(), out.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.Relative.native() < b.Relative.native();
                  });
        return out;
    }
    void Unchanged(const Array<SnapshotNode>& a, const Array<SnapshotNode>& b)
    {
        CHECK(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].Relative == b[i].Relative && Same(a[i].Id, b[i].Id) && a[i].Size == b[i].Size &&
                  a[i].Hash == b[i].Hash && a[i].bDirectory == b[i].bDirectory);
        }
    }
    TestText PathText(const std::filesystem::path& path)
    {
        TestText out;
        CHECK(Access::EncodeCookPathUtf8(path, out));
        return out;
    }
    void Call(Array<TestText> values, int expected)
    {
        Array<const char*> args;
        args.push_back("AssetCook");
        for (const auto& value : values)
        {
            args.push_back(value.c_str());
        }
        int code = -1;
        CHECK(RunTextureAssetSetCommand(static_cast<int>(args.size()), args.data(), code));
        CHECK(code == expected);
    }
    struct Fixture
    {
        std::filesystem::path Base, Runtime, Spec, Source;
        Array<SingleAssetCookRequest> Assets;
        TestText SpecText =
            R"json({"version":1,"name":"cli","package_root":"Cooked/Probe","default_variant":"default","textures":[{"logical_path":"Textures/a.ppm","source_path":"a.ppm","format":"nvtex.v0.rgba8.linear","package_name":"a.nvpkg","entry_name":"a.nvtex"},{"logical_path":"Textures/b.ppm","source_path":"b.ppm","format":"nvtex.v0.rgba8.linear","package_name":"b.nvpkg","entry_name":"b.nvtex"}]})json";
        explicit Fixture(const std::filesystem::path& base)
            : Base(base), Runtime(base / "Runtime"), Spec(base / "spec.json"), Source(base / "source")
        {
            CHECK(std::filesystem::create_directory(Base));
            CHECK(std::filesystem::create_directory(Source));
            Write(Spec, BytesOf(SpecText));
            const char* image = "P6\n2 2\n255\nabcdefghijkl";
            Write(Source / "a.ppm", image);
            Write(Source / "b.ppm", image);
            for (const char* name : {"a", "b"})
            {
                SingleAssetCookRequest asset;
                TestText leaf = name;
                leaf.append(".ppm");
                asset.InputPath = Source / leaf.c_str();
                asset.LogicalPath = "Textures/";
                asset.LogicalPath.append(leaf);
                leaf = name;
                leaf.append(".nvpkg");
                asset.PackagePath = Runtime / "Cooked/Probe" / leaf.c_str();
                asset.ManifestPath = Runtime / "manifest.json";
                asset.EntryName = name;
                asset.EntryName.append(".nvtex");
                asset.Kind = "texture";
                asset.EntryTypeText = "Tex0";
                asset.Format = "nvtex.v0.rgba8.linear";
                asset.Variant = "default";
                Assets.push_back(std::move(asset));
            }
        }
        Array<TestText> Arguments() const
        {
            return {"--asset-set",     PathText(Spec),  "--runtime-root",
                    PathText(Runtime), "--source-root", PathText(Source)};
        }
        TextureAssetSetCookRequest ServiceRequest() const
        {
            return {Spec, Source, Runtime, {}};
        }
        void Create()
        {
            Call(Arguments(), 0);
        }
        void Change()
        {
            auto bytes = Read(Source / "a.ppm");
            bytes.back() ^= 0x55;
            Write(Source / "a.ppm", bytes);
        }
        void Interrupt(Point at)
        {
            struct Stop
            {
                Point At;
                bool bReached = false;
            } stop{at};
            Probe probe;
            probe.Context = &stop;
            probe.Checkpoint =
                [](Point point, const std::filesystem::path&, const std::filesystem::path&, size_t, void* context)
            {
                auto& value = *static_cast<Stop*>(context);
                if (point != value.At)
                {
                    return true;
                }
                value.bReached = true;
                return false;
            };
            CookManagedUpdateRequest request{{Spec, Runtime, "manifest.json"}, Assets, BytesOf(SpecText), 1};
            CookManagedUpdateOutcome out;
            out.ClaimId = "held";
            TestText error;
            const auto result = Access::UpdateCookManagedAssetSetForTest(request, probe, out, error);
            if (result != Update::NeedsRecovery)
            {
                std::fprintf(stderr, "cli fixture interrupt: %s\n", error.c_str());
            }
            CHECK(result == Update::NeedsRecovery && stop.bReached && out.ClaimId == "held");
        }
    };
    void MixedBatch(const std::filesystem::path& root)
    {
        Fixture f(root / "mixed-v2");
        f.SpecText =
            R"json({"version":2,"name":"mixed","package_root":"Cooked/Probe","default_variant":"default","assets":[{"kind":"texture","logical_path":"Textures/a.ppm","source_path":"a.ppm","format":"nvtex.v0.rgba8.linear","package_name":"a.nvpkg","entry_name":"a.nvtex"},{"kind":"raw","logical_path":"Data/config","source_path":"config.bin","format":"raw.v0","package_name":"config.nvpk","entry_name":"config"}]})json";
        Write(f.Spec, BytesOf(f.SpecText));
        Write(f.Source / "config.bin", "initial");
        auto request = f.ServiceRequest();
        CookBatchReport report;
        request.Report = &report;
        CookManagedBootstrapOutcome out;
        TestText error;
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::Created);
        CHECK(report.Assets.size() == 2 && report.Assets[0].Metrics.TextureBytes == 20 &&
              report.Assets[1].Metrics.TextureBytes == 0);
        CHECK(std::filesystem::is_regular_file(report.ReportDirectory / "cook_report.json"));
        Fixture parallel(root / "mixed-v2-parallel");
        Write(parallel.Spec, BytesOf(f.SpecText));
        Write(parallel.Source / "config.bin", "initial");
        auto parallelArgs = parallel.Arguments();
        parallelArgs.push_back("--jobs=4");
        Call(parallelArgs, 0);
        for (const auto* relative : {"manifest.json", "Cooked/Probe/a.nvpkg", "Cooked/Probe/config.nvpk"})
        {
            CHECK(Read(f.Runtime / relative) == Read(parallel.Runtime / relative));
        }
        auto invalidJobs = parallel.Arguments();
        invalidJobs.push_back("--jobs=0");
        Call(invalidJobs, 1);
        invalidJobs.back() = "--jobs=65";
        Call(invalidJobs, 1);
        invalidJobs.back() = "--jobs=4";
        invalidJobs.push_back("--jobs=1");
        Call(invalidJobs, 1);
        request.Jobs = 4;
        const auto generation = out.StateGeneration;
        const auto texture = Read(f.Runtime / "Cooked/Probe/a.nvpkg");
        const auto manifest = Read(f.Runtime / "manifest.json");
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::NoChange);
        CHECK(out.StateGeneration == generation && report.Assets[0].bSkipped && report.Assets[1].bSkipped);
        CHECK(Read(f.Runtime / "manifest.json") == manifest);
        Write(f.Source / "config.bin", "changed raw source");
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::Updated);
        CHECK(out.StateGeneration == generation + 1 && report.Assets[0].bSkipped && !report.Assets[1].bSkipped);
        CHECK(Read(f.Runtime / "Cooked/Probe/a.nvpkg") == texture);
        const auto published = Read(f.Runtime / "manifest.json");
        const auto at = f.SpecText.find("\"assets\"");
        CHECK(at != TestText::npos);
        const auto limited = f.SpecText.substr(0, at) + "\"budgets\":{\"defaults\":{\"texture\":{\"max_bytes\":0}}}," +
                             f.SpecText.substr(at);
        Write(f.Spec, BytesOf(limited));
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::BudgetExceeded);
        CHECK(report.BudgetErrors == 1 && Read(f.Runtime / "manifest.json") == published);
        Call(f.Arguments(), 2);
        auto warning = f.Arguments();
        warning.push_back("--warn-budget");
        Call(warning, 0);
        CHECK(Read(f.Runtime / "manifest.json") == published);
        // 固定report名にある既存ファイルは、新しいrunの出力で書き換えない。
        auto reports = f.Runtime;
        reports += L".reports";
        Write(reports / "cook_report.json", "unowned-held");
        Call(warning, 0);
        CHECK((Read(reports / "cook_report.json") ==
               TestBytes{'u', 'n', 'o', 'w', 'n', 'e', 'd', '-', 'h', 'e', 'l', 'd'}));

        // v2は所属資産の増減を許可するが、外したpackageを削除も再採用もしない。
        const auto rawBeforeRemoval = Read(f.Runtime / "Cooked/Probe/config.nvpk");
        const auto rawEntry = f.SpecText.find(",{\"kind\":\"raw\"");
        CHECK(rawEntry != TestText::npos);
        const auto textureOnly = f.SpecText.substr(0, rawEntry) + "]}";
        Write(f.Spec, BytesOf(textureOnly));
        const auto beforePrune = Read(f.Runtime / "manifest.json");
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::NoChange);
        CHECK(Read(f.Runtime / "manifest.json") == beforePrune);
        request.bPrune = true;
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::Updated);
        CHECK(report.Assets.size() == 1 && report.Assets[0].bSkipped);
        CHECK(Read(f.Runtime / "Cooked/Probe/config.nvpk") == rawBeforeRemoval);
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::NoChange);
        Write(f.Spec, BytesOf(f.SpecText));
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::Error);
        CHECK(error.find("new_output_requires_absence") != TestText::npos);
        auto added = f.SpecText;
        const auto packageAt = added.find("config.nvpk");
        CHECK(packageAt != TestText::npos);
        added = added.substr(0, packageAt) + "config-new.nvpk" + added.substr(packageAt + std::strlen("config.nvpk"));
        Write(f.Spec, BytesOf(added));
        CHECK(CookTextureAssetSetWithOutcome(request, out, error) == TextureAssetSetCookResult::Updated);
        CHECK(report.Assets[0].bSkipped && !report.Assets[1].bSkipped);
        CHECK(Read(f.Runtime / "Cooked/Probe/config.nvpk") == rawBeforeRemoval);
        CHECK(std::filesystem::is_regular_file(f.Runtime / "Cooked/Probe/config-new.nvpk"));

        Fixture blocked(root / "budget-report-blocked");
        Write(blocked.Spec, BytesOf(limited));
        Write(blocked.Source / "config.bin", "raw");
        auto reportBlock = blocked.Runtime;
        reportBlock += L".reports";
        Write(reportBlock, "held");
        Call(blocked.Arguments(), 2);
        CHECK(!std::filesystem::exists(blocked.Runtime));
        CHECK((Read(reportBlock) == TestBytes{'h', 'e', 'l', 'd'}));
    }
    void BudgetWithoutReport(const std::filesystem::path& root)
    {
        Fixture f(root / "budget-no-report");
        f.Create();
        const auto before = Snapshot(f.Runtime);
        CookManagedBootstrapRequest request;
        request.Owner = {f.Spec, f.Runtime, "manifest.json"};
        request.Assets = f.Assets;
        request.ExpectedSpecBytes = BytesOf(f.SpecText);
        request.TotalBudget.MaxTextureBytes = 0;
        CHECK(request.Report == nullptr);
        CookManagedBootstrapOutcome out;
        TestText error;
        CHECK(UpdateCookManagedAssetSet(request, out, error) == CookManagedUpdateResult::Error);
        CHECK(error.find("budget_exceeded") != TestText::npos);
        Unchanged(before, Snapshot(f.Runtime));
    }
    void Run(const std::filesystem::path& root)
    {
        MixedBatch(root);
        BudgetWithoutReport(root);
        {
            Fixture f(root / "service");
            CookManagedBootstrapOutcome out;
            TestText error;
            CHECK(CookTextureAssetSetWithOutcome(f.ServiceRequest(), out, error) == TextureAssetSetCookResult::Created);
            CHECK(out.StateGeneration == 1 && out.IndexGeneration == 2);
            const auto initial = Snapshot(f.Base);
            CHECK(CookTextureAssetSetWithOutcome(f.ServiceRequest(), out, error) ==
                  TextureAssetSetCookResult::NoChange);
            CHECK(out.StateGeneration == 1 && out.IndexGeneration == 2);
            Unchanged(initial, Snapshot(f.Base));
            CHECK(CookTextureAssetSet(f.ServiceRequest(), error));
            Unchanged(initial, Snapshot(f.Base));
            f.Change();
            const auto skipId = Identity(f.Assets[1].PackagePath);
            const auto skipBytes = Read(f.Assets[1].PackagePath);
            CHECK(CookTextureAssetSetWithOutcome(f.ServiceRequest(), out, error) == TextureAssetSetCookResult::Updated);
            CHECK(out.StateGeneration == 2 && out.IndexGeneration == 3);
            CHECK(Same(skipId, Identity(f.Assets[1].PackagePath)) && Read(f.Assets[1].PackagePath) == skipBytes);
        }
        {
            Fixture f(root / "directory-spellings");
            f.Create();
            const auto initial = Snapshot(f.Base);
            const auto cwd = std::filesystem::current_path();
            std::filesystem::current_path(f.Source);
            for (const char* spelling : {".", "./", "../source/", "../source/."})
            {
                auto args = f.Arguments();
                args.back() = spelling;
                Call(args, 0);
                Unchanged(initial, Snapshot(f.Base));
            }
            std::filesystem::current_path(cwd);
            std::filesystem::path resolved;
            TestText error;
            CHECK(!Access::ResolveTextureCliPath("CON/../safe", f.Base, resolved, error));
            CHECK(!Access::ResolveTextureCliPath("bad./../safe", f.Base, resolved, error));
            CHECK(!Access::ResolveTextureCliPath("C:relative", f.Base, resolved, error));
        }
        {
            Fixture f(root / "bom");
            TestBytes bytes{0xef, 0xbb, 0xbf};
            for (char c : f.SpecText)
            {
                bytes.push_back(static_cast<uint8_t>(c));
            }
            Write(f.Spec, bytes);
            f.Create();
            const auto initial = Snapshot(f.Base);
            Call(f.Arguments(), 0);
            Unchanged(initial, Snapshot(f.Base));
        }
        {
            Fixture f(root / "legacy-root");
            CHECK(std::filesystem::create_directory(f.Runtime));
            Write(f.Runtime / "keep", "unclaimed");
            const auto initial = Snapshot(f.Base);
            CookManagedBootstrapOutcome out;
            out.ClaimId = "held";
            TestText error;
            CHECK(CookTextureAssetSetWithOutcome(f.ServiceRequest(), out, error) ==
                  TextureAssetSetCookResult::Conflict);
            CHECK(out.ClaimId == "held");
            Unchanged(initial, Snapshot(f.Base));
        }
        for (bool bCommit : {false, true})
        {
            Fixture f(root / (bCommit ? "recover-commit" : "recover-rollback"));
            f.Create();
            const auto before = Snapshot(f.Runtime);
            const auto rootId = Identity(f.Runtime);
            const auto changedId = Identity(f.Assets[0].PackagePath);
            const auto changedBytes = Read(f.Assets[0].PackagePath);
            const auto skipId = Identity(f.Assets[1].PackagePath);
            const auto skipBytes = Read(f.Assets[1].PackagePath);
            f.Change();
            f.Interrupt(bCommit ? Point::ReceiptCommitted : Point::IndexPublished);
            Write(f.Spec, "{");
            std::filesystem::rename(f.Source, f.Base / "source-held");
            const auto pending = Snapshot(f.Base);
            // JSONが壊れsourceが不在でも、通常経路は先にNeedsRecoveryとして停止する。
            CookManagedBootstrapOutcome out;
            out.ClaimId = "held";
            TestText error;
            CHECK(CookTextureAssetSetWithOutcome(f.ServiceRequest(), out, error) ==
                  TextureAssetSetCookResult::NeedsRecovery);
            CHECK(out.ClaimId == "held");
            Call(f.Arguments(), 1);
            Unchanged(pending, Snapshot(f.Base));
            std::filesystem::rename(f.Spec, f.Base / "spec-held");
            // 明示されたparent workspaceのpendingなので、別の未作成leafでも同じ対象を復旧する。
            Call({"--recover", "--runtime-root", PathText(f.Base / "SiblingLocator")}, 0);
            CHECK(!std::filesystem::exists(f.Source) && !std::filesystem::exists(f.Spec));
            CHECK(!std::filesystem::exists(f.Base / ".norves-assetcook/pending"));
            if (!bCommit)
            {
                Unchanged(before, Snapshot(f.Runtime));
            }
            else
            {
                CHECK(!Same(changedId, Identity(f.Assets[0].PackagePath)) &&
                      changedBytes != Read(f.Assets[0].PackagePath));
                CHECK(Same(skipId, Identity(f.Assets[1].PackagePath)) && skipBytes == Read(f.Assets[1].PackagePath));
                CHECK(Same(rootId, Identity(f.Runtime)));
            }
            const auto stable = Snapshot(f.Base);
            Call({"--recover", "--runtime-root", PathText(f.Runtime)}, 0);
            Unchanged(stable, Snapshot(f.Base));
            for (const auto& args : Array<Array<TestText>>{
                     {"--recover"},
                     {"--recover=yes", "--runtime-root", PathText(f.Runtime)},
                     {"--recover", "--recover", "--runtime-root", PathText(f.Runtime)},
                     {"--recover", "--runtime-root", PathText(f.Runtime), "--source-root", PathText(f.Source)},
                     {"--recover", "--runtime-root", PathText(f.Runtime), "--manifest", "manifest.json"},
                     {"--recover", "--runtime-root", PathText(f.Runtime), "--asset-set", PathText(f.Spec)}})
            {
                Call(args, 1);
                Unchanged(stable, Snapshot(f.Base));
            }
        }
        {
            Fixture f(root / "bad-pending");
            f.Create();
            f.Change();
            f.Interrupt(Point::PendingPublished);
            Write(f.Base / ".norves-assetcook/pending/unknown", "keep");
            std::filesystem::rename(f.Spec, f.Base / "spec-held");
            std::filesystem::rename(f.Source, f.Base / "source-held");
            const auto before = Snapshot(f.Base);
            Call({"--recover", "--runtime-root", PathText(f.Runtime)}, 1);
            Unchanged(before, Snapshot(f.Base));
        }
        {
            const auto empty = root / "no-store";
            CHECK(std::filesystem::create_directory(empty));
            const auto before = Snapshot(empty);
            Call({"--recover", "--runtime-root", PathText(empty / "Runtime")}, 1);
            Unchanged(before, Snapshot(empty));
        }
        std::puts(
            "TEXTURE_MANAGED_CLI result=pass adapter_nochange_update_explicit_workspace_recovery_source_absent_pending_refusal_raw_paths_bom");
    }
#endif
} // 名前空間 TextureCliTest
int main()
{
#if defined(_WIN32)
    wchar_t temp[32768]{};
    const auto length = GetTempPathW(32768, temp);
    CHECK(length && length < 32768);
    wchar_t name[128]{};
    std::swprintf(name, 128, L"NorvesManagedTextureCli-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    const auto root = std::filesystem::path(temp) / name;
    CHECK(std::filesystem::create_directory(root));
    TextureCliTest::Run(root);
    CHECK(std::filesystem::remove_all(root) > 0);
    return 0;
#else
    return 125;
#endif
}
