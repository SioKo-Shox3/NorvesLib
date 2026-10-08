// 固定inventory更新とsource非依存復旧を、実process終了とnative IDで反証する。
#include "Tools/AssetCook/CookManagedBootstrap.h"
#include "Tools/AssetCook/CookManagedUpdate.h"
#include "Tools/AssetCook/CookManagedUpdateTestAccess.h"
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
namespace UpdateTest
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
    struct Fixture
    {
        std::filesystem::path Base, Runtime, Spec, Source, StableSource, Store;
        TestText SpecText = "{\"version\":1,\"fixture\":\"update\"}";
        Array<SingleAssetCookRequest> Assets;
        CookOwnerResolveRequest Owner;
        explicit Fixture(const std::filesystem::path& base, bool bCreate = true)
            : Base(base), Runtime(base / "Runtime"), Spec(base / "spec.json"), Source(base / "source.ppm"),
              StableSource(base / "stable.ppm"), Store(base / ".norves-assetcook")
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
                Write(StableSource, BytesOf(ppm));
            }
            Owner = {Spec, Runtime, "manifest.json"};
            for (const char* name : {"a", "b", "c"})
            {
                SingleAssetCookRequest request;
                request.InputPath = name[0] == 'c' ? StableSource : Source;
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
                request.Format = name[0] == 'b' ? "nvtex.v0.rg8.linear" : "nvtex.v0.rgba8.srgb";
                request.Variant = "default";
                Assets.push_back(std::move(request));
            }
            if (bCreate)
            {
                CookManagedStoreObservation observed;
                CookManagedBootstrapOutcome out;
                TestText error;
                CHECK(InitializeNewCookManagedStore(Owner, observed, error) ==
                      CookManagedStoreInitializationResult::Created);
                const auto result = BootstrapCookManagedAssetSet(Request(), out, error);
                if (result != Boot::Created)
                {
                    std::fprintf(stderr, "update fixture bootstrap: %s\n", error.c_str());
                }
                CHECK(result == Boot::Created);
                CHECK(std::filesystem::create_directory(Runtime / "User"));
                Write(Runtime / "User/keep.txt", "unlisted");
            }
        }
        CookManagedUpdateRequest Request() const
        {
            return {Owner, Assets, BytesOf(SpecText), 7};
        }
        Update Run(CookManagedUpdateOutcome& out, TestText& error, const Probe* probe = nullptr) const
        {
            const auto request = Request();
            const auto result = probe ? Access::UpdateCookManagedAssetSetForTest(request, *probe, out, error)
                                      : UpdateCookManagedAssetSet(request, out, error);
            if (result != Update::Updated && result != Update::NoChange)
            {
                std::fprintf(stderr, "UPDATE_RESULT result=%u error=%s\n", static_cast<unsigned>(result),
                             error.c_str());
            }
            return result;
        }
        void Change()
        {
            auto bytes = Read(Source);
            bytes.back() ^= 0x55;
            bytes[bytes.size() - 3] ^= 0x33;
            Write(Source, bytes);
        }
        void HideInputs()
        {
            for (const auto& path : {Source, StableSource, Spec})
            {
                auto held = path;
                held += ".held";
                std::filesystem::rename(path, held);
                CHECK(!std::filesystem::exists(path));
            }
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
    struct Before
    {
        CookStateBinding Binding;
        std::filesystem::path StatePath;
        Array<SnapshotNode> RuntimeTree;
        NativeId RootId, IndexId, StateId, SkipId;
        TestBytes IndexBytes, StateBytes, SkipBytes;
        uint64_t IndexGeneration = 0, StateGeneration = 0;
        TestText Claim;
        explicit Before(const Fixture& f)
        {
            CookResolvedOwnerBinding owner;
            TestText error;
            CHECK(ResolveCookOwnerBinding(f.Owner, owner, error));
            Binding = owner.ExpectedBinding;
            const auto index = Index(f);
            IndexGeneration = index.Generation;
            for (const auto& claim : index.Roots)
            {
                if (claim.RootLeaf == "Runtime")
                {
                    Claim = claim.ClaimId;
                }
            }
            CHECK(!Claim.empty());
            TestText leaf = "state-";
            leaf.append(Claim);
            leaf.append(".json");
            StatePath = f.Store / leaf.c_str();
            RuntimeTree = Snapshot(f.Runtime);
            RootId = Identity(f.Runtime);
            IndexId = Identity(f.Store / "roots.json");
            StateId = Identity(StatePath);
            IndexBytes = Read(f.Store / "roots.json");
            StateBytes = Read(StatePath);
            SkipId = Identity(f.Assets[2].PackagePath);
            SkipBytes = Read(f.Assets[2].PackagePath);
            CookOwnedState state;
            CHECK(ParseCookOwnedState(StateBytes, Binding, state, error));
            StateGeneration = state.Generation;
        }
        void UnchangedState(const Fixture& f) const
        {
            CHECK(Same(RootId, Identity(f.Runtime)));
            CHECK(Same(IndexId, Identity(f.Store / "roots.json")) && IndexBytes == Read(f.Store / "roots.json"));
            CHECK(Same(StateId, Identity(StatePath)) && StateBytes == Read(StatePath));
            Unchanged(RuntimeTree, Snapshot(f.Runtime));
        }
        void UpdatedState(const Fixture& f) const
        {
            CHECK(Same(RootId, Identity(f.Runtime)));
            CHECK(!Same(IndexId, Identity(f.Store / "roots.json")) && !Same(StateId, Identity(StatePath)));
            CHECK(Index(f).Generation == IndexGeneration + 1);
            TestText error;
            CookOwnedState state;
            CHECK(ParseCookOwnedState(Read(StatePath), Binding, state, error));
            CHECK(state.Generation == StateGeneration + 1 && state.Records.size() == 3);
            CHECK(Same(SkipId, Identity(f.Assets[2].PackagePath)) && SkipBytes == Read(f.Assets[2].PackagePath));
            const auto tree = Snapshot(f.Runtime);
            CHECK(tree.size() == RuntimeTree.size());
            for (size_t i = 0; i < tree.size(); ++i)
            {
                CHECK(tree[i].Relative == RuntimeTree[i].Relative);
                if (tree[i].Relative == "Cooked/a.nvpkg" || tree[i].Relative == "Cooked/b.nvpkg" ||
                    tree[i].Relative == "manifest.json")
                {
                    CHECK(!Same(tree[i].Id, RuntimeTree[i].Id));
                }
                else
                {
                    CHECK(Same(tree[i].Id, RuntimeTree[i].Id) && tree[i].Size == RuntimeTree[i].Size &&
                          tree[i].Hash == RuntimeTree[i].Hash);
                }
            }
        }
    };
    struct Boundary
    {
        Point At;
        size_t Ordinal = 0;
    };
    struct Stop
    {
        Boundary Target{Point::Prepared};
        bool bExit = false, bReached = false;
        std::filesystem::path Transaction;
    };
    bool StopPoint(Point point, const std::filesystem::path& transaction, const std::filesystem::path&, size_t ordinal,
                   void* context)
    {
        auto& stop = *static_cast<Stop*>(context);
        if (point != stop.Target.At || ordinal != stop.Target.Ordinal)
        {
            return true;
        }
        stop.bReached = true;
        stop.Transaction = transaction;
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
    void Interrupt(Fixture& f, Boundary boundary)
    {
        Stop stop;
        stop.Target = boundary;
        auto probe = StopProbe(stop);
        CookManagedUpdateOutcome out;
        out.ClaimId = "held";
        TestText error;
        const auto result = f.Run(out, error, &probe);
        CHECK(stop.bReached && out.ClaimId == "held");
        CHECK(result == (boundary.At == Point::Prepared  ? Update::Error
                         : boundary.At == Point::Retired ? Update::CommittedButError
                                                         : Update::NeedsRecovery));
    }
    CookManagedUpdateOutcome Recover(Fixture& f, Recovery expected, const Probe* probe = nullptr)
    {
        CookManagedUpdateOutcome out;
        out.ClaimId = "held";
        TestText error;
        const auto result = probe ? Access::RecoverCookManagedUpdateForTest(f.Runtime, *probe, out, error)
                                  : RecoverCookManagedPending(f.Runtime, out, error);
        if (result != expected)
        {
            std::fprintf(stderr, "update recovery expected=%u result=%u error=%s\n", static_cast<unsigned>(expected),
                         static_cast<unsigned>(result), error.c_str());
        }
        CHECK(result == expected);
        if (expected == Recovery::NoPending || expected == Recovery::Conflict || expected == Recovery::Error)
        {
            CHECK(out.ClaimId == "held");
        }
        else
        {
            CHECK(out.ClaimId != "held" && !out.bCleanupIncomplete);
        }
        return out;
    }
    void Crash(const Fixture& f, bool bRecovery, Boundary boundary)
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
        token(L"--test=CookManagedUpdateTest");
        token(bRecovery ? L"--recovery-child" : L"--update-child");
        token(f.Base.c_str());
        wchar_t number[32]{};
        std::swprintf(number, 32, L"%u", static_cast<unsigned>(boundary.At));
        token(number);
        std::swprintf(number, 32, L"%zu", boundary.Ordinal);
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
            std::fprintf(stderr, "update child recovery=%d point=%u ordinal=%zu wait=%lu exit=%lu\n", bRecovery,
                         static_cast<unsigned>(boundary.At), boundary.Ordinal, wait, code);
        }
        CHECK(wait == WAIT_OBJECT_0 && bCode && code == 73);
    }
    int Child(int argc, char** argv)
    {
        if (argc != 5 || (std::strcmp(argv[1], "--update-child") && std::strcmp(argv[1], "--recovery-child")))
        {
            return -1;
        }
        Fixture f(std::filesystem::path(argv[2]), false);
        Stop stop;
        stop.Target.At = static_cast<Point>(std::strtoul(argv[3], nullptr, 10));
        stop.Target.Ordinal = static_cast<size_t>(std::strtoull(argv[4], nullptr, 10));
        stop.bExit = true;
        auto probe = StopProbe(stop);
        TestText error;
        CookManagedUpdateOutcome out;
        if (!std::strcmp(argv[1], "--update-child"))
        {
            (void)f.Run(out, error, &probe);
        }
        else
        {
            (void)Access::RecoverCookManagedUpdateForTest(f.Runtime, probe, out, error);
        }
        std::fprintf(stderr, "update child did not exit: %s\n", error.c_str());
        return 91;
    }
    struct BusyData
    {
        Fixture* Case = nullptr;
        Update Result = Update::Error;
    };
    DWORD WINAPI TryBusy(LPVOID context)
    {
        auto& data = *static_cast<BusyData*>(context);
        CookManagedUpdateOutcome out;
        TestText error;
        data.Result = data.Case->Run(out, error);
        return 0;
    }
    bool BusyPoint(Point point, const std::filesystem::path&, const std::filesystem::path&, size_t, void* context)
    {
        if (point != Point::Prepared)
        {
            return true;
        }
        HANDLE thread = CreateThread(nullptr, 0, TryBusy, context, 0, nullptr);
        CHECK(thread);
        CHECK(WaitForSingleObject(thread, 60000) == WAIT_OBJECT_0);
        CHECK(CloseHandle(thread));
        return true;
    }
    struct DirtyInput
    {
        Fixture* Case = nullptr;
        bool bSpec = false;
    };
    bool DirtyPoint(Point point, const std::filesystem::path&, const std::filesystem::path&, size_t, void* context)
    {
        if (point != Point::Prepared)
        {
            return true;
        }
        const auto& dirty = *static_cast<DirtyInput*>(context);
        if (dirty.bSpec)
        {
            auto bytes = Read(dirty.Case->Spec);
            bytes.push_back(' ');
            Write(dirty.Case->Spec, bytes);
        }
        else
        {
            dirty.Case->Change();
        }
        return true;
    }
    void Run(const std::filesystem::path& root)
    {
        CookManagedUpdateOutcome out;
        TestText error;
        {
            Fixture f(root / "happy");
            const auto all = Snapshot(f.Base);
            CHECK(f.Run(out, error) == Update::NoChange && out.StateGeneration == 1 && out.IndexGeneration == 2);
            Unchanged(all, Snapshot(f.Base));
            Fixture sibling(f.Base, false);
            sibling.Runtime = f.Base / "Sibling";
            sibling.Owner.FinalRuntimeRoot = sibling.Runtime;
            for (auto& asset : sibling.Assets)
            {
                asset.PackagePath = sibling.Runtime / "Cooked" / asset.PackagePath.filename();
                asset.ManifestPath = sibling.Runtime / "manifest.json";
            }
            CHECK(BootstrapCookManagedAssetSet(sibling.Request(), out, error) == Boot::Created);
            const auto siblingId = Identity(sibling.Runtime);
            const auto siblingTree = Snapshot(sibling.Runtime);
            CHECK(std::filesystem::create_directory(f.Store / ".transaction-stage-foreign"));
            CHECK(std::filesystem::create_directory(f.Store / ".transaction-retired-foreign"));
            Write(f.Store / ".transaction-stage-foreign/keep", "unknown-stage");
            Write(f.Store / ".transaction-retired-foreign/keep", "unknown-retired");
            const auto unknownStage = Snapshot(f.Store / ".transaction-stage-foreign");
            const auto unknownRetired = Snapshot(f.Store / ".transaction-retired-foreign");
            f.Change();
            const Before before(f);
            BusyData busy{&f};
            Probe probe;
            probe.Checkpoint = BusyPoint;
            probe.Context = &busy;
            CHECK(f.Run(out, error, &probe) == Update::Updated && busy.Result == Update::Busy);
            CHECK(out.ClaimId == before.Claim && out.IndexGeneration == before.IndexGeneration + 1 &&
                  out.StateGeneration == 2);
            before.UpdatedState(f);
            CHECK(Same(siblingId, Identity(sibling.Runtime)));
            Unchanged(siblingTree, Snapshot(sibling.Runtime));
            Unchanged(unknownStage, Snapshot(f.Store / ".transaction-stage-foreign"));
            Unchanged(unknownRetired, Snapshot(f.Store / ".transaction-retired-foreign"));
            CHECK(f.Run(out, error) == Update::NoChange);
        }
        {
            Fixture f(root / "manifest-only");
            const Before before(f);
            const auto packages = Snapshot(f.Runtime / "Cooked");
            std::swap(f.Assets[0], f.Assets[1]);
            CHECK(f.Run(out, error) == Update::Updated);
            Unchanged(packages, Snapshot(f.Runtime / "Cooked"));
            CHECK(out.StateGeneration == 2 && out.IndexGeneration == 3 && Same(before.RootId, Identity(f.Runtime)));
            CHECK(f.Run(out, error) == Update::NoChange);
        }
        {
            Fixture f(root / "generation-max");
            Before start(f);
            auto index = Index(f);
            index.Generation = UINT64_MAX;
            TestText text;
            CHECK(SerializeCookManagedStoreIndex(index, text, error));
            Write(f.Store / "roots.json", BytesOf(text));
            CookOwnedState state;
            CHECK(ParseCookOwnedState(start.StateBytes, start.Binding, state, error));
            state.Generation = UINT64_MAX;
            CHECK(SerializeCookOwnedState(state, text, error));
            Write(start.StatePath, BytesOf(text));
            const Before before(f);
            const auto store = Snapshot(f.Store);
            CHECK(f.Run(out, error) == Update::NoChange && out.StateGeneration == UINT64_MAX &&
                  out.IndexGeneration == UINT64_MAX);
            before.UnchangedState(f);
            f.Change();
            out.ClaimId = "held";
            CHECK(f.Run(out, error) == Update::Error && out.ClaimId == "held");
            before.UnchangedState(f);
            Unchanged(store, Snapshot(f.Store));
        }
        for (bool bMissing : {false, true})
        {
            Fixture f(root / (bMissing ? "missing-package" : "corrupt-package"));
            if (bMissing)
            {
                CHECK(std::filesystem::remove(f.Assets[0].PackagePath));
            }
            else
            {
                Write(f.Assets[0].PackagePath, "corrupt");
            }
            const auto skipId = Identity(f.Assets[1].PackagePath);
            const auto skipBytes = Read(f.Assets[1].PackagePath);
            CHECK(f.Run(out, error) == Update::Updated);
            CHECK(Same(skipId, Identity(f.Assets[1].PackagePath)) && Read(f.Assets[1].PackagePath) == skipBytes);
            CHECK(f.Run(out, error) == Update::NoChange);
        }
        for (bool bMissing : {false, true})
        {
            Fixture f(root / (bMissing ? "missing-manifest" : "corrupt-manifest"));
            if (bMissing)
            {
                CHECK(std::filesystem::remove(f.Runtime / "manifest.json"));
            }
            else
            {
                Write(f.Runtime / "manifest.json", "corrupt");
            }
            CHECK(f.Run(out, error) == Update::Updated);
            CHECK(f.Run(out, error) == Update::NoChange);
        }
        {
            Fixture f(root / "missing-parent");
            std::filesystem::rename(f.Runtime / "Cooked", f.Base / "held-cooked");
            const auto before = Snapshot(f.Base);
            out.ClaimId = "held";
            CHECK(f.Run(out, error) == Update::Error && out.ClaimId == "held");
            Unchanged(before, Snapshot(f.Base));
        }
        for (bool bSpec : {false, true})
        {
            Fixture f(root / (bSpec ? "dirty-spec" : "dirty-source"));
            f.Change();
            const Before before(f);
            DirtyInput dirty{&f, bSpec};
            Probe probe;
            probe.Checkpoint = DirtyPoint;
            probe.Context = &dirty;
            out.ClaimId = "held";
            CHECK(f.Run(out, error, &probe) == Update::Error && out.ClaimId == "held");
            before.UnchangedState(f);
            CHECK(!std::filesystem::exists(f.Store / "pending"));
        }
        for (int fault = 0; fault < 5; ++fault)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "recovery-conflict-%d", fault);
            Fixture f(root / name);
            f.Change();
            Interrupt(f, {Point::PendingPublished});
            const auto pendingSnapshot = Snapshot(f.Base);
            out.ClaimId = "held";
            CHECK(f.Run(out, error) == Update::NeedsRecovery && out.ClaimId == "held");
            Unchanged(pendingSnapshot, Snapshot(f.Base));
            const auto pending = f.Store / "pending";
            if (fault == 0)
            {
                Write(pending / "unknown", "keep");
            }
            else if (fault == 1)
            {
                const auto path = pending / "package.after-00000000";
                const auto bytes = Read(path);
                std::filesystem::rename(path, f.Base / "held-after");
                Write(path, bytes);
                CHECK(!Same(Identity(path), Identity(f.Base / "held-after")));
            }
            else if (fault == 2)
            {
                Write(pending / "state.after", "corrupt");
            }
            else if (fault == 3)
            {
                std::filesystem::rename(f.Runtime / "Cooked", f.Base / "held-parent");
                CHECK(std::filesystem::create_directory(f.Runtime / "Cooked"));
            }
            else
            {
                const auto bytes = Read(pending / "receipt.stage");
                std::filesystem::rename(pending / "receipt.stage", f.Base / "held-receipt");
                Write(pending / "receipt.stage", bytes);
            }
            f.HideInputs();
            const auto snapshot = Snapshot(f.Base);
            Recover(f, Recovery::Conflict);
            Unchanged(snapshot, Snapshot(f.Base));
        }
        {
            Fixture f(root / "absent-before-rollback");
            CHECK(std::filesystem::remove(f.Assets[0].PackagePath));
            CHECK(std::filesystem::remove(f.Runtime / "manifest.json"));
            const Before before(f);
            Interrupt(f, {Point::IndexPublished});
            f.HideInputs();
            const auto recovered = Recover(f, Recovery::RolledBack);
            CHECK(recovered.StateGeneration == before.StateGeneration &&
                  recovered.IndexGeneration == before.IndexGeneration);
            before.UnchangedState(f);
        }
        unsigned crashes = 0;
        const Boundary publish[] = {{Point::Prepared},           {Point::PendingPublished},
                                    {Point::PackageBackedUp, 0}, {Point::PackagePublished, 0},
                                    {Point::PackageBackedUp, 1}, {Point::PackagePublished, 1},
                                    {Point::ManifestBackedUp},   {Point::ManifestPublished},
                                    {Point::StateBackedUp},      {Point::StatePublished},
                                    {Point::IndexBackedUp},      {Point::IndexPublished},
                                    {Point::BeforeReceipt},      {Point::ReceiptCommitted},
                                    {Point::BeforeRetire},       {Point::Retired}};
        for (const auto boundary : publish)
        {
            char name[48];
            std::snprintf(name, sizeof(name), "publish-%u-%zu", static_cast<unsigned>(boundary.At), boundary.Ordinal);
            Fixture f(root / name);
            f.Change();
            const Before before(f);
            Crash(f, false, boundary);
            ++crashes;
            f.HideInputs();
            if (boundary.At == Point::Prepared || boundary.At == Point::Retired)
            {
                const auto orphan = Snapshot(f.Store);
                Recover(f, Recovery::NoPending);
                Unchanged(orphan, Snapshot(f.Store));
                if (boundary.At == Point::Prepared)
                {
                    before.UnchangedState(f);
                }
                else
                {
                    before.UpdatedState(f);
                }
            }
            else if (boundary.At == Point::ReceiptCommitted || boundary.At == Point::BeforeRetire)
            {
                const auto recovered = Recover(f, Recovery::Committed);
                CHECK(recovered.StateGeneration == before.StateGeneration + 1);
                before.UpdatedState(f);
            }
            else
            {
                const auto recovered = Recover(f, Recovery::RolledBack);
                CHECK(recovered.StateGeneration == before.StateGeneration &&
                      recovered.IndexGeneration == before.IndexGeneration);
                before.UnchangedState(f);
            }
        }
        const Boundary rollback[] = {{Point::RollbackIndexRemoved},
                                     {Point::RollbackIndexRestored},
                                     {Point::RollbackStateRemoved},
                                     {Point::RollbackStateRestored},
                                     {Point::RollbackManifestRemoved},
                                     {Point::RollbackManifestRestored},
                                     {Point::RollbackPackageRemoved, 1},
                                     {Point::RollbackPackageRestored, 1},
                                     {Point::RollbackPackageRemoved, 0},
                                     {Point::RollbackPackageRestored, 0},
                                     {Point::BeforeRetire},
                                     {Point::Retired}};
        for (const auto boundary : rollback)
        {
            char name[48];
            std::snprintf(name, sizeof(name), "rollback-%u-%zu", static_cast<unsigned>(boundary.At), boundary.Ordinal);
            Fixture f(root / name);
            f.Change();
            const Before before(f);
            Interrupt(f, {Point::IndexPublished});
            f.HideInputs();
            Crash(f, true, boundary);
            ++crashes;
            const auto orphan = Snapshot(f.Store);
            Recover(f, boundary.At == Point::Retired ? Recovery::NoPending : Recovery::RolledBack);
            if (boundary.At == Point::Retired)
            {
                Unchanged(orphan, Snapshot(f.Store));
            }
            before.UnchangedState(f);
        }
        CHECK(crashes == 28);
        {
            Fixture f(root / "abandoned");
            f.Change();
            const Before before(f);
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
            Crash(f, false, {Point::IndexPublished});
            f.HideInputs();
            bool bAbandoned = false;
            Probe probe;
            probe.bAbandonedObserved = &bAbandoned;
            Recover(f, Recovery::RolledBack, &probe);
            CHECK(bAbandoned && CloseHandle(keep));
            before.UnchangedState(f);
            ++crashes;
        }
        for (auto fault : {Access::CookLockFault::Release, Access::CookLockFault::Close})
        {
            Fixture f(root / (fault == Access::CookLockFault::Release ? "release-fault" : "close-fault"));
            f.Change();
            const Before before(f);
            Probe probe;
            probe.LockFault = fault;
            out.ClaimId = "held";
            CHECK(f.Run(out, error, &probe) == Update::CommittedButError && out.ClaimId == "held");
            before.UpdatedState(f);
        }
        std::puts("update_busy_checked=1 update_abandoned_checked=1 update_source_free_recovery_checked=1");
        std::printf(
            "COOK_MANAGED_UPDATE result=pass fixed_inventory_nochange_manifest_only_known_images_skip_unlisted_preserved crashes=%u\n",
            crashes);
    }
#endif
} // 名前空間 UpdateTest
int main(int argc, char** argv)
{
#if defined(_WIN32)
    const int child = UpdateTest::Child(argc, argv);
    if (child >= 0)
    {
        return child;
    }
    wchar_t temp[32768]{};
    const auto length = GetTempPathW(32768, temp);
    CHECK(length && length < 32768);
    wchar_t name[128]{};
    std::swprintf(name, 128, L"NorvesManagedUpdate-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    const auto root = std::filesystem::path(temp) / name;
    CHECK(std::filesystem::create_directory(root));
    UpdateTest::Run(root);
    CHECK(std::filesystem::remove_all(root) > 0);
    return 0;
#else
    (void)argc;
    (void)argv;
    return 125;
#endif
}
