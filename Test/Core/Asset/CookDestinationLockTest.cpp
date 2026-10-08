// 実thread/processの協調排他とmutex消滅/abandonedを検証する。資産publicationは行わない。
#include "Tools/AssetCook/CookDestinationLock.h"
#include "Tools/AssetCook/CookDestinationLockTestAccess.h"
#include "Container/VariableArray.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <exception>
#include <thread>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace DestinationLockTest
{
    struct Failure
    {
    };
    struct StandardFailure : std::exception
    {
    };
    bool bCleanupFailed = false;
} // namespace DestinationLockTest
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            throw DestinationLockTest::Failure{};                                                                      \
        }                                                                                                              \
    } while (false)
namespace DestinationLockTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace Probe = NorvesLib::Tools::AssetCook::Detail;
    using LockText = NorvesLib::Core::Container::AnsiString;
    using Wide = NorvesLib::Core::Container::VariableArray<wchar_t>;
    using Result = CookDestinationLockResult;
    enum class Mode
    {
        Pass,
        Reject,
        Standard,
        Other,
        Nested,
        Peer,
        CreateRoot
    };
    struct Work
    {
        unsigned Calls = 0;
        bool bAbandoned = false;
        Mode Action = Mode::Pass;
        CookDestinationLockRequest Other;
    };
    bool Run(const CookDestinationLockContext& context, void* data, LockText& error)
    {
        auto& work = *static_cast<Work*>(data);
        ++work.Calls;
        work.bAbandoned = context.bAbandoned;
        CHECK(!context.CanonicalFinalRuntimeRootIdentity.empty() && context.CanonicalVolumeGuid.size() == 36);
        if (work.Action == Mode::Reject)
        {
            error = "fixture_rejection";
            return false;
        }
        if (work.Action == Mode::Standard)
        {
            throw StandardFailure{};
        }
        if (work.Action == Mode::Other)
        {
            throw 71;
        }
        if (work.Action == Mode::Nested)
        {
            Work nested;
            LockText inner;
            CHECK(WithCookDestinationLock(work.Other, Run, &nested, inner) == Result::Error);
            CHECK(nested.Calls == 0 && std::strstr(inner.c_str(), "nested_invocation"));
        }
        if (work.Action == Mode::Peer)
        {
            Work peer;
            LockText otherError;
            Result result = Result::Error;
            std::thread thread(
                [&]
                {
                    result = WithCookDestinationLock(work.Other, Run, &peer, otherError);
                });
            thread.join();
            CHECK(result == Result::Busy && peer.Calls == 0 && otherError.empty());
        }
        if (work.Action == Mode::CreateRoot)
        {
            CHECK(!context.bFinalRuntimeRootPresent);
            CHECK(std::filesystem::create_directory(context.FinalRuntimeRootLocator));
            Work peer;
            LockText otherError;
            Result result = Result::Error;
            std::thread thread(
                [&]
                {
                    result = WithCookDestinationLock(work.Other, Run, &peer, otherError);
                });
            thread.join();
            CHECK(result == Result::Busy && peer.Calls == 0);
        }
        return true;
    }
    void Good(const CookDestinationLockRequest& request)
    {
        Work work;
        LockText error;
        CHECK(WithCookDestinationLock(request, Run, &work, error) == Result::Executed);
        CHECK(work.Calls == 1 && error.empty());
    }
    void GoodPeer(const CookDestinationLockRequest& request)
    {
        Work work;
        LockText error;
        Result result = Result::Error;
        std::thread thread(
            [&]
            {
                result = WithCookDestinationLock(request, Run, &work, error);
            });
        thread.join();
        CHECK(result == Result::Executed && work.Calls == 1 && error.empty());
    }
    LockText Name(const CookDestinationLockRequest& request)
    {
        LockText name, error;
        CHECK(Probe::InspectCookDestinationMutexNameForTest(request, name, error));
        CHECK(error.empty());
        return name;
    }
    void Bad(const CookDestinationLockRequest& request)
    {
        Work work;
        LockText error;
        CHECK(WithCookDestinationLock(request, Run, &work, error) == Result::Error);
        CHECK(work.Calls == 0 && !error.empty());
    }
    void Write(const std::filesystem::path& path, const char* text)
    {
        std::ofstream f(path, std::ios::binary);
        CHECK(f);
        f << text;
        f.close();
        CHECK(!f.fail());
    }
    size_t Count(const std::filesystem::path& root)
    {
        size_t count = 0;
        for (const auto& p : std::filesystem::recursive_directory_iterator(root))
        {
            (void)p;
            ++count;
        }
        return count;
    }
#if defined(_WIN32)
    Wide Widen(const char* text)
    {
        Wide out;
        for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        {
            CHECK(*p < 128);
            out.push_back(*p);
        }
        out.push_back(0);
        return out;
    }
    void Append(Wide& out, const wchar_t* text)
    {
        while (*text)
        {
            out.push_back(*text++);
        }
    }
    void Quote(Wide& out, const wchar_t* text)
    {
        out.push_back(34);
        Append(out, text);
        out.push_back(34);
        out.push_back(32);
    }
    struct Handle
    {
        HANDLE Value = nullptr;
        Handle() = default;
        explicit Handle(HANDLE value) : Value(value)
        {
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        ~Handle()
        {
            if (Value && !CloseHandle(Value))
            {
                bCleanupFailed = true;
            }
        }
    };
    struct Process
    {
        HANDLE Value = nullptr, Stop = nullptr;
        Process(const CookDestinationLockRequest& request, const char* mode, const char* ready = "",
                const char* stop = "", HANDLE stopHandle = nullptr)
            : Stop(stopHandle)
        {
            Wide exe(32768, 0);
            const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            CHECK(n && n < exe.size());
            Wide command;
            Quote(command, exe.data());
            Append(command, L"--test=CookDestinationLockTest --lock-child ");
            Quote(command, request.FinalRuntimeRoot.c_str());
            const auto m = Widen(mode), r = Widen(ready), s = Widen(stop);
            Quote(command, m.data());
            Quote(command, r.data());
            Quote(command, s.data());
            command.push_back(0);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION info{};
            CHECK(CreateProcessW(exe.data(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                                 &info));
            Value = info.hProcess;
            Handle thread(info.hThread);
        }
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;
        DWORD Exit()
        {
            CHECK(WaitForSingleObject(Value, 60000) == WAIT_OBJECT_0);
            DWORD code = 0;
            CHECK(GetExitCodeProcess(Value, &code));
            return code;
        }
        ~Process()
        {
            if (!Value)
            {
                return;
            }
            if (WaitForSingleObject(Value, 0) == WAIT_TIMEOUT)
            {
                if (Stop)
                {
                    SetEvent(Stop);
                }
                if (WaitForSingleObject(Value, 60000) != WAIT_OBJECT_0)
                {
                    bCleanupFailed = true;
                    std::fputs("fixture child did not exit; terminating owned test process\n", stderr);
                    TerminateProcess(Value, 99);
                    WaitForSingleObject(Value, 60000);
                }
            }
            if (!CloseHandle(Value))
            {
                bCleanupFailed = true;
            }
        }
    };
    struct ChildWork
    {
        const char* Mode;
        HANDLE Ready, Stop;
    };
    bool ChildRun(const CookDestinationLockContext&, void* data, LockText&)
    {
        auto& child = *static_cast<ChildWork*>(data);
        if (std::strcmp(child.Mode, "probe") == 0)
        {
            return true;
        }
        CHECK(SetEvent(child.Ready));
        CHECK(WaitForSingleObject(child.Stop, 60000) == WAIT_OBJECT_0);
        if (std::strcmp(child.Mode, "abandon") == 0)
        {
            ExitProcess(73);
        }
        return true;
    }
    int ChildMain(int argc, char** argv)
    {
        CHECK(argc == 6);
        CookDestinationLockRequest request{std::filesystem::path(argv[2])};
        LockText error;
        const auto readyName = Widen(argv[4]), stopName = Widen(argv[5]);
        const bool bProbe = std::strcmp(argv[3], "probe") == 0;
        Handle ready(bProbe ? nullptr : OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName.data()));
        Handle stop(bProbe ? nullptr : OpenEventW(SYNCHRONIZE, FALSE, stopName.data()));
        CHECK(bProbe || (ready.Value && stop.Value));
        ChildWork work{argv[3], ready.Value, stop.Value};
        const auto result = WithCookDestinationLock(request, ChildRun, &work, error);
        if (result == Result::Error)
        {
            std::fprintf(stderr, "child lock error: %s\n", error.c_str());
        }
        return result == Result::Executed ? 0 : result == Result::Busy ? 20 : 21;
    }
    bool ProcessProbe(const CookDestinationLockContext& context, void*, LockText&)
    {
        Process child({context.FinalRuntimeRootLocator}, "probe");
        CHECK(child.Exit() == 20);
        return true;
    }
    struct Events
    {
        LockText ReadyName, StopName;
        Handle Ready, Stop;
        explicit Events(unsigned number)
        {
            char suffix[100];
            std::snprintf(suffix, sizeof(suffix), "NorvesCookLockTest.%lu.%llu.%u", GetCurrentProcessId(),
                          static_cast<unsigned long long>(GetTickCount64()), number);
            ReadyName = "Local";
            ReadyName.push_back(92);
            ReadyName.append(suffix);
            ReadyName.append(".ready");
            StopName = "Local";
            StopName.push_back(92);
            StopName.append(suffix);
            StopName.append(".stop");
            const auto ready = Widen(ReadyName.c_str()), stop = Widen(StopName.c_str());
            Ready.Value = CreateEventW(nullptr, TRUE, FALSE, ready.data());
            CHECK(Ready.Value && GetLastError() != ERROR_ALREADY_EXISTS);
            Stop.Value = CreateEventW(nullptr, TRUE, FALSE, stop.data());
            CHECK(Stop.Value && GetLastError() != ERROR_ALREADY_EXISTS);
        }
        void WaitReady(const Process& process)
        {
            HANDLE handles[] = {Ready.Value, process.Value};
            CHECK(WaitForMultipleObjects(2, handles, FALSE, 60000) == WAIT_OBJECT_0);
        }
    };
    std::filesystem::path Short(const std::filesystem::path& path)
    {
        Wide value(32768, 0);
        const auto n = GetShortPathNameW(path.c_str(), value.data(), static_cast<DWORD>(value.size()));
        CHECK(n && n < value.size());
        return std::filesystem::path(value.data());
    }
    struct Alias
    {
        wchar_t Drive[3] = {0, 58, 0};
        std::filesystem::path Target;
        bool bMapped = false;
        explicit Alias(const std::filesystem::path& target) : Target(target)
        {
            const DWORD used = GetLogicalDrives();
            if (!used)
            {
                return;
            }
            for (wchar_t c = L'Z'; c >= L'P'; --c)
            {
                if (used & (1u << (c - L'A')))
                {
                    continue;
                }
                Drive[0] = c;
                wchar_t previous[32768]{};
                if (QueryDosDeviceW(Drive, previous, 32768) != 0 || GetLastError() != ERROR_FILE_NOT_FOUND)
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
        ~Alias()
        {
            if (bMapped &&
                !DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_NO_BROADCAST_SYSTEM, Drive,
                                  Target.c_str()))
            {
                bCleanupFailed = true;
            }
        }
        std::filesystem::path Root() const
        {
            const wchar_t path[] = {Drive[0], 58, 92, 0};
            return std::filesystem::path(path);
        }
    };
#endif
} // namespace DestinationLockTest
int main(int argc, char** argv)
{
#if !defined(_WIN32)
    (void)argc;
    (void)argv;
    return 125;
#else
    try
    {
        using namespace DestinationLockTest;
        if (argc > 1 && std::strcmp(argv[1], "--lock-child") == 0)
        {
            return ChildMain(argc, argv);
        }
        char leaf[100];
        std::snprintf(leaf, sizeof(leaf), "norves-destination-lock-%lu-%llu", GetCurrentProcessId(),
                      static_cast<unsigned long long>(GetTickCount64()));
        const auto root = std::filesystem::temp_directory_path() / leaf;
        CHECK(std::filesystem::create_directory(root));
        Write(root / "sentinel.txt", "unchanged");
        const auto stamp = std::filesystem::last_write_time(root / "sentinel.txt");
        const auto initialCount = Count(root);
        const CookDestinationLockRequest request{root / "Target Root"};
        LockText error;
        const auto name = Name(request);
        CHECK(std::strstr(name.c_str(), "NorvesLib.AssetCook.DestinationVolumeLock.v1."));
        GoodPeer(request);
        CHECK(Count(root) == initialCount && !std::filesystem::exists(request.FinalRuntimeRoot));
        Work nested;
        nested.Action = Mode::Nested;
        nested.Other = request;
        CHECK(WithCookDestinationLock(request, Run, &nested, error) == Result::Executed);
        CHECK(nested.Calls == 1);
        for (const auto& other : {request.FinalRuntimeRoot, root / "target root", root / "Sibling Root", root})
        {
            Work peer;
            peer.Action = Mode::Peer;
            peer.Other = {other};
            CHECK(Name(peer.Other) == name);
            CHECK(WithCookDestinationLock(request, Run, &peer, error) == Result::Executed);
        }
        CHECK(WithCookDestinationLock(request, ProcessProbe, nullptr, error) == Result::Executed);
        Work create;
        create.Action = Mode::CreateRoot;
        create.Other = request;
        CHECK(WithCookDestinationLock(request, Run, &create, error) == Result::Executed);
        CHECK(Name(request) == name);
        Work childRoot;
        childRoot.Action = Mode::Peer;
        childRoot.Other = {request.FinalRuntimeRoot / "nested"};
        CHECK(WithCookDestinationLock(request, Run, &childRoot, error) == Result::Executed);
        for (const auto action : {Mode::Reject, Mode::Standard, Mode::Other})
        {
            Work work;
            work.Action = action;
            CHECK(WithCookDestinationLock(request, Run, &work, error) == Result::Error);
            CHECK(work.Calls == 1 && !error.empty());
            GoodPeer(request);
        }
        for (const auto fault :
             {Probe::CookLockFault::Create, Probe::CookLockFault::Wait, Probe::CookLockFault::Reobserve,
              Probe::CookLockFault::VolumeChanged, Probe::CookLockFault::Release, Probe::CookLockFault::Close})
        {
            Work work;
            CHECK(Probe::WithCookDestinationLockForTest(request, Run, &work, error, fault) == Result::Error);
            CHECK(!error.empty());
            const bool bAfter = fault == Probe::CookLockFault::Release || fault == Probe::CookLockFault::Close;
            CHECK(work.Calls == (bAfter ? 1u : 0u));
            GoodPeer(request);
        }
        CHECK(WithCookDestinationLock(request, nullptr, nullptr, error) == Result::Error && !error.empty());
        Bad({root / "sentinel.txt"});
        Bad({root / "absent-parent/child"});
        Bad({std::filesystem::path("relative")});
        Bad({root.root_path()});
        const auto junction = root / "junction";
        LockText command = "cmd /c mklink /J ";
        command.push_back(34);
        command.append(junction.string().c_str());
        command.push_back(34);
        command.push_back(32);
        command.push_back(34);
        command.append(request.FinalRuntimeRoot.string().c_str());
        command.push_back(34);
        command.append(" >nul");
        CHECK(std::system(command.c_str()) == 0);
        Bad({junction});
        CHECK(std::filesystem::remove(junction));
        const auto shortRoot = Short(request.FinalRuntimeRoot);
        const bool bShort = shortRoot != request.FinalRuntimeRoot;
        CHECK(Name({shortRoot}) == name);
        Work shortPeer;
        shortPeer.Action = Mode::Peer;
        shortPeer.Other = {shortRoot};
        CHECK(WithCookDestinationLock(request, Run, &shortPeer, error) == Result::Executed);
        std::printf("lock_short_alias_distinct=%d\n", bShort);
        bool bSubst = false;
        {
            Alias alias(root);
            if (alias.bMapped)
            {
                const CookDestinationLockRequest mapped{alias.Root() / request.FinalRuntimeRoot.filename()};
                LockText observed;
                if (Probe::InspectCookDestinationMutexNameForTest(mapped, observed, error))
                {
                    CHECK(observed == name);
                    Work peer;
                    peer.Action = Mode::Peer;
                    peer.Other = mapped;
                    CHECK(WithCookDestinationLock(request, Run, &peer, error) == Result::Executed);
                    bSubst = true;
                }
                else
                {
                    CHECK(std::strstr(error.c_str(), "final_name_failed"));
                }
            }
        }
        CHECK(!bCleanupFailed);
        std::printf("lock_subst_checked=%d\n", bSubst);
        const auto unicodeParent = root / std::filesystem::path(u8"資料\U0001f43a");
        CHECK(std::filesystem::create_directory(unicodeParent));
        const auto unicodeAlias = Short(unicodeParent);
        bool bAscii = true;
        for (auto c : unicodeAlias.native())
        {
            if (c < 32 || c >= 127)
            {
                bAscii = false;
            }
        }
        bool bUnicode = false;
        if (bAscii)
        {
            Work peer;
            peer.Action = Mode::Peer;
            peer.Other = {unicodeAlias / "Child"};
            CHECK(Name(peer.Other) == name);
            CHECK(WithCookDestinationLock(request, Run, &peer, error) == Result::Executed);
            bUnicode = true;
        }
        std::printf("lock_unicode_ascii_alias=%d\n", bUnicode);
        bool bOtherVolume = false;
        auto otherRoot = std::filesystem::current_path() / leaf;
        otherRoot += "-other-volume";
        CHECK(std::filesystem::create_directory(otherRoot));
        if (Name({otherRoot}) != name)
        {
            struct Other
            {
                CookDestinationLockRequest Request;
            } other{{otherRoot}};
            const auto parallel = [](const CookDestinationLockContext&, void* data, LockText&)
            {
                const auto& request = static_cast<Other*>(data)->Request;
                Result result = Result::Error;
                Work work;
                LockText error;
                std::thread thread(
                    [&]
                    {
                        result = WithCookDestinationLock(request, Run, &work, error);
                    });
                thread.join();
                CHECK(result == Result::Executed && work.Calls == 1);
                return true;
            };
            CHECK(WithCookDestinationLock(request, parallel, &other, error) == Result::Executed);
            bOtherVolume = true;
        }
        CHECK(std::filesystem::remove(otherRoot));
        std::printf("lock_other_volume_checked=%d\n", bOtherVolume);
        // 同名eventを既存mutexと誤認しない。
        const auto wideName = Widen(name.c_str());
        {
            Handle event(CreateEventW(nullptr, TRUE, FALSE, wideName.data()));
            CHECK(event.Value);
            Bad(request);
        }
        GoodPeer(request);
        // 子processが実際に所有し、親のtryはBusy。正常終了で再取得可能。
        {
            Events events(1);
            Process child(request, "hold", events.ReadyName.c_str(), events.StopName.c_str(), events.Stop.Value);
            events.WaitReady(child);
            Work work;
            CHECK(WithCookDestinationLock(request, Run, &work, error) == Result::Busy && work.Calls == 0);
            CHECK(SetEvent(events.Stop.Value));
            CHECK(child.Exit() == 0);
        }
        GoodPeer(request);
        // keepalive handleを残して実process終了を観測するとAbandonedになる。
        {
            Handle keep(CreateMutexExW(nullptr, wideName.data(), 0, SYNCHRONIZE | MUTEX_MODIFY_STATE));
            CHECK(keep.Value);
            Events events(2);
            Process child(request, "abandon", events.ReadyName.c_str(), events.StopName.c_str(), events.Stop.Value);
            events.WaitReady(child);
            CHECK(SetEvent(events.Stop.Value));
            CHECK(child.Exit() == 73);
            Work work;
            CHECK(WithCookDestinationLock(request, Run, &work, error) == Result::Executed && work.bAbandoned);
        }
        // 全handleが消えた後の新objectはordinary。journal検査の省略条件にはできない。
        Work ordinary;
        CHECK(WithCookDestinationLock(request, Run, &ordinary, error) == Result::Executed && !ordinary.bAbandoned);
        CHECK(!bCleanupFailed && std::filesystem::last_write_time(root / "sentinel.txt") == stamp);
        CHECK(!std::filesystem::exists(root / "state.json"));
        std::filesystem::remove_all(root);
        std::puts("lock_cross_process_checked=1");
        std::puts("lock_cross_session_checked=0");
        std::puts(
            "COOK_DESTINATION_LOCK result=pass volume_mutex_threads_processes_busy_abandoned_callback_cleanup_no_publication");
        return 0;
    }
    catch (const DestinationLockTest::Failure&)
    {
        return 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "fixture exception: %s\n", e.what());
        return 1;
    }
#endif
}
