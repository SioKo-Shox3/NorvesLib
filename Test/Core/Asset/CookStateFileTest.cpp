// 実Windowsのhandle新規保存と不在/失敗/競合で既存fileを保持する契約を検証する。
#include "Tools/AssetCook/CookStateFile.h"
#include "Tools/AssetCook/CookStateFileTestAccess.h"
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
namespace StateFileTest
{
    using namespace NorvesLib::Tools::AssetCook;
    using IOText = NorvesLib::Core::Container::AnsiString;
    using IOBytes = NorvesLib::Core::Container::VariableArray<uint8_t>;
    namespace ProbeAPI = NorvesLib::Tools::AssetCook::Detail;
    void Write(const std::filesystem::path& p, const char* value)
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        CHECK(f);
        f.write(value, static_cast<std::streamsize>(std::strlen(value)));
        f.close();
        CHECK(!f.fail());
    }
    IOBytes Read(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        CHECK(f);
        IOBytes b;
        char c;
        while (f.get(c))
        {
            b.push_back(static_cast<uint8_t>(c));
        }
        CHECK(f.eof());
        return b;
    }
    size_t Files(const std::filesystem::path& root)
    {
        size_t n = 0;
        for (const auto& p : std::filesystem::directory_iterator(root))
        {
            if (p.is_regular_file())
            {
                ++n;
            }
        }
        return n;
    }
    CookStateFileRequest Request(const std::filesystem::path& root, const char* name)
    {
        CookStateFileRequest r;
        r.StatePath = root / name;
        r.RuntimeRoot = root / "runtime";
        r.ExpectedBinding.OwnerId = "0123456789abcdef0123456789abcdef";
        r.ExpectedBinding.RuntimeRootIdentity = r.RuntimeRoot.generic_string().c_str();
        r.ExpectedBinding.ManifestName = "manifest.json";
        return r;
    }
    CookOwnedState State(const CookStateFileRequest& r)
    {
        CookOwnedState s;
        s.Binding = r.ExpectedBinding;
        return s;
    }
    void LoadResult(const CookStateFileRequest& r, CookStateLoadResult expected)
    {
        CookOwnedState held;
        held.Generation = 123;
        held.Binding.OwnerId = "held";
        IOText error;
        CHECK(LoadCookOwnedState(r, held, error) == expected);
        CHECK(held.Generation == 123 && held.Binding.OwnerId == "held");
        CHECK((expected == CookStateLoadResult::Missing) == error.empty());
    }
#if defined(_WIN32)
    void Junction(const std::filesystem::path& link, const std::filesystem::path& target)
    {
        const auto a = link.string(), b = target.string();
        char command[4096];
        std::snprintf(command, sizeof(command), "cmd /c mklink /J \"%s\" \"%s\" >nul", a.c_str(), b.c_str());
        CHECK(std::system(command) == 0);
    }
    void Grow(const std::filesystem::path& path, uint64_t size)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(size);
        CHECK(SetFilePointerEx(h, offset, nullptr, FILE_BEGIN));
        CHECK(SetEndOfFile(h));
        CHECK(CloseHandle(h));
    }
    struct Concurrent
    {
        const CookStateFileRequest* Request = nullptr;
        const CookOwnedState* State = nullptr;
        HANDLE Start = nullptr;
        bool bResult = false;
        IOText Error;
    };
    DWORD WINAPI SaveThread(void* raw)
    {
        auto& c = *static_cast<Concurrent*>(raw);
        CHECK(WaitForSingleObject(c.Start, INFINITE) == WAIT_OBJECT_0);
        c.bResult = WriteNewCookOwnedState(*c.Request, *c.State, c.Error);
        return 0;
    }
#endif
} // namespace StateFileTest
int main()
{
#if !defined(_WIN32)
    return 125;
#else
    using namespace StateFileTest;
    char name[100];
    std::snprintf(name, sizeof(name), "norves-state-file-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
    const auto root = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(root));
    const auto r = Request(root, "state.json");
    const auto state = State(r);
    IOText error;
    CHECK(std::filesystem::create_directory(r.RuntimeRoot));
    LoadResult(r, CookStateLoadResult::Missing);
    auto invalid = r;
    invalid.ExpectedBinding.OwnerId = "invalid";
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.ExpectedBinding.ManifestName = "../bad";
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.StatePath = root / "missing/child.json";
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    CHECK(Files(root) == 0);
    CHECK(WriteNewCookOwnedState(r, state, error));
    CHECK(error.empty());
    CookStateFileRequest restarted = r;
    CookOwnedState loaded;
    CHECK(LoadCookOwnedState(restarted, loaded, error) == CookStateLoadResult::Loaded);
    CHECK(loaded.Binding.OwnerId == r.ExpectedBinding.OwnerId && loaded.Generation == 1);
    const auto original = Read(r.StatePath);
    CHECK(!WriteNewCookOwnedState(r, state, error));
    CHECK(Read(r.StatePath) == original);
    invalid = r;
    invalid.StatePath = root / "STATE.JSON";
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    CHECK(Read(r.StatePath) == original);
    invalid = r;
    invalid.ExpectedBinding.OwnerId = "1123456789abcdef0123456789abcdef";
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    CHECK(Read(r.StatePath) == original);
    Write(root / "parent-file", "held");
    invalid = r;
    invalid.StatePath = root / "parent-file/child.json";
    LoadResult(invalid, CookStateLoadResult::Error);
    const auto directory = root / "directory";
    CHECK(std::filesystem::create_directory(directory));
    invalid = r;
    invalid.StatePath = directory;
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    CHECK(std::filesystem::is_directory(directory));
    CHECK(std::filesystem::is_directory(r.RuntimeRoot));
    invalid = r;
    invalid.StatePath = r.RuntimeRoot / "inside.json";
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    invalid = r;
    invalid.StatePath = r.RuntimeRoot;
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.RuntimeRoot = root / "parent-file/child";
    invalid.ExpectedBinding.RuntimeRootIdentity = invalid.RuntimeRoot.generic_string().c_str();
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    auto missingRuntime = r;
    missingRuntime.StatePath = root / "missing-runtime.state.json";
    missingRuntime.RuntimeRoot = root / "not-created-runtime";
    missingRuntime.ExpectedBinding.RuntimeRootIdentity = missingRuntime.RuntimeRoot.generic_string().c_str();
    LoadResult(missingRuntime, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(missingRuntime, State(missingRuntime), error));
    CHECK(!std::filesystem::exists(missingRuntime.StatePath));
    invalid.StatePath = root / "state.json.";
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.StatePath = root / "state.json ";
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.StatePath = root / "CON.json";
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.StatePath = root / std::filesystem::path(u8"状態.json");
    LoadResult(invalid, CookStateLoadResult::Error);
    const auto link = root / "junction";
    Junction(link, directory);
    invalid = r;
    invalid.StatePath = link / "child.json";
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    invalid = r;
    invalid.StatePath = link;
    LoadResult(invalid, CookStateLoadResult::Error);
    invalid = r;
    invalid.RuntimeRoot = link;
    invalid.ExpectedBinding.RuntimeRootIdentity = link.generic_string().c_str();
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(std::filesystem::remove(link));
    const auto bad = Request(root, "bad.json");
    Write(bad.StatePath, "{bad");
    LoadResult(bad, CookStateLoadResult::Error);
    Write(bad.StatePath, "");
    LoadResult(bad, CookStateLoadResult::Error);
    CHECK(std::filesystem::remove(bad.StatePath));
    Grow(bad.StatePath, MaximumCookStateBytes);
    LoadResult(bad, CookStateLoadResult::Error);
    CHECK(std::filesystem::remove(bad.StatePath));
    Grow(bad.StatePath, MaximumCookStateBytes + 1);
    LoadResult(bad, CookStateLoadResult::Error);
    CHECK(std::filesystem::remove(bad.StatePath));
    HANDLE locked =
        CreateFileW(r.StatePath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    LoadResult(r, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(r, state, error));
    CHECK(CloseHandle(locked));
    CHECK(Read(r.StatePath) == original);
    const auto failures = Request(root, "failure.json");
    const auto failureState = State(failures);
    for (const auto fault : {ProbeAPI::CookStateFileFault::AfterCreate, ProbeAPI::CookStateFileFault::PartialWrite,
                             ProbeAPI::CookStateFileFault::ZeroWrite, ProbeAPI::CookStateFileFault::Flush,
                             ProbeAPI::CookStateFileFault::Seek, ProbeAPI::CookStateFileFault::ReadBack,
                             ProbeAPI::CookStateFileFault::ByteMismatch,
                             ProbeAPI::CookStateFileFault::ParseVerification, ProbeAPI::CookStateFileFault::Rename})
    {
        const auto count = Files(root);
        ProbeAPI::CookStateFileProbe probe;
        probe.Fault = fault;
        CHECK(!ProbeAPI::WriteNewCookOwnedStateForTest(failures, failureState, probe, error));
        CHECK(!error.empty());
        CHECK(!std::filesystem::exists(failures.StatePath) && Files(root) == count);
    }
    const auto collision = Request(root, "collision.json");
    ProbeAPI::CookStateFileProbe probe;
    probe.FirstTempLeaf = "occupied.tmp";
    Write(root / "occupied.tmp", "foreign sentinel");
    const auto foreign = Read(root / "occupied.tmp");
    CHECK(ProbeAPI::WriteNewCookOwnedStateForTest(collision, State(collision), probe, error));
    CHECK(Read(root / "occupied.tmp") == foreign);
    const auto raced = Request(root, "raced.json");
    probe = {};
    probe.BeforePublish = [](const std::filesystem::path& destination, void*)
    {
        Write(destination, "race winner");
    };
    const auto beforeRace = Files(root);
    CHECK(!ProbeAPI::WriteNewCookOwnedStateForTest(raced, State(raced), probe, error));
    CHECK(Files(root) == beforeRace + 1);
    const auto raceBytes = Read(raced.StatePath);
    CHECK(raceBytes.size() == 11 && std::memcmp(raceBytes.data(), "race winner", 11) == 0);
    const auto orphan = Request(root, "orphan.json");
    probe = {};
    probe.Fault = ProbeAPI::CookStateFileFault::CleanupDisposition;
    const auto beforeOrphan = Files(root);
    CHECK(!ProbeAPI::WriteNewCookOwnedStateForTest(orphan, State(orphan), probe, error));
    CHECK(std::strstr(error.c_str(), "cleanup_failed_orphan=") && Files(root) == beforeOrphan + 1 &&
          !std::filesystem::exists(orphan.StatePath));
    const auto concurrent = Request(root, "concurrent.json");
    const auto concurrentState = State(concurrent);
    const auto beforeConcurrent = Files(root);
    HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(start);
    Concurrent contexts[2] = {{&concurrent, &concurrentState, start}, {&concurrent, &concurrentState, start}};
    HANDLE threads[2] = {CreateThread(nullptr, 0, SaveThread, &contexts[0], 0, nullptr),
                         CreateThread(nullptr, 0, SaveThread, &contexts[1], 0, nullptr)};
    CHECK(threads[0] && threads[1]);
    CHECK(SetEvent(start));
    CHECK(WaitForMultipleObjects(2, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
    CHECK(CloseHandle(threads[0]) && CloseHandle(threads[1]) && CloseHandle(start));
    CHECK(contexts[0].bResult != contexts[1].bResult);
    CHECK(LoadCookOwnedState(concurrent, loaded, error) == CookStateLoadResult::Loaded);
    CHECK(Files(root) == beforeConcurrent + 1);
    // 8.3の生成が無効なvolumeでは、その条件を明示して通常inside-root検査を維持する。
    wchar_t shortBuffer[32768]{};
    const DWORD shortLength = GetShortPathNameW(r.RuntimeRoot.c_str(), shortBuffer, 32768);
    CHECK(shortLength > 0 && shortLength < 32768);
    const std::filesystem::path shortRoot(shortBuffer);
    invalid = r;
    invalid.StatePath = shortRoot / "inside-short.json";
    LoadResult(invalid, CookStateLoadResult::Error);
    CHECK(!WriteNewCookOwnedState(invalid, state, error));
    std::printf("short_alias_distinct=%d\n", shortRoot.native() != r.RuntimeRoot.native());
    // runnerのworkspaceとTEMPが別volumeなら実拒否を確認する。勝手にdriveを作らない。
    const auto other = std::filesystem::current_path() / (IOText(name) + "-other").c_str();
    CHECK(std::filesystem::create_directory(other));
    invalid = r;
    invalid.StatePath = other / "state.json";
    wchar_t rootVolume[64]{}, otherVolume[64]{};
    const bool bVolumesKnown = GetVolumeNameForVolumeMountPointW(root.root_path().c_str(), rootVolume, 64) &&
                               GetVolumeNameForVolumeMountPointW(other.root_path().c_str(), otherVolume, 64);
    if (bVolumesKnown && CompareStringOrdinal(rootVolume, -1, otherVolume, -1, TRUE) != CSTR_EQUAL)
    {
        CHECK(!WriteNewCookOwnedState(invalid, state, error));
        CHECK(!std::filesystem::exists(invalid.StatePath));
        std::puts("different_volume_checked=1");
    }
    else
    {
        std::puts("different_volume_checked=0");
    }
    CHECK(std::filesystem::remove(other));
    CHECK(Read(r.StatePath) == original && Read(root / "occupied.tmp") == foreign);
    std::filesystem::remove_all(root);
    std::puts("COOK_STATE_FILE result=pass native_handles_no_replace_scope_load_faults_competing_writers");
    return 0;
#endif
}
