// fresh管理領域の公開境界・故障・実process中断を検証する。資産本体は公開しない。
#include "Tools/AssetCook/CookManagedStoreObservation.h"
#include "Tools/AssetCook/CookManagedStoreInitializationTestAccess.h"
#include "Tools/AssetCook/CookDestinationLock.h"
#include "Tools/AssetCook/NativeCookPath.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <exception>
#include <fstream>
#include <source_location>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace StoreInitTest
{
    struct Failure
    {
    };
    bool bAliasCleanupFailed = false;
} // namespace StoreInitTest
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            throw StoreInitTest::Failure{};                                                                            \
        }                                                                                                              \
    } while (false)
namespace StoreInitTest
{
    using namespace NorvesLib::Tools::AssetCook;
    using Text = NorvesLib::Core::Container::AnsiString;
    template <class T> using Array = NorvesLib::Core::Container::VariableArray<T>;
    using Result = CookManagedStoreResult;
    constexpr char StoreId[] = "11111111111111111111111111111111";
    constexpr char OwnerId[] = "22222222222222222222222222222222";
    void Write(const std::filesystem::path& p, const Text& s)
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        CHECK(f);
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
        f.close();
        CHECK(!f.fail());
    }
    Text Read(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        CHECK(f);
        Text s;
        char c;
        while (f.get(c))
        {
            s.push_back(c);
        }
        CHECK(f.eof());
        return s;
    }
#if defined(_WIN32)
    struct Fixture
    {
        std::filesystem::path Root, Spec;
        Fixture()
        {
            char name[96];
            std::snprintf(name, sizeof(name), "norves-store-init-%lu-%llu", GetCurrentProcessId(),
                          static_cast<unsigned long long>(GetTickCount64()));
            Root = std::filesystem::temp_directory_path() / name;
            CHECK(std::filesystem::create_directory(Root));
            Spec = Root / "spec.json";
            Write(Spec, "{}");
        }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }
        std::filesystem::path Workspace(const char* name)
        {
            auto p = Root / name;
            CHECK(std::filesystem::create_directory(p));
            return p;
        }
        CookOwnerResolveRequest Request(const std::filesystem::path& w, const char* leaf = "Runtime Long Name")
        {
            return {Spec, w / leaf, "manifest.json"};
        }
    };
    struct NativeIdentity
    {
        Text Id, Serial, Guid;
    };
    NativeIdentity Identity(const std::filesystem::path& p)
    {
        HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        FILE_ID_INFO id{};
        CHECK(GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id)));
        wchar_t full[32768]{}, canonical[50]{};
        auto n = GetFinalPathNameByHandleW(h, full, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
        CHECK(CloseHandle(h));
        CHECK(n > 49 && n < 32768);
        full[49] = 0;
        CHECK(GetVolumeNameForVolumeMountPointW(full, canonical, 50));
        NativeIdentity out;
        char hex[33]{};
        for (size_t i = 0; i < 16; ++i)
        {
            std::snprintf(hex + i * 2, 3, "%02x", id.FileId.Identifier[i]);
        }
        out.Id = hex;
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(id.VolumeSerialNumber));
        out.Serial = hex;
        for (size_t i = 11; i < 47; ++i)
        {
            auto c = canonical[i];
            if (c >= L'A' && c <= L'F')
            {
                c += L'a' - L'A';
            }
            out.Guid.push_back(static_cast<char>(c));
        }
        return out;
    }
    Text Field(const char* key, const Text& value)
    {
        Text s = "\"";
        s.append(key);
        s.append("\":\"");
        s.append(value);
        s.append("\"");
        return s;
    }
    Text Header(const std::filesystem::path& w, const Text& storeId = StoreId)
    {
        auto wi = Identity(w), si = Identity(w / ".norves-assetcook");
        Text s = "{\"producer\":\"NorvesLib.AssetCook\",\"schema\":1,";
        s.append(Field("store_id", storeId));
        s.push_back(',');
        s.append(Field("volume_guid", wi.Guid));
        s.push_back(',');
        s.append(Field("volume_serial", wi.Serial));
        s.push_back(',');
        s.append(Field("workspace_id", wi.Id));
        s.push_back(',');
        s.append(Field("store_directory_id", si.Id));
        s.push_back('}');
        return s;
    }
    Text Claim(const char* leaf, const Text& id, const Text& owner, size_t n = 1)
    {
        char cid[33];
        std::snprintf(cid, sizeof(cid), "%032llx", static_cast<unsigned long long>(n));
        Text s = "{";
        s.append(Field("claim_id", cid));
        s.push_back(',');
        s.append(Field("leaf", leaf));
        s.push_back(',');
        s.append(Field("directory_id", id));
        s.push_back(',');
        s.append(Field("owner_id", owner));
        s.push_back('}');
        return s;
    }
    Text Index(const Text& rows = "")
    {
        Text s = "{\"schema\":1,";
        s.append(Field("store_id", StoreId));
        s.append(",\"generation\":\"0000000000000001\",\"roots\":[");
        s.append(rows);
        s.append("]}");
        return s;
    }
    void Init(const std::filesystem::path& w)
    {
        CHECK(std::filesystem::create_directory(w / ".norves-assetcook"));
        Write(w / ".norves-assetcook" / "header.json", Header(w));
        Write(w / ".norves-assetcook" / "roots.json", Index());
    }
    Text Bind(const CookOwnerResolveRequest& r)
    {
        CookResolvedOwnerBinding o;
        Text e;
        CHECK(ResolveCookOwnerBinding(r, o, e));
        return o.ExpectedBinding.OwnerId;
    }
    Text Own(const CookOwnerResolveRequest& r)
    {
        CHECK(std::filesystem::create_directory(r.FinalRuntimeRoot));
        Text leaf;
        const auto filename = r.FinalRuntimeRoot.filename();
        for (auto c : filename.native())
        {
            CHECK(c >= 32 && c < 127);
            leaf.push_back(static_cast<char>(c));
        }
        auto row = Claim(leaf.c_str(), Identity(r.FinalRuntimeRoot).Id, Bind(r));
        Write(r.FinalRuntimeRoot.parent_path() / ".norves-assetcook" / "roots.json", Index(row));
        return row;
    }
    struct SnapshotEntry
    {
        std::filesystem::path Path;
        int64_t Stamp = 0;
        uint64_t Hash = 0, Size = 0, VolumeSerial = 0;
        NorvesLib::Core::Container::FixedArray<uint8_t, 16> FileId;
        bool bFile = false;
    };
    Array<SnapshotEntry> Snapshot(const std::filesystem::path& p)
    {
        Array<SnapshotEntry> out;
        for (const auto& e : std::filesystem::recursive_directory_iterator(p))
        {
            SnapshotEntry a;
            a.Path = e.path();
            // 列挙cacheのdirectory時刻は子の作成直後に古い場合がある。全entryをhandleで再観測する。
            struct SnapshotHandle
            {
                HANDLE Value = INVALID_HANDLE_VALUE;
                ~SnapshotHandle()
                {
                    if (Value != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(Value);
                    }
                }
            } h;
            h.Value = CreateFileW(e.path().c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            CHECK(h.Value != INVALID_HANDLE_VALUE);
            FILE_ATTRIBUTE_TAG_INFO tag{};
            CHECK(GetFileType(h.Value) == FILE_TYPE_DISK);
            CHECK(GetFileInformationByHandleEx(h.Value, FileAttributeTagInfo, &tag, sizeof(tag)));
            CHECK((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0);
            a.bFile = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            if (a.bFile)
            {
                auto bytes = Read(e.path());
                a.Size = bytes.size();
                a.Hash = 14695981039346656037ull;
                for (unsigned char c : bytes)
                {
                    a.Hash = (a.Hash ^ c) * 1099511628211ull;
                }
            }
            FILE_BASIC_INFO basic{};
            FILE_ID_INFO identity{};
            CHECK(GetFileInformationByHandleEx(h.Value, FileBasicInfo, &basic, sizeof(basic)));
            CHECK(GetFileInformationByHandleEx(h.Value, FileIdInfo, &identity, sizeof(identity)));
            a.Stamp = basic.LastWriteTime.QuadPart;
            a.VolumeSerial = identity.VolumeSerialNumber;
            std::memcpy(a.FileId.data(), identity.FileId.Identifier, 16);
            const auto handle = h.Value;
            h.Value = INVALID_HANDLE_VALUE;
            CHECK(CloseHandle(handle));
            out.push_back(std::move(a));
        }
        std::sort(out.begin(), out.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.Path.native() < b.Path.native();
                  });
        return out;
    }
    void Unchanged(const Array<SnapshotEntry>& a, const Array<SnapshotEntry>& b, uint_least32_t callerLine)
    {
        CHECK(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            const bool bSame = a[i].Path == b[i].Path && a[i].Stamp == b[i].Stamp && a[i].Hash == b[i].Hash &&
                               a[i].Size == b[i].Size && a[i].bFile == b[i].bFile &&
                               a[i].VolumeSerial == b[i].VolumeSerial &&
                               std::memcmp(a[i].FileId.data(), b[i].FileId.data(), 16) == 0;
            if (!bSame)
            {
                Text before, after;
                CHECK(NorvesLib::Tools::AssetCook::Detail::EncodeCookPathUtf8(a[i].Path, before, false));
                CHECK(NorvesLib::Tools::AssetCook::Detail::EncodeCookPathUtf8(b[i].Path, after, false));
                std::fprintf(
                    stderr,
                    "store_snapshot_difference caller_line=%u entry=%zu before=%s after=%s file=%d/%d size=%llu/%llu hash=%016llx/%016llx stamp=%lld/%lld identity_equal=%d\n",
                    static_cast<unsigned>(callerLine), i, before.c_str(), after.c_str(), a[i].bFile, b[i].bFile,
                    static_cast<unsigned long long>(a[i].Size), static_cast<unsigned long long>(b[i].Size),
                    static_cast<unsigned long long>(a[i].Hash), static_cast<unsigned long long>(b[i].Hash),
                    static_cast<long long>(a[i].Stamp), static_cast<long long>(b[i].Stamp),
                    a[i].VolumeSerial == b[i].VolumeSerial &&
                        std::memcmp(a[i].FileId.data(), b[i].FileId.data(), 16) == 0);
            }
            CHECK(bSame);
        }
    }
    CookManagedStoreObservation Observe(const CookOwnerResolveRequest& r, Result expected,
                                        const std::filesystem::path& snapshotRoot = {},
                                        std::source_location location = std::source_location::current())
    {
        auto before = snapshotRoot.empty() ? Array<SnapshotEntry>{} : Snapshot(snapshotRoot);
        CookManagedStoreObservation out;
        out.CurrentRootClaimId = "held";
        Text error;
        auto result = ObserveCookManagedStore(r, out, error);
        if (result != expected)
        {
            std::fprintf(stderr, "expected %u got %u: %s\n", unsigned(expected), unsigned(result), error.c_str());
            CHECK(false);
        }
        if (result == Result::Observed || result == Result::StoreMissing)
        {
            CHECK(error.empty());
        }
        else
        {
            CHECK(!error.empty());
            CHECK(out.CurrentRootClaimId == "held" && out.Stores.empty());
        }
        if (!snapshotRoot.empty())
        {
            Unchanged(before, Snapshot(snapshotRoot), location.line());
        }
        return out;
    }
    std::filesystem::path Short(const std::filesystem::path& path)
    {
        wchar_t buf[32768]{};
        const auto n = GetShortPathNameW(path.c_str(), buf, 32768);
        CHECK(n && n < 32768);
        return std::filesystem::path(buf);
    }
    bool Ascii(const std::filesystem::path& path)
    {
        for (auto c : path.native())
        {
            if (c < 32 || c >= 127)
            {
                return false;
            }
        }
        return true;
    }
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

    namespace Access = NorvesLib::Tools::AssetCook::Detail;
    using InitResult = CookManagedStoreInitializationResult;
    using Fault = Access::CookStoreInitFault;
    using Point = Access::CookStoreInitPoint;
    constexpr char FixedStage[] = ".assetcook-store-stage-00000000000000000000000000000001";
    CookManagedStoreObservation Initialize(const CookOwnerResolveRequest& r, InitResult expected,
                                           const Access::CookStoreInitProbe* probe = nullptr)
    {
        const bool bRootPresentBefore = std::filesystem::exists(r.FinalRuntimeRoot);
        CookManagedStoreObservation out;
        out.CurrentRootClaimId = "held";
        Text error;
        const auto result = probe ? Access::InitializeNewCookManagedStoreForTest(r, *probe, out, error)
                                  : InitializeNewCookManagedStore(r, out, error);
        if (result != expected)
        {
            std::fprintf(stderr, "init expected=%u actual=%u error=%s\n", unsigned(expected), unsigned(result),
                         error.c_str());
            CHECK(false);
        }
        if (result == InitResult::Created)
        {
            CHECK(error.empty());
            CHECK(!out.bRuntimeRootClaimed);
        }
        else
        {
            CHECK(!error.empty());
            CHECK(out.CurrentRootClaimId == "held" && out.Stores.empty());
        }
        CHECK(result == InitResult::PublishedButError ||
              std::filesystem::exists(r.FinalRuntimeRoot) == bRootPresentBefore);
        if (result == InitResult::Created)
        {
            CHECK(!bRootPresentBefore);
        }
        return out;
    }
    const CookManagedStoreView& Current(const CookManagedStoreObservation& out)
    {
        for (const auto& v : out.Stores)
        {
            if (v.bCurrentWorkspace)
            {
                return v;
            }
        }
        CHECK(false);
        return out.Stores[0];
    }
    void VerifyNew(const CookOwnerResolveRequest& r, const CookManagedStoreObservation& out)
    {
        const auto workspace = r.FinalRuntimeRoot.parent_path();
        const auto store = workspace / ".norves-assetcook";
        const auto& current = Current(out);
        CHECK(current.IndexGeneration == 1 && current.Roots.empty() && current.StoreId.size() == 32);
        CHECK(Read(store / "header.json") == Header(workspace, current.StoreId));
        Text expected = "{\"schema\":1,";
        expected.append(Field("store_id", current.StoreId));
        expected.append(",\"generation\":\"0000000000000001\",\"roots\":[]}");
        CHECK(Read(store / "roots.json") == expected);
        size_t count = 0;
        for (const auto& e : std::filesystem::directory_iterator(store))
        {
            CHECK(e.is_regular_file());
            ++count;
        }
        CHECK(count == 2);
        CHECK(!std::filesystem::exists(r.FinalRuntimeRoot));
        CHECK(!std::filesystem::exists(store / "pending") && !std::filesystem::exists(store / "states"));
        Observe(r, Result::Observed);
    }
    struct Trace
    {
        Text StageId, PublishedId;
        std::filesystem::path StagePath;
        bool bRenamed = false;
    };
    void TracePoint(Point point, const std::filesystem::path& stage, const std::filesystem::path& dest, void* data)
    {
        auto& trace = *static_cast<Trace*>(data);
        if (point == Point::StageCreated)
        {
            trace.StageId = Identity(stage).Id;
            trace.StagePath = stage;
        }
        if (point == Point::Renamed)
        {
            trace.PublishedId = Identity(dest).Id;
            trace.bRenamed = true;
        }
    }
    void AddForeign(Point point, const std::filesystem::path& stage, const std::filesystem::path& dest, void*)
    {
        (void)dest;
        if (point == Point::IndexWritten)
        {
            Write(stage / "foreign.keep", "do not delete");
        }
    }
    struct Race
    {
        Text DirectoryId;
    };
    void AddDestination(Point point, const std::filesystem::path&, const std::filesystem::path& dest, void* data)
    {
        if (point == Point::BeforeRename)
        {
            CHECK(std::filesystem::create_directory(dest));
            Write(dest / "foreign.keep", "target wins");
            static_cast<Race*>(data)->DirectoryId = Identity(dest).Id;
        }
    }
    void CrashPoint(Point point, const std::filesystem::path&, const std::filesystem::path&, void* data)
    {
        if (point == *static_cast<Point*>(data))
        {
            ExitProcess(73);
        }
    }
    int Child(int argc, char** argv)
    {
        if (argc != 5 || std::strcmp(argv[1], "--init-child"))
        {
            return 2;
        }
        const auto number = std::strtoul(argv[4], nullptr, 10);
        if (number > static_cast<unsigned>(Point::Renamed))
        {
            return 3;
        }
        Point point = static_cast<Point>(number);
        CookOwnerResolveRequest request{argv[2], std::filesystem::path(argv[3]) / "Runtime", "manifest.json"};
        Access::CookStoreInitProbe probe;
        probe.FirstStageLeaf = FixedStage;
        probe.Checkpoint = CrashPoint;
        probe.Context = &point;
        CookManagedStoreObservation out;
        Text error;
        (void)Access::InitializeNewCookManagedStoreForTest(request, probe, out, error);
        std::fprintf(stderr, "crash checkpoint not reached: %s\n", error.c_str());
        return 4;
    }
    void Crash(const CookOwnerResolveRequest& r, Point point)
    {
        wchar_t exe[32768]{};
        const DWORD size = GetModuleFileNameW(nullptr, exe, 32768);
        CHECK(size && size < 32768);
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
        token(L"--test=CookManagedStoreInitializationTest");
        token(L"--init-child");
        token(r.SpecPath.c_str());
        token(r.FinalRuntimeRoot.parent_path().c_str());
        wchar_t phase[20]{};
        std::swprintf(phase, 20, L"%u", static_cast<unsigned>(point));
        token(phase);
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
            std::fprintf(stderr, "child point=%u wait=%lu exit=%lu\n", static_cast<unsigned>(point), wait, code);
        }
        CHECK(wait == WAIT_OBJECT_0 && bCode && code == 73);
    }
    void Run()
    {
        Fixture f;
        auto w = f.Workspace("Fresh Workspace");
        auto r = f.Request(w);
        Trace trace;
        Access::CookStoreInitProbe probe;
        probe.FirstStageLeaf = FixedStage;
        probe.Checkpoint = TracePoint;
        probe.Context = &trace;
        auto out = Initialize(r, InitResult::Created, &probe);
        VerifyNew(r, out);
        CHECK(trace.bRenamed && trace.StageId == trace.PublishedId &&
              trace.PublishedId == Identity(w / ".norves-assetcook").Id);
        CHECK(!std::filesystem::exists(trace.StagePath));
        auto before = Snapshot(f.Root);
        Initialize(r, InitResult::StoreExists);
        Unchanged(before, Snapshot(f.Root), __LINE__);
        auto collision = f.Workspace("Stage Collision");
        CHECK(std::filesystem::create_directory(collision / FixedStage));
        Write(collision / FixedStage / "keep", "unknown stage");
        auto oldId = Identity(collision / FixedStage).Id;
        probe = {};
        probe.FirstStageLeaf = FixedStage;
        auto request = f.Request(collision);
        out = Initialize(request, InitResult::Created, &probe);
        VerifyNew(request, out);
        CHECK(Identity(collision / FixedStage).Id == oldId && Read(collision / FixedStage / "keep") == "unknown stage");
        auto unknown = f.Workspace("Unknown Store");
        CHECK(std::filesystem::create_directory(unknown / ".norves-assetcook"));
        Write(unknown / ".norves-assetcook" / "keep", "unknown");
        before = Snapshot(f.Root);
        Initialize(f.Request(unknown), InitResult::Conflict);
        Unchanged(before, Snapshot(f.Root), __LINE__);
        auto unknownRoot = f.Workspace("Unknown Root");
        auto unknownRequest = f.Request(unknownRoot);
        CHECK(std::filesystem::create_directory(unknownRequest.FinalRuntimeRoot));
        before = Snapshot(f.Root);
        Initialize(unknownRequest, InitResult::Conflict);
        Unchanged(before, Snapshot(f.Root), __LINE__);
        auto ancestor = f.Workspace("Ancestor");
        Init(ancestor);
        auto owned = f.Request(ancestor);
        Own(owned);
        auto nested = f.Request(owned.FinalRuntimeRoot);
        before = Snapshot(f.Root);
        Initialize(nested, InitResult::Conflict);
        Unchanged(before, Snapshot(f.Root), __LINE__);
        Write(w / ".norves-assetcook" / "pending", "retain");
        Initialize(r, InitResult::NeedsRecovery);
        CHECK(Read(w / ".norves-assetcook" / "pending") == "retain");
        CHECK(std::filesystem::remove(w / ".norves-assetcook" / "pending"));
        // 全pre-publish故障では固定storeを作らず、確認できないorphanを採用しない。
        const Fault early[] = {Fault::Rng,
                               Fault::StageOpen,
                               Fault::StageIdentity,
                               Fault::HeaderCreate,
                               Fault::IndexCreate,
                               Fault::HeaderPartialWrite,
                               Fault::IndexPartialWrite,
                               Fault::ZeroWrite,
                               Fault::Flush,
                               Fault::Seek,
                               Fault::ReadBack,
                               Fault::ByteMismatch,
                               Fault::Parse,
                               Fault::ChildClose,
                               Fault::Rename,
                               Fault::CleanupDisposition};
        for (size_t i = 0; i < sizeof(early) / sizeof(early[0]); ++i)
        {
            char name[50];
            std::snprintf(name, sizeof(name), "Early%zu", i);
            const auto parent = f.Workspace(name);
            auto q = f.Request(parent);
            probe = {};
            probe.FirstStageLeaf = FixedStage;
            probe.Fault = early[i];
            Initialize(q, InitResult::Error, &probe);
            CHECK(!std::filesystem::exists(parent / ".norves-assetcook"));
            Observe(q, Result::StoreMissing);
            const bool bOrphan = early[i] == Fault::StageOpen || early[i] == Fault::StageIdentity ||
                                 early[i] == Fault::ChildClose || early[i] == Fault::Rename ||
                                 early[i] == Fault::CleanupDisposition;
            CHECK(std::filesystem::exists(parent / FixedStage) == bOrphan);
        }
        // 不明childだけを残し、既知open handleのheader/indexだけを削除する。
        auto foreign = f.Workspace("Foreign Child");
        request = f.Request(foreign);
        probe = {};
        probe.FirstStageLeaf = FixedStage;
        probe.Checkpoint = AddForeign;
        Initialize(request, InitResult::Error, &probe);
        CHECK(Read(foreign / FixedStage / "foreign.keep") == "do not delete");
        CHECK(!std::filesystem::exists(foreign / FixedStage / "header.json") &&
              !std::filesystem::exists(foreign / FixedStage / "roots.json"));
        // 最終直前の別targetをno-replaceで保持する。
        auto race = f.Workspace("Rename Race");
        request = f.Request(race);
        Race competing;
        probe = {};
        probe.FirstStageLeaf = FixedStage;
        probe.Checkpoint = AddDestination;
        probe.Context = &competing;
        Initialize(request, InitResult::Error, &probe);
        CHECK(Identity(race / ".norves-assetcook").Id == competing.DirectoryId &&
              Read(race / ".norves-assetcook" / "foreign.keep") == "target wins");
        CHECK(std::filesystem::exists(race / FixedStage));
        // 公開後の失敗やmutex解除報告失敗では完成storeを保持し、outを更新しない。
        const Fault late[] = {Fault::AfterPublishIdentity, Fault::AfterPublishClose, Fault::AfterPublishObservation,
                              Fault::MutexRelease};
        for (size_t i = 0; i < sizeof(late) / sizeof(late[0]); ++i)
        {
            char name[50];
            std::snprintf(name, sizeof(name), "Late%zu", i);
            auto parent = f.Workspace(name);
            auto q = f.Request(parent);
            probe = {};
            probe.Fault = late[i];
            Initialize(q, InitResult::PublishedButError, &probe);
            out = Observe(q, Result::Observed);
            VerifyNew(q, out);
            Initialize(q, InitResult::StoreExists);
        }
        // stage名と不在runtime候補が一致しても、作成後にownerを差し替えず中止する。
        auto overlap = f.Workspace("Stage Root Collision");
        request = f.Request(overlap, FixedStage);
        probe = {};
        probe.FirstStageLeaf = FixedStage;
        Initialize(request, InitResult::Error, &probe);
        CHECK(!std::filesystem::exists(overlap / ".norves-assetcook"));
        // 実processを各境界で終了させ、orphanと完成storeを区別する。
        for (unsigned i = 0; i <= static_cast<unsigned>(Point::Renamed); ++i)
        {
            char name[50];
            std::snprintf(name, sizeof(name), "Crash%u", i);
            auto parent = f.Workspace(name);
            auto q = f.Request(parent, "Runtime");
            Crash(q, static_cast<Point>(i));
            if (i == static_cast<unsigned>(Point::Renamed))
            {
                out = Observe(q, Result::Observed);
                VerifyNew(q, out);
                CHECK(!std::filesystem::exists(parent / FixedStage));
            }
            else
            {
                CHECK(std::filesystem::exists(parent / FixedStage));
                const auto id = Identity(parent / FixedStage).Id;
                Observe(q, Result::StoreMissing);
                Initialize(q, InitResult::Created);
                CHECK(Identity(parent / FixedStage).Id == id);
            }
        }
        std::puts("store_init_process_crash_checked=1");
        // ASCII short aliasとSUBSTから同じ物理workspaceに作成する。
        auto aliasParent = f.Workspace("Long Alias Workspace");
        auto shortParent = Short(aliasParent);
        request = f.Request(shortParent);
        out = Initialize(request, InitResult::Created);
        VerifyNew(request, out);
        std::printf("store_init_short_alias_distinct=%d\n", shortParent != aliasParent);
        auto mapped = f.Workspace("Mapped");
        bool bSubst = false;
        {
            DriveAlias drive(mapped);
            if (drive.bMapped)
            {
                request = f.Request(drive.Root());
                out = Initialize(request, InitResult::Created);
                VerifyNew(request, out);
                bSubst = true;
            }
        }
        CHECK(!bAliasCleanupFailed);
        std::printf("store_init_subst_checked=%d\n", bSubst);
        auto unicode = f.Root / std::filesystem::path(u8"資料\U0001f43a");
        CHECK(std::filesystem::create_directory(unicode));
        auto shortUnicode = Short(unicode);
        bool bUnicode = false;
        if (Ascii(shortUnicode))
        {
            request = f.Request(shortUnicode);
            out = Initialize(request, InitResult::Created);
            VerifyNew(request, out);
            bUnicode = true;
        }
        std::printf("store_init_unicode_ascii_alias=%d\n", bUnicode);
        // 8.3の割当て自体は仮定せず、実際に候補rootが現れた境界を条件flagにする。
        auto stageAlias = f.Workspace("Prospective Stage Alias");
        CHECK(std::filesystem::create_directory(stageAlias / FixedStage));
        auto shortStage = Short(stageAlias / FixedStage);
        const bool bStagePotential = shortStage.filename() != std::filesystem::path(FixedStage);
        CHECK(std::filesystem::remove(stageAlias / FixedStage));
        bool bStageAlias = false;
        if (bStagePotential)
        {
            struct AliasProbe
            {
                std::filesystem::path Root;
                bool bPresent = false;
            } aliasProbe;
            request = f.Request(stageAlias);
            request.FinalRuntimeRoot = stageAlias / shortStage.filename();
            aliasProbe.Root = request.FinalRuntimeRoot;
            probe = {};
            probe.FirstStageLeaf = FixedStage;
            probe.Context = &aliasProbe;
            probe.Checkpoint = [](Point point, const std::filesystem::path&, const std::filesystem::path&, void* data)
            {
                if (point == Point::StageCreated)
                {
                    auto& state = *static_cast<AliasProbe*>(data);
                    state.bPresent = std::filesystem::exists(state.Root);
                }
            };
            CookManagedStoreObservation held;
            held.CurrentRootClaimId = "held";
            Text error;
            const auto result = Access::InitializeNewCookManagedStoreForTest(request, probe, held, error);
            bStageAlias = aliasProbe.bPresent;
            if (bStageAlias)
            {
                CHECK(result == InitResult::Error && held.CurrentRootClaimId == "held");
                CHECK(!std::filesystem::exists(stageAlias / ".norves-assetcook"));
            }
            else
            {
                CHECK(result == InitResult::Created || result == InitResult::PublishedButError);
                if (result == InitResult::Created)
                {
                    CHECK(!std::filesystem::exists(request.FinalRuntimeRoot));
                }
                else
                {
                    CHECK(held.CurrentRootClaimId == "held" && std::filesystem::exists(request.FinalRuntimeRoot));
                    CHECK(Identity(request.FinalRuntimeRoot).Id == Identity(stageAlias / ".norves-assetcook").Id);
                }
            }
        }
        std::printf("store_init_stage_alias_checked=%d\n", bStageAlias);
        auto storeAlias = f.Workspace("Prospective Store Alias");
        CHECK(std::filesystem::create_directory(storeAlias / ".norves-assetcook"));
        auto shortStore = Short(storeAlias / ".norves-assetcook");
        const bool bStorePotential = shortStore.filename() != std::filesystem::path(".norves-assetcook");
        CHECK(std::filesystem::remove(storeAlias / ".norves-assetcook"));
        bool bStoreAlias = false;
        if (bStorePotential)
        {
            request = f.Request(storeAlias);
            request.FinalRuntimeRoot = storeAlias / shortStore.filename();
            CookManagedStoreObservation held;
            held.CurrentRootClaimId = "held";
            Text error;
            const auto result = InitializeNewCookManagedStore(request, held, error);
            bStoreAlias = std::filesystem::exists(request.FinalRuntimeRoot);
            if (bStoreAlias)
            {
                CHECK(result == InitResult::PublishedButError && held.CurrentRootClaimId == "held");
            }
            else
            {
                CHECK(result == InitResult::Created);
            }
            CHECK(std::filesystem::exists(storeAlias / ".norves-assetcook"));
            auto good = f.Request(storeAlias);
            VerifyNew(good, Observe(good, Result::Observed));
        }
        std::printf("store_init_store_alias_checked=%d\n", bStoreAlias);
        auto caseParent = f.Workspace("Case Sensitive Init");
        HANDLE caseHandle = CreateFileW(caseParent.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        CHECK(caseHandle != INVALID_HANDLE_VALUE);
        FILE_CASE_SENSITIVE_INFO caseInfo{};
        caseInfo.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR;
        const bool bCase =
            SetFileInformationByHandle(caseHandle, FileCaseSensitiveInfo, &caseInfo, sizeof(caseInfo)) != 0;
        CHECK(CloseHandle(caseHandle));
        if (bCase)
        {
            request = f.Request(caseParent);
            out = Initialize(request, InitResult::Created);
            VerifyNew(request, out);
        }
        std::printf("store_init_case_sensitive_checked=%d\n", bCase);
        auto reparseTarget = f.Workspace("Reparse Target");
        const auto reparseLink = f.Root / "Reparse Workspace";
        const bool bReparse =
            CreateSymbolicLinkW(reparseLink.c_str(), reparseTarget.c_str(),
                                SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
        if (bReparse)
        {
            Initialize(f.Request(reparseLink), InitResult::Error);
            CHECK(!std::filesystem::exists(reparseTarget / ".norves-assetcook"));
            CHECK(std::filesystem::remove(reparseLink));
        }
        std::printf("store_init_reparse_checked=%d\n", bReparse);
        CHECK(Read(f.Spec) == "{}");
        std::puts(
            "COOK_STORE_INITIALIZATION result=pass fresh_stage_no_replace_ids_readback_crash_orphans_published_error_no_runtime");
    }
#endif
} // namespace StoreInitTest
int main(int argc, char** argv)
{
#if !defined(_WIN32)
    (void)argc;
    (void)argv;
    return 125;
#else
    try
    {
        if (argc > 1)
        {
            return StoreInitTest::Child(argc, argv);
        }
        StoreInitTest::Run();
        return StoreInitTest::bAliasCleanupFailed ? 1 : 0;
    }
    catch (...)
    {
        return 1;
    }
#endif
}
