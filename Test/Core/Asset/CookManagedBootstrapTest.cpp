// 実texture cookとnative公開・source非依存復旧を、実process終了を含めて反証する。
#include "Tools/AssetCook/CookManagedBootstrap.h"
#include "Tools/AssetCook/CookManagedBootstrapTestAccess.h"
#include "Tools/AssetCook/CookManagedStoreInitialization.h"
#include "Tools/AssetCook/CookManagedTransactionIntent.h"
#include "Tools/AssetCook/CookCacheDecision.h"
#include "Text/JsonDocument.h"
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
namespace BootstrapTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace Access = NorvesLib::Tools::AssetCook::Detail;
    namespace Asset = NorvesLib::Core::Asset;
    using TestText = NorvesLib::Core::Container::AnsiString;
    using TestBytes = NorvesLib::Core::Container::VariableArray<uint8_t>;
    template <class T> using Array = NorvesLib::Core::Container::VariableArray<T>;
    using ByteView = NorvesLib::Core::Container::Span<const uint8_t>;
    using Point = Access::CookManagedBootstrapPoint;
    using Probe = Access::CookManagedBootstrapProbe;
    using Boot = CookManagedBootstrapResult;
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
    struct Fixture
    {
        std::filesystem::path Base, Runtime, Spec, Source, Store;
        TestText SpecText = "{\"version\":1,\"fixture\":\"bootstrap\"}";
        Array<SingleAssetCookRequest> Assets;
        CookOwnerResolveRequest Owner;
        explicit Fixture(const std::filesystem::path& base, bool bCreate = true)
            : Base(base), Runtime(base / "Runtime"), Spec(base / "spec.json"), Source(base / "source.ppm"),
              Store(base / ".norves-assetcook")
        {
            if (bCreate)
            {
                CHECK(std::filesystem::create_directory(Base));
                Write(Spec, BytesOf(SpecText));
                TestText ppm = "P6\n2 2\n255\n";
                for (int i = 0; i < 12; ++i)
                {
                    ppm.push_back(static_cast<char>(20 + i * 13));
                }
                Write(Source, BytesOf(ppm));
            }
            Owner = {Spec, Runtime, "manifest.json"};
            for (const char* name : {"a", "b"})
            {
                SingleAssetCookRequest request;
                request.InputPath = Source;
                TestText package = "Cooked/";
                package.append(name);
                package.append(".nvpkg");
                request.PackagePath = Runtime / package.c_str();
                request.ManifestPath = Runtime / "manifest.json";
                request.Kind = "texture";
                request.LogicalPath = "Test/";
                request.LogicalPath.append(name);
                request.EntryName = "__asset__";
                request.EntryTypeText = "Tex0";
                request.Format = name[0] == 'a' ? "nvtex.v0.rgba8.srgb" : "nvtex.v0.rg8.linear";
                request.Variant = "default";
                Assets.push_back(std::move(request));
            }
            if (bCreate)
            {
                CookManagedStoreObservation out;
                TestText error;
                CHECK(InitializeNewCookManagedStore(Owner, out, error) ==
                      CookManagedStoreInitializationResult::Created);
            }
        }
        CookManagedBootstrapRequest Request() const
        {
            return {Owner, Assets, BytesOf(SpecText), 7};
        }
        Boot Run(CookManagedBootstrapOutcome& out, TestText& error, const Probe* probe = nullptr) const
        {
            auto request = Request();
            return probe ? Access::BootstrapCookManagedAssetSetForTest(request, *probe, out, error)
                         : BootstrapCookManagedAssetSet(request, out, error);
        }
    };
    CookManagedStoreIndex Index(const Fixture& f)
    {
        TestText error;
        const auto header = Read(f.Store / "header.json");
        NorvesLib::Core::JsonDocument document;
        CHECK(NorvesLib::Core::JsonDocument::TryParseUtf8(header, document));
        const auto store = document.GetRoot().FindMember("store_id").AsString();
        TestText id;
        for (auto c : store)
        {
            CHECK(c < 128);
            id.push_back(static_cast<char>(c));
        }
        CookManagedStoreIndex out;
        const auto bytes = Read(f.Store / "roots.json");
        CHECK(ParseCookManagedStoreIndex(bytes, id, MaximumCookStoreRoots, out, error));
        return out;
    }
    void VerifyCommitted(const Fixture& f, const CookStateBinding& binding)
    {
        CHECK(std::filesystem::is_directory(f.Runtime));
        CHECK(!std::filesystem::exists(f.Store / "pending"));
        const auto index = Index(f);
        CHECK(index.Generation >= 2);
        const CookManagedRootClaim* claim = nullptr;
        for (const auto& row : index.Roots)
        {
            if (row.RootLeaf == "Runtime")
            {
                claim = &row;
            }
        }
        CHECK(claim);
        CHECK(std::memcmp(claim->DirectoryId.data(), Identity(f.Runtime).File.data(), 16) == 0 &&
              claim->OwnerId == binding.OwnerId);
        TestText leaf = "state-";
        leaf.append(claim->ClaimId);
        leaf.append(".json");
        const auto stateBytes = Read(f.Store / leaf.c_str());
        CookOwnedState state;
        TestText error;
        CHECK(ParseCookOwnedState(stateBytes, binding, state, error));
        CHECK(state.Generation == 1 && state.Records.size() == 2);
        const auto bytes = Read(f.Runtime / "manifest.json");
        NorvesLib::Core::Container::String text;
        for (uint8_t c : bytes)
        {
            text.push_back(static_cast<wchar_t>(c));
        }
        Asset::AssetManifest manifest;
        CHECK(manifest.LoadFromJsonText(text));
        CHECK(manifest.GetReferenceCount() == 2);
        // sourceを戻したfixtureでは共通cacheが実packageのtyped内容を含めてSkipを返す。
        if (std::filesystem::exists(f.Source))
        {
            for (size_t i = 0; i < f.Assets.size(); ++i)
            {
                const auto& record = state.Records[i].Record;
                CookDecisionContext context;
                CHECK(DecideCookCache(f.Assets[i], 7, true, &record, &manifest, context, error) == CookDecision::Skip);
            }
        }
    }
    struct Stop
    {
        Point At = Point::Prepared;
        bool bExit = false, bReached = false;
        std::filesystem::path Transaction, Runtime;
    };
    bool StopPoint(Point point, const std::filesystem::path& transaction, const std::filesystem::path& runtime,
                   void* context)
    {
        auto& stop = *static_cast<Stop*>(context);
        if (point != stop.At)
        {
            return true;
        }
        stop.bReached = true;
        stop.Transaction = transaction;
        stop.Runtime = runtime;
        if (stop.bExit)
        {
            ExitProcess(73);
        }
        return false;
    }
    Probe StopProbe(Stop& stop)
    {
        Probe probe;
        probe.Checkpoint = StopPoint;
        probe.Context = &stop;
        return probe;
    }
    void Interrupt(Fixture& f, Point point)
    {
        Stop stop;
        stop.At = point;
        auto probe = StopProbe(stop);
        CookManagedBootstrapOutcome out;
        out.ClaimId = "held";
        TestText error;
        const auto result = f.Run(out, error, &probe);
        if (!stop.bReached)
        {
            std::fprintf(stderr, "unreached %u: result=%u %s\n", static_cast<unsigned>(point),
                         static_cast<unsigned>(result), error.c_str());
        }
        CHECK(stop.bReached && out.ClaimId == "held");
        CHECK(result == (point == Point::Prepared  ? Boot::Error
                         : point == Point::Retired ? Boot::CommittedButError
                                                   : Boot::NeedsRecovery));
    }
    void HideInputs(Fixture& f)
    {
        auto held = f.Source;
        held += ".held";
        std::filesystem::rename(f.Source, held);
        held = f.Spec;
        held += ".held";
        std::filesystem::rename(f.Spec, held);
        CHECK(!std::filesystem::exists(f.Source) && !std::filesystem::exists(f.Spec));
    }
    CookStateBinding Binding(const Fixture& f)
    {
        CookResolvedOwnerBinding out;
        TestText error;
        CHECK(ResolveCookOwnerBinding(f.Owner, out, error));
        return out.ExpectedBinding;
    }
    void Recover(Fixture& f, Recovery expected)
    {
        CookManagedBootstrapOutcome out;
        out.ClaimId = "held";
        TestText error;
        const auto result = RecoverCookManagedPending(f.Runtime, out, error);
        if (result != expected)
        {
            std::fprintf(stderr, "recover expected=%u got=%u: %s\n", static_cast<unsigned>(expected),
                         static_cast<unsigned>(result), error.c_str());
        }
        CHECK(result == expected);
        if (expected == Recovery::NoPending || expected == Recovery::Conflict || expected == Recovery::Error)
        {
            CHECK(out.ClaimId == "held");
        }
        else
        {
            CHECK(!out.ClaimId.empty() && out.ClaimId != "held" && !out.bCleanupIncomplete);
        }
    }
    void Crash(const Fixture& f, bool bRecovery, Point point)
    {
        wchar_t exe[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, exe, 32768);
        CHECK(length && length < 32768);
        Array<wchar_t> command;
        const auto token = [&](const wchar_t* value)
        {
            if (!command.empty())
            {
                command.push_back(L' ');
            }
            command.push_back(L'"');
            for (size_t i = 0; value[i]; ++i)
            {
                CHECK(value[i] != L'"');
                command.push_back(value[i]);
            }
            command.push_back(L'"');
        };
        token(exe);
        token(L"--test=CookManagedBootstrapTest");
        token(bRecovery ? L"--recovery-child" : L"--bootstrap-child");
        token(f.Base.c_str());
        wchar_t number[32]{};
        std::swprintf(number, 32, L"%u", static_cast<unsigned>(point));
        token(number);
        command.push_back(0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        CHECK(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                             &process));
        CHECK(CloseHandle(process.hThread));
        const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
        if (wait != WAIT_OBJECT_0)
        {
            (void)TerminateProcess(process.hProcess, 99);
            (void)WaitForSingleObject(process.hProcess, 60000);
        }
        DWORD code = 0;
        const bool bCode = GetExitCodeProcess(process.hProcess, &code) != 0;
        CHECK(CloseHandle(process.hProcess));
        if (!bCode || code != 73)
        {
            std::fprintf(stderr, "child recovery=%d point=%u wait=%lu exit=%lu\n", bRecovery,
                         static_cast<unsigned>(point), wait, code);
        }
        CHECK(wait == WAIT_OBJECT_0 && bCode && code == 73);
    }
    int Child(int argc, char** argv)
    {
        if (argc != 4 || (std::strcmp(argv[1], "--bootstrap-child") && std::strcmp(argv[1], "--recovery-child")))
        {
            return -1;
        }
        Fixture f(std::filesystem::path(argv[2]), false);
        Stop stop;
        stop.At = static_cast<Point>(std::strtoul(argv[3], nullptr, 10));
        stop.bExit = true;
        auto probe = StopProbe(stop);
        TestText error;
        CookManagedBootstrapOutcome out;
        if (!std::strcmp(argv[1], "--bootstrap-child"))
        {
            (void)f.Run(out, error, &probe);
        }
        else
        {
            (void)Access::RecoverCookManagedPendingForTest(f.Runtime, probe, out, error);
        }
        std::fprintf(stderr, "child did not exit: %s\n", error.c_str());
        return 91;
    }
    struct Mutation
    {
        Fixture* Case = nullptr;
        bool bSpec = false, bReached = false;
    };
    bool ChangeInput(Point point, const std::filesystem::path&, const std::filesystem::path&, void* context)
    {
        auto& mutation = *static_cast<Mutation*>(context);
        if (point != Point::Prepared)
        {
            return true;
        }
        mutation.bReached = true;
        const auto& path = mutation.bSpec ? mutation.Case->Spec : mutation.Case->Source;
        auto bytes = Read(path);
        if (mutation.bSpec)
        {
            bytes.push_back(' ');
        }
        else
        {
            bytes.back() ^= 1;
        }
        Write(path, bytes);
        return true;
    }
    struct BusyData
    {
        Fixture* Case = nullptr;
        Boot Result = Boot::Error;
    };
    DWORD WINAPI TryBusy(LPVOID context)
    {
        auto& data = *static_cast<BusyData*>(context);
        CookManagedBootstrapOutcome out;
        TestText error;
        data.Result = data.Case->Run(out, error);
        return 0;
    }
    bool ProbeBusy(Point point, const std::filesystem::path&, const std::filesystem::path&, void* context)
    {
        if (point != Point::Prepared)
        {
            return true;
        }
        auto& data = *static_cast<BusyData*>(context);
        HANDLE thread = CreateThread(nullptr, 0, TryBusy, &data, 0, nullptr);
        CHECK(thread);
        CHECK(WaitForSingleObject(thread, 60000) == WAIT_OBJECT_0);
        CHECK(CloseHandle(thread));
        CHECK(data.Result == Boot::Busy);
        return true;
    }
    bool CollidePending(Point point, const std::filesystem::path& transaction, const std::filesystem::path&, void*)
    {
        if (point != Point::Prepared)
        {
            return true;
        }
        const auto pending = transaction.parent_path() / "pending";
        CHECK(std::filesystem::create_directory(pending));
        Write(pending / "keep", "no replace");
        return true;
    }
    bool bAliasCleanupFailed = false;
    struct DriveAlias
    {
        wchar_t Drive[3] = {0, L':', 0};
        std::filesystem::path Target;
        bool bMapped = false;
        explicit DriveAlias(const std::filesystem::path& target) : Target(target)
        {
            const DWORD used = GetLogicalDrives();
            if (used == 0)
            {
                return;
            }
            for (wchar_t c = L'Z'; c >= L'P'; --c)
            {
                if ((used & (1u << (c - L'A'))) != 0)
                {
                    continue;
                }
                Drive[0] = c;
                wchar_t prior[32768]{};
                if (QueryDosDeviceW(Drive, prior, 32768) != 0 || GetLastError() != ERROR_FILE_NOT_FOUND)
                {
                    continue;
                }
                if (DefineDosDeviceW(DDD_NO_BROADCAST_SYSTEM, Drive, Target.c_str()))
                {
                    bMapped = true;
                    break;
                }
            }
        }
        ~DriveAlias()
        {
            if (bMapped &&
                !DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_NO_BROADCAST_SYSTEM, Drive,
                                  Target.c_str()))
            {
                bAliasCleanupFailed = true;
                std::fprintf(stderr, "DOS mapping cleanup failed: %lu\n", GetLastError());
            }
        }
        std::filesystem::path Root() const
        {
            wchar_t path[] = {Drive[0], L':', L'\\', 0};
            return std::filesystem::path(path);
        }
    };

    void Run(const std::filesystem::path& root)
    {
        TestText error;
        CookManagedBootstrapOutcome out;
        Fixture fresh(root / "fresh");
        const auto binding = Binding(fresh);
        for (const char* name : {".transaction-stage-unknown", ".transaction-retired-unknown"})
        {
            CHECK(std::filesystem::create_directory(fresh.Store / name));
            Write(fresh.Store / name / "keep", "unknown orphan");
        }
        const auto unknownStage = Snapshot(fresh.Store / ".transaction-stage-unknown"),
                   unknownRetired = Snapshot(fresh.Store / ".transaction-retired-unknown");
        CHECK(fresh.Run(out, error) == Boot::Created);
        CHECK(!out.bCleanupIncomplete && out.RetiredDirectory.empty());
        VerifyCommitted(fresh, binding);
        CookManagedStoreObservation observed;
        CHECK(ObserveCookManagedStore(fresh.Owner, observed, error) == CookManagedStoreResult::Observed &&
              observed.bRuntimeRootClaimed);
        const auto packageId = Identity(fresh.Runtime / "Cooked/a.nvpkg");
        const auto packageBytes = Read(fresh.Runtime / "Cooked/a.nvpkg");
        out.ClaimId = "held";
        CHECK(fresh.Run(out, error) == Boot::Conflict && out.ClaimId == "held");
        CHECK(Same(packageId, Identity(fresh.Runtime / "Cooked/a.nvpkg")) &&
              packageBytes == Read(fresh.Runtime / "Cooked/a.nvpkg"));
        Recover(fresh, Recovery::NoPending);
        Unchanged(unknownStage, Snapshot(fresh.Store / ".transaction-stage-unknown"));
        Unchanged(unknownRetired, Snapshot(fresh.Store / ".transaction-retired-unknown"));
        // 同workspaceの既存claimとstate/packageを保ったままsiblingを作る。
        Fixture sibling(fresh.Base, false);
        sibling.Runtime = fresh.Base / "Sibling";
        sibling.Owner.FinalRuntimeRoot = sibling.Runtime;
        for (auto& asset : sibling.Assets)
        {
            asset.PackagePath = sibling.Runtime / "Cooked" / asset.PackagePath.filename();
            asset.ManifestPath = sibling.Runtime / "manifest.json";
        }
        CHECK(sibling.Run(out, error) == Boot::Created);
        CHECK(Index(fresh).Roots.size() == 2 && Index(fresh).Generation == 3);
        CHECK(Same(packageId, Identity(fresh.Runtime / "Cooked/a.nvpkg")) &&
              packageBytes == Read(fresh.Runtime / "Cooked/a.nvpkg"));
        {
            Fixture f(root / "unowned");
            CHECK(std::filesystem::create_directory(f.Runtime));
            Write(f.Runtime / "keep", "user data");
            const auto id = Identity(f.Runtime / "keep");
            out.ClaimId = "held";
            CHECK(f.Run(out, error) == Boot::Conflict && out.ClaimId == "held");
            CHECK(Same(id, Identity(f.Runtime / "keep")) && Index(f).Roots.empty());
        }
        for (bool bSpec : {false, true})
        {
            Fixture f(root / (bSpec ? "dirty-spec" : "dirty-source"));
            Mutation mutation{&f, bSpec, false};
            Probe probe;
            probe.Checkpoint = ChangeInput;
            probe.Context = &mutation;
            out.ClaimId = "held";
            CHECK(f.Run(out, error, &probe) == Boot::Error && mutation.bReached && out.ClaimId == "held");
            CHECK(!std::filesystem::exists(f.Runtime) && !std::filesystem::exists(f.Store / "pending") &&
                  Index(f).Generation == 1);
            Recover(f, Recovery::NoPending);
        }
        {
            Fixture f(root / "unknown-pending");
            Interrupt(f, Point::PendingPublished);
            Write(f.Store / "pending/keep", "unknown");
            const auto id = Identity(f.Store / "pending/keep");
            Recover(f, Recovery::Conflict);
            CHECK(Same(id, Identity(f.Store / "pending/keep")));
            CHECK(std::filesystem::remove(f.Store / "pending/keep"));
            Recover(f, Recovery::RolledBack);
        }
        {
            Fixture f(root / "same-bytes-other-id");
            Interrupt(f, Point::RootPublished);
            const auto path = f.Runtime / "Cooked/a.nvpkg";
            const auto before = Read(path);
            const auto old = Identity(path);
            std::filesystem::rename(path, f.Base / "original-package");
            Write(path, before);
            const auto replacement = Identity(path);
            CHECK(!Same(old, replacement));
            Recover(f, Recovery::Conflict);
            CHECK(Same(replacement, Identity(path)) && Read(path) == before);
            CHECK(std::filesystem::remove(path));
            std::filesystem::rename(f.Base / "original-package", path);
            Recover(f, Recovery::RolledBack);
            CHECK(!std::filesystem::exists(f.Runtime));
        }
        {
            Fixture f(root / "unknown-runtime-child");
            Interrupt(f, Point::RootPublished);
            Write(f.Runtime / "keep", "unknown runtime child");
            const auto id = Identity(f.Runtime / "keep");
            Recover(f, Recovery::Conflict);
            CHECK(Same(id, Identity(f.Runtime / "keep")));
            CHECK(std::filesystem::remove(f.Runtime / "keep"));
            Recover(f, Recovery::RolledBack);
        }
        {
            Fixture f(root / "replaced-parent");
            Interrupt(f, Point::RootPublished);
            const auto old = Identity(f.Runtime / "Cooked");
            std::filesystem::rename(f.Runtime / "Cooked", f.Base / "old-parent");
            CHECK(std::filesystem::create_directory(f.Runtime / "Cooked"));
            for (const char* leaf : {"a.nvpkg", "b.nvpkg"})
            {
                CHECK(std::filesystem::copy_file(f.Base / "old-parent" / leaf, f.Runtime / "Cooked" / leaf));
            }
            const auto replacement = Identity(f.Runtime / "Cooked");
            CHECK(!Same(old, replacement));
            Recover(f, Recovery::Conflict);
            CHECK(Same(replacement, Identity(f.Runtime / "Cooked")));
            CHECK(std::filesystem::remove_all(f.Runtime / "Cooked") == 3);
            std::filesystem::rename(f.Base / "old-parent", f.Runtime / "Cooked");
            Recover(f, Recovery::RolledBack);
        }
        {
            Fixture f(root / "receipt-replaced");
            Interrupt(f, Point::IndexPublished);
            const auto path = f.Store / "pending/receipt.stage";
            const auto bytes = Read(path);
            const auto old = Identity(path);
            std::filesystem::rename(path, f.Base / "old-receipt");
            Write(path, bytes);
            CHECK(!Same(old, Identity(path)));
            Recover(f, Recovery::Conflict);
            CHECK(std::filesystem::remove(path));
            std::filesystem::rename(f.Base / "old-receipt", path);
            Recover(f, Recovery::RolledBack);
        }
        {
            Fixture f(root / "control-corrupt");
            Interrupt(f, Point::PendingPublished);
            const auto path = f.Store / "pending/index.after";
            const auto bytes = Read(path);
            const auto id = Identity(path);
            Write(path, "{}");
            CHECK(Same(id, Identity(path)));
            Recover(f, Recovery::Conflict);
            Write(path, bytes);
            Recover(f, Recovery::RolledBack);
        }
        {
            Fixture f(root / "busy");
            BusyData data{&f};
            Probe probe;
            probe.Checkpoint = ProbeBusy;
            probe.Context = &data;
            CHECK(f.Run(out, error, &probe) == Boot::Created && data.Result == Boot::Busy);
            VerifyCommitted(f, Binding(f));
        }
        {
            Fixture f(root / "no-replace-pending");
            Probe probe;
            probe.Checkpoint = CollidePending;
            out.ClaimId = "held";
            CHECK(f.Run(out, error, &probe) == Boot::Error && out.ClaimId == "held");
            CHECK(!std::filesystem::exists(f.Runtime));
            const auto id = Identity(f.Store / "pending/keep");
            const auto bytes = Read(f.Store / "pending/keep");
            Recover(f, Recovery::Conflict);
            CHECK(Same(id, Identity(f.Store / "pending/keep")) && Read(f.Store / "pending/keep") == bytes);
        }
        {
            // intent自身は循環する保存self-IDを持たない。同bytesの固定slotは意味を変えない。
            Fixture f(root / "intent-same-bytes");
            Interrupt(f, Point::IndexPublished);
            const auto path = f.Store / "pending/intent.json";
            const auto bytes = Read(path);
            const auto old = Identity(path);
            std::filesystem::rename(path, f.Base / "original-intent");
            Write(path, bytes);
            CHECK(!Same(old, Identity(path)));
            Recover(f, Recovery::RolledBack);
            CHECK(Same(old, Identity(f.Base / "original-intent")) && Read(f.Base / "original-intent") == bytes);
        }
        // 実process終了。全部のhandleが消えたordinary取得でもpendingを復旧する。
        unsigned crashes = 0;
        for (Point point : {Point::Prepared, Point::PendingPublished, Point::RootPublished, Point::StatePublished,
                            Point::IndexBackedUp, Point::IndexPublished, Point::BeforeReceipt, Point::ReceiptCommitted,
                            Point::BeforeRetire, Point::Retired})
        {
            char name[40];
            std::snprintf(name, sizeof(name), "publish-crash-%u", static_cast<unsigned>(point));
            Fixture f(root / name);
            const auto expectedBinding = Binding(f);
            const auto before = Read(f.Store / "roots.json");
            const auto indexId = Identity(f.Store / "roots.json");
            Crash(f, false, point);
            ++crashes;
            HideInputs(f);
            if (point == Point::Prepared)
            {
                CHECK(!std::filesystem::exists(f.Runtime));
                const auto orphan = Snapshot(f.Store);
                CHECK(orphan.size() > 2);
                Recover(f, Recovery::NoPending);
                Unchanged(orphan, Snapshot(f.Store));
                CHECK(Read(f.Store / "roots.json") == before && Same(indexId, Identity(f.Store / "roots.json")));
            }
            else if (point == Point::Retired)
            {
                const auto orphan = Snapshot(f.Store);
                Recover(f, Recovery::NoPending);
                Unchanged(orphan, Snapshot(f.Store));
                VerifyCommitted(f, expectedBinding);
            }
            else if (point == Point::ReceiptCommitted || point == Point::BeforeRetire)
            {
                Recover(f, Recovery::Committed);
                VerifyCommitted(f, expectedBinding);
            }
            else
            {
                Recover(f, Recovery::RolledBack);
                CHECK(!std::filesystem::exists(f.Runtime));
                CHECK(Read(f.Store / "roots.json") == before && Same(indexId, Identity(f.Store / "roots.json")));
            }
        }
        // rollback自体の中断後も、原index/after objectsを残してもう一度復旧できる。
        for (Point point : {Point::RollbackIndexRemoved, Point::RollbackIndexRestored, Point::RollbackStateRestored,
                            Point::RollbackRootRestored, Point::BeforeRetire, Point::Retired})
        {
            char name[40];
            std::snprintf(name, sizeof(name), "rollback-crash-%u", static_cast<unsigned>(point));
            Fixture f(root / name);
            const auto before = Read(f.Store / "roots.json");
            const auto id = Identity(f.Store / "roots.json");
            Interrupt(f, Point::IndexPublished);
            HideInputs(f);
            Crash(f, true, point);
            ++crashes;
            const auto orphan = Snapshot(f.Store);
            Recover(f, point == Point::Retired ? Recovery::NoPending : Recovery::RolledBack);
            if (point == Point::Retired)
            {
                Unchanged(orphan, Snapshot(f.Store));
            }
            CHECK(!std::filesystem::exists(f.Runtime) && Read(f.Store / "roots.json") == before &&
                  Same(id, Identity(f.Store / "roots.json")));
        }
        CHECK(crashes == 16);
        {
            Fixture f(root / "abandoned");
            TestText name;
            CHECK(Access::InspectCookDestinationMutexNameForTest({f.Runtime}, name, error));
            Array<wchar_t> wide;
            for (char c : name)
            {
                wide.push_back(static_cast<wchar_t>(c));
            }
            wide.push_back(0);
            HANDLE keep = CreateMutexExW(nullptr, wide.data(), 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
            CHECK(keep);
            Crash(f, false, Point::IndexPublished);
            HideInputs(f);
            bool bAbandoned = false;
            Probe probe;
            probe.bAbandonedObserved = &bAbandoned;
            CHECK(Access::RecoverCookManagedPendingForTest(f.Runtime, probe, out, error) == Recovery::RolledBack &&
                  bAbandoned);
            CHECK(CloseHandle(keep));
            ++crashes;
        }
        for (auto fault : {Access::CookLockFault::Release, Access::CookLockFault::Close})
        {
            Fixture f(root / (fault == Access::CookLockFault::Release ? "release-report" : "close-report"));
            const auto expectedBinding = Binding(f);
            Probe probe;
            probe.LockFault = fault;
            out.ClaimId = "held";
            CHECK(f.Run(out, error, &probe) == Boot::CommittedButError && out.ClaimId == "held");
            VerifyCommitted(f, expectedBinding);
            Recover(f, Recovery::NoPending);
        }
        CHECK(crashes == 17);
        bool bShortAlias = false, bUnicodeAlias = false, bSubst = false;
        for (bool bUnicode : {false, true})
        {
            const auto longRoot = root / (bUnicode ? L"\u8cc7\u6599 Unicode Parent" : L"Long Parent For Short Alias");
            CHECK(std::filesystem::create_directory(longRoot));
            wchar_t shortName[32768]{};
            const auto count = GetShortPathNameW(longRoot.c_str(), shortName, 32768);
            bool bAscii = count && count < 32768;
            if (bAscii)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    bAscii &= shortName[i] >= 32 && shortName[i] < 127;
                }
            }
            if (bAscii && std::filesystem::path(shortName).native() != longRoot.native())
            {
                Fixture f(std::filesystem::path(shortName) / "Alias Case");
                const auto expectedBinding = Binding(f);
                CHECK(f.Run(out, error) == Boot::Created);
                VerifyCommitted(f, expectedBinding);
                if (bUnicode)
                {
                    bUnicodeAlias = true;
                }
                else
                {
                    bShortAlias = true;
                }
            }
        }
        const auto substTarget = root / "Subst Target";
        CHECK(std::filesystem::create_directory(substTarget));
        {
            DriveAlias alias(substTarget);
            if (alias.bMapped)
            {
                Fixture f(alias.Root() / "Mapped Case");
                const auto expectedBinding = Binding(f);
                Interrupt(f, Point::IndexPublished);
                HideInputs(f);
                Recover(f, Recovery::RolledBack);
                auto held = f.Spec;
                held += ".held";
                std::filesystem::rename(held, f.Spec);
                held = f.Source;
                held += ".held";
                std::filesystem::rename(held, f.Source);
                CHECK(f.Run(out, error) == Boot::Created);
                VerifyCommitted(f, expectedBinding);
                bSubst = true;
            }
        }
        CHECK(!bAliasCleanupFailed);
        std::printf(
            "bootstrap_short_alias_checked=%d bootstrap_unicode_alias_checked=%d bootstrap_subst_checked=%d bootstrap_abandoned_checked=1 bootstrap_busy_checked=1\n",
            bShortAlias, bUnicodeAlias, bSubst);

        std::printf(
            "COOK_MANAGED_BOOTSTRAP result=pass real_texture_publish_source_free_recovery_known_images_before_bytes_retired_orphans crashes=%u\n",
            crashes);
    }
#endif
} // namespace BootstrapTest
int main(int argc, char** argv)
{
#if defined(_WIN32)
    const int child = BootstrapTest::Child(argc, argv);
    if (child >= 0)
    {
        return child;
    }
    wchar_t temp[32768]{};
    const auto length = GetTempPathW(32768, temp);
    CHECK(length && length < 32768);
    wchar_t name[128]{};
    std::swprintf(name, 128, L"NorvesManagedBootstrap-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    const auto root = std::filesystem::path(temp) / name;
    CHECK(std::filesystem::create_directory(root));
    BootstrapTest::Run(root);
    CHECK(std::filesystem::remove_all(root) > 0);
    return 0;
#else
    (void)argc;
    (void)argv;
    return 125;
#endif
}
