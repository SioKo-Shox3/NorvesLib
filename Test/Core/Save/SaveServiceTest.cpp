#include "Save/SaveService.h"
#include "Game/Save/ProgressSaveData.h"
#include "Text/JsonDocument.h"
#include "Thread/JobSystem.h"
#include <cstdio>
#include <cwchar>
#include <thread>
#include <Windows.h>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Save;
namespace GameSave = Game::Save;

namespace
{
#define SERVICE_CHECK(condition) \
    do { if (!(condition)) { std::fprintf(stderr, "確認失敗: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return false; } } while (false)

    // 実行文字セットに依存せず、保存 API に UTF-8 の値を渡す。
    Container::String Utf8(const char8_t* text)
    {
        return Container::String(reinterpret_cast<const char*>(text));
    }

    struct TestDirectory
    {
        Container::WideString Wide;
        Container::String Utf8;

        TestDirectory()
        {
            wchar_t temporary[32768]{};
            const DWORD length = GetTempPathW(static_cast<DWORD>(sizeof(temporary) / sizeof(temporary[0])), temporary);
            if (length == 0 || length >= sizeof(temporary) / sizeof(temporary[0]))
            {
                return;
            }
            for (unsigned attempt = 0; attempt < 64; ++attempt)
            {
                wchar_t name[128]{};
                std::swprintf(name, sizeof(name) / sizeof(name[0]), L"NorvesLib-SaveServiceTest-%lu-%llu-%u",
                    static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long long>(GetTickCount64()), attempt);
                Container::WideString candidate(temporary);
                candidate.append(name);
                if (CreateDirectoryW(candidate.c_str(), nullptr))
                {
                    Wide = candidate;
                    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (count > 0)
                    {
                        Container::VariableArray<char> buffer(count);
                        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide.c_str(), -1, buffer.data(), count, nullptr, nullptr) == count)
                        {
                            Utf8 = buffer.data();
                        }
                    }
                    return;
                }
                if (GetLastError() != ERROR_ALREADY_EXISTS)
                {
                    return;
                }
            }
        }

        ~TestDirectory()
        {
            if (Wide.empty())
            {
                return;
            }
            auto pattern = Wide;
            pattern.append(L"\\*");
            WIN32_FIND_DATAW entry{};
            const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
            if (search != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    {
                        continue;
                    }
                    auto path = Wide;
                    path.append(L"\\");
                    path.append(entry.cFileName);
                    DeleteFileW(path.c_str());
                } while (FindNextFileW(search, &entry));
                FindClose(search);
            }
            RemoveDirectoryW(Wide.c_str());
        }
    };

    // 実ファイルへの書き込みを止め、重なった Write の最大数をワーカー間で数える。
    class BlockingFileOperations : public Platform::IFileOperations
    {
    public:
        explicit BlockingFileOperations(bool bBlock)
            : m_Entered(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
              m_Released(CreateEventW(nullptr, TRUE, bBlock ? FALSE : TRUE, nullptr))
        {
        }

        ~BlockingFileOperations() override
        {
            CloseHandle(m_Entered);
            CloseHandle(m_Released);
        }

        bool IsReady() const { return m_Entered && m_Released; }
        bool WaitForWrite() const { return WaitForSingleObject(m_Entered, 10000) == WAIT_OBJECT_0; }
        void Release() { SetEvent(m_Released); }
        LONG WriteCount() { return InterlockedCompareExchange(&m_Writes, 0, 0); }
        LONG MaxConcurrent() { return InterlockedCompareExchange(&m_MaxConcurrent, 0, 0); }
        DWORD WriterThreadId() { return static_cast<DWORD>(InterlockedCompareExchange(&m_WriterThread, 0, 0)); }

        uint32_t CreateNew(const Container::WideString& path, Handle& outHandle) override
        {
            return m_Default.CreateNew(path, outHandle);
        }
        uint32_t Write(Handle handle, Container::Span<const uint8_t> bytes) override
        {
            const LONG active = InterlockedIncrement(&m_Active);
            LONG maximum = InterlockedCompareExchange(&m_MaxConcurrent, 0, 0);
            while (active > maximum)
            {
                const LONG previous = InterlockedCompareExchange(&m_MaxConcurrent, active, maximum);
                if (previous == maximum)
                {
                    break;
                }
                maximum = previous;
            }
            InterlockedIncrement(&m_Writes);
            InterlockedExchange(&m_WriterThread, static_cast<LONG>(GetCurrentThreadId()));
            SetEvent(m_Entered);
            // 同期経路への退行でも永久停止せず、失敗を返せるよう待ち時間に上限を置く。
            const uint32_t result = WaitForSingleObject(m_Released, 10000) == WAIT_OBJECT_0
                ? m_Default.Write(handle, bytes) : ERROR_TIMEOUT;
            InterlockedDecrement(&m_Active);
            return result;
        }
        uint32_t Flush(Handle handle) override { return m_Default.Flush(handle); }
        uint32_t Close(Handle handle) override { return m_Default.Close(handle); }
        uint32_t Replace(const Container::WideString& path, const Container::WideString& temporary,
            const Container::WideString& backup) override { return m_Default.Replace(path, temporary, backup); }
        uint32_t MoveReplace(const Container::WideString& from, const Container::WideString& to) override
        {
            return m_Default.MoveReplace(from, to);
        }
        uint32_t Delete(const Container::WideString& path) override { return m_Default.Delete(path); }
        uint32_t ReadAll(const Container::WideString& path, Container::VariableArray<uint8_t>& outBytes) override
        {
            return m_Default.ReadAll(path, outBytes);
        }
        void Sleep(uint32_t milliseconds) override { m_Default.Sleep(milliseconds); }

    private:
        Platform::IFileOperations& m_Default = Platform::GetDefaultFileOperations();
        HANDLE m_Entered;
        HANDLE m_Released;
        volatile LONG m_Active = 0;
        volatile LONG m_MaxConcurrent = 0;
        volatile LONG m_Writes = 0;
        volatile LONG m_WriterThread = 0;
    };

    struct ServiceFixture
    {
        TestDirectory Directory;
        BlockingFileOperations Operations;
        unsigned Captures = 0;
        unsigned Completions = 0;
        bool bAllSuccessful = true;
        SaveService Service;

        explicit ServiceFixture(bool bBlock = false) : Operations(bBlock)
        {
            Service.Configure(Directory.Utf8, Operations);
            Service.SetSnapshotProvider([this](Container::String& out)
            {
                char payload[80]{};
                std::snprintf(payload, sizeof(payload), "{\"formatVersion\":2,\"snapshot\":%u}", ++Captures);
                out = payload;
                return true;
            });
            Service.OnWriteCompleted().Add([this](const SaveWriteReport& report)
            {
                ++Completions;
                bAllSuccessful = bAllSuccessful && report.Result.bSuccess;
            });
        }

        ~ServiceFixture()
        {
            // 途中の確認で失敗しても、サービスが待つ前に書き込みの停止を解除する。
            Operations.Release();
            Service.FlushAndWait();
        }

        bool IsReady() const { return !Directory.Utf8.empty() && Operations.IsReady(); }

        bool CheckCompleted(unsigned expected)
        {
            SERVICE_CHECK(Captures == expected && Completions == expected && bAllSuccessful);
            SERVICE_CHECK(Service.GetCompletedWriteCount() == expected);
            SERVICE_CHECK(Operations.WriteCount() == static_cast<LONG>(expected));
            SERVICE_CHECK(!Service.IsWriteInFlight() && !Service.HasPendingRequests());
            return true;
        }
    };

    bool TestCoalescedAutosaves()
    {
        ServiceFixture fixture;
        SERVICE_CHECK(fixture.IsReady());
        fixture.Service.RequestAutosave("defeat");
        fixture.Service.RequestAutosave("waypoint");
        fixture.Service.RequestAutosave("rest");
        SERVICE_CHECK(fixture.Captures == 0 && fixture.Operations.WriteCount() == 0);
        fixture.Service.Update(false);
        fixture.Service.FlushAndWait();
        return fixture.CheckCompleted(1);
    }

    bool TestSaveInhibit()
    {
        ServiceFixture fixture;
        SERVICE_CHECK(fixture.IsReady());
        fixture.Service.PushSaveInhibit();
        fixture.Service.RequestAutosave("rest");
        for (unsigned frame = 0; frame < 8; ++frame)
        {
            fixture.Service.Update(false);
            SERVICE_CHECK(fixture.Captures == 0 && fixture.Operations.WriteCount() == 0);
            SERVICE_CHECK(fixture.Completions == 0 && fixture.Service.GetCompletedWriteCount() == 0);
            SERVICE_CHECK(!fixture.Service.IsWriteInFlight() && fixture.Service.HasPendingRequests());
        }
        fixture.Service.PopSaveInhibit();
        fixture.Service.Update(false);
        SERVICE_CHECK(fixture.Captures == 1);
        fixture.Service.FlushAndWait();
        return fixture.CheckCompleted(1);
    }

    bool TestSerializedWrites()
    {
        ServiceFixture fixture(true);
        SERVICE_CHECK(fixture.IsReady());
        fixture.Service.RequestAutosave("first");
        fixture.Service.Update(false);
        SERVICE_CHECK(fixture.Operations.WaitForWrite());
        SERVICE_CHECK(fixture.Operations.WriterThreadId() != GetCurrentThreadId());
        fixture.Service.RequestAutosave("second");
        for (unsigned frame = 0; frame < 8; ++frame)
        {
            fixture.Service.Update(false);
            SERVICE_CHECK(fixture.Service.IsWriteInFlight() && fixture.Service.HasPendingRequests());
            SERVICE_CHECK(fixture.Captures == 1 && fixture.Operations.WriteCount() == 1);
            SERVICE_CHECK(fixture.Completions == 0 && fixture.Service.GetCompletedWriteCount() == 0);
        }
        fixture.Operations.Release();
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (fixture.Completions < 2 && GetTickCount64() < deadline)
        {
            fixture.Service.Update(false);
            ::Sleep(1);
        }
        SERVICE_CHECK(fixture.CheckCompleted(2));
        SERVICE_CHECK(fixture.Operations.MaxConcurrent() == 1);
        return true;
    }

    bool CheckSavedSnapshot(const Container::String& directory, uint32_t slot, unsigned expected)
    {
        SaveSlotStore store(directory);
        const auto loaded = store.LoadSlot(slot);
        SERVICE_CHECK(loaded.Status == SaveLoadStatus::Loaded && loaded.Source == SaveLoadSource::Primary);
        JsonDocument document;
        SERVICE_CHECK(JsonDocument::TryParseUtf8(loaded.PayloadUtf8, document));
        const auto snapshot = document.GetRoot().FindMember("snapshot");
        SERVICE_CHECK(snapshot.IsNumber() && snapshot.AsNumber() == expected);
        return true;
    }

    bool TestFlushAndWait()
    {
        ServiceFixture fixture(true);
        SERVICE_CHECK(fixture.IsReady());
        fixture.Service.Save(0);
        fixture.Service.Update(false);
        SERVICE_CHECK(fixture.Operations.WaitForWrite());
        SERVICE_CHECK(fixture.Operations.WriterThreadId() != GetCurrentThreadId());
        fixture.Service.Save(1);
        SERVICE_CHECK(fixture.Service.IsWriteInFlight() && fixture.Service.HasPendingRequests());
        SERVICE_CHECK(fixture.Captures == 1 && fixture.Completions == 0);
        std::thread release([&fixture]()
        {
            ::Sleep(30);
            fixture.Operations.Release();
        });
        fixture.Service.FlushAndWait();
        release.join();
        SERVICE_CHECK(fixture.CheckCompleted(2));
        SERVICE_CHECK(fixture.Operations.MaxConcurrent() == 1);
        SERVICE_CHECK(CheckSavedSnapshot(fixture.Directory.Utf8, 0, 1));
        SERVICE_CHECK(CheckSavedSnapshot(fixture.Directory.Utf8, 1, 2));
        return true;
    }

    template <typename T>
    bool EqualMap(const Container::Map<Container::String, T>& left, const Container::Map<Container::String, T>& right)
    {
        SERVICE_CHECK(left.size() == right.size());
        for (const auto& [name, value] : left)
        {
            const auto found = right.find(name);
            SERVICE_CHECK(found != right.end() && found->second == value);
        }
        return true;
    }

    bool CheckProgress(const GameSave::ProgressSaveData& expected, const GameSave::ProgressSaveData& actual)
    {
        SERVICE_CHECK(actual.FormatVersion == expected.FormatVersion && actual.AppVersion == expected.AppVersion);
        SERVICE_CHECK(actual.SavedAtUnix == expected.SavedAtUnix && actual.PlaySeconds == expected.PlaySeconds);
        for (size_t index = 0; index < 3; ++index)
        {
            SERVICE_CHECK(actual.Player.Pos[index] == expected.Player.Pos[index]);
        }
        SERVICE_CHECK(actual.Player.Yaw == expected.Player.Yaw);
        SERVICE_CHECK(actual.Player.Waypoint.Id == expected.Player.Waypoint.Id);
        SERVICE_CHECK(actual.Player.Waypoint.Kind == expected.Player.Waypoint.Kind);
        SERVICE_CHECK(actual.World.TimeOfDay == expected.World.TimeOfDay && actual.World.Day == expected.World.Day);
        SERVICE_CHECK(actual.World.Weather == expected.World.Weather);
        SERVICE_CHECK(actual.Progress.Defeated.size() == expected.Progress.Defeated.size());
        for (size_t index = 0; index < expected.Progress.Defeated.size(); ++index)
        {
            SERVICE_CHECK(actual.Progress.Defeated[index] == expected.Progress.Defeated[index]);
        }
        SERVICE_CHECK(actual.Progress.UnlockedWaypoints.size() == expected.Progress.UnlockedWaypoints.size());
        for (size_t index = 0; index < expected.Progress.UnlockedWaypoints.size(); ++index)
        {
            SERVICE_CHECK(actual.Progress.UnlockedWaypoints[index].Id == expected.Progress.UnlockedWaypoints[index].Id);
            SERVICE_CHECK(actual.Progress.UnlockedWaypoints[index].Kind == expected.Progress.UnlockedWaypoints[index].Kind);
        }
        SERVICE_CHECK(actual.Progress.MapMask.size() == expected.Progress.MapMask.size());
        for (size_t index = 0; index < expected.Progress.MapMask.size(); ++index)
        {
            SERVICE_CHECK(actual.Progress.MapMask[index] == expected.Progress.MapMask[index]);
        }
        SERVICE_CHECK(EqualMap(expected.Progress.Flags, actual.Progress.Flags));
        SERVICE_CHECK(EqualMap(expected.Progress.Ints, actual.Progress.Ints));
        SERVICE_CHECK(EqualMap(expected.Progress.Reals, actual.Progress.Reals));
        return true;
    }

    bool TestProgressRoundTrip()
    {
        ServiceFixture fixture;
        SERVICE_CHECK(fixture.IsReady());
        GameSave::ProgressSaveData original;
        original.AppVersion = "save-test-2";
        original.SavedAtUnix = 1712345679;
        original.PlaySeconds = 12345.75;
        original.Player.Pos[0] = 1.25;
        original.Player.Pos[1] = -3.5;
        original.Player.Pos[2] = 7.125;
        original.Player.Yaw = 135.5;
        original.Player.Waypoint = {Utf8(u8"草原の寝床"), GameSave::WaypointKind::Bed};
        original.World.TimeOfDay = 0.875;
        original.World.Day = 42;
        original.World.Weather = "snow";
        original.Progress.Defeated = {"guardian", "wolf"};
        original.Progress.UnlockedWaypoints = {
            {Utf8(u8"草原の寝床"), GameSave::WaypointKind::Bed},
            {Utf8(u8"山の拠点"), GameSave::WaypointKind::Checkpoint},
            {Utf8(u8"古い塔"), GameSave::WaypointKind::Poi}};
        original.Progress.MapMask = {0x00, 0x01, 0x0f, 0x10, 0x80, 0xff};
        original.Progress.Flags["gateOpen"] = true;
        original.Progress.Flags["introSeen"] = false;
        original.Progress.Ints["score"] = 9007199254740993LL;
        original.Progress.Ints["debt"] = -9007199254740995LL;
        original.Progress.Reals["speed"] = 1.25;
        original.Progress.Reals["offset"] = -0.5;
        fixture.Service.SetSnapshotProvider([&](Container::String& out)
        {
            ++fixture.Captures;
            out = GameSave::ToJson(original);
            return !out.empty();
        });
        GameSave::ProgressSaveData restored;
        unsigned loads = 0;
        bool bParsed = false;
        Container::String error;
        fixture.Service.SetLoadConsumer([&](Container::Span<const uint8_t> bytes)
        {
            ++loads;
            bParsed = GameSave::FromJson(bytes, restored, &error);
            return bParsed;
        });
        fixture.Service.Save(0);
        fixture.Service.Update(false);
        fixture.Service.FlushAndWait();
        SERVICE_CHECK(fixture.CheckCompleted(1));
        const auto loaded = fixture.Service.Load(0);
        SERVICE_CHECK(loaded.Status == SaveLoadStatus::Loaded && loaded.Source == SaveLoadSource::Primary);
        SERVICE_CHECK(loads == 1 && bParsed && error.empty());
        return CheckProgress(original, restored);
    }

    bool TestVersionOneFixture()
    {
        Container::String path(NORVES_SAVE_FIXTURE_DIR);
        path.append("/save_v1.json");
        Container::VariableArray<uint8_t> bytes;
        SERVICE_CHECK(Platform::ReadFileBytes(path, bytes).bSuccess);
        GameSave::ProgressSaveData data;
        Container::String error;
        SERVICE_CHECK(GameSave::FromJson(bytes, data, &error) && error.empty());
        SERVICE_CHECK(data.FormatVersion == SavePayloadFormatVersion);
        SERVICE_CHECK(data.SavedAtUnix == 1712345678 && data.PlaySeconds == 12345.5);
        SERVICE_CHECK(data.Player.Waypoint.Id == Utf8(u8"草原の拠点"));
        SERVICE_CHECK(data.Player.Waypoint.Kind == GameSave::WaypointKind::Checkpoint);
        SERVICE_CHECK(data.World.TimeOfDay == 0.75 && data.World.Day == 24 && data.World.Weather == "rain");
        SERVICE_CHECK(data.Progress.Defeated.size() == 2);
        SERVICE_CHECK(data.Progress.Defeated[0] == "guardian" && data.Progress.Defeated[1] == "wolf");
        Container::Map<Container::String, bool> flags;
        flags["gateOpen"] = true;
        flags["introSeen"] = false;
        Container::Map<Container::String, int64_t> ints;
        ints["score"] = 9007199254740991LL;
        SERVICE_CHECK(EqualMap(flags, data.Progress.Flags));
        SERVICE_CHECK(EqualMap(ints, data.Progress.Ints));
        return true;
    }

    // 束のほかの試験が用意したワーカーは借り、自分が初期化した場合だけ停止する。
    struct JobSystemScope
    {
        bool bOwned = NorvesLib::Thread::JobSystem::Get().GetWorkerThreadCount() == 0;
        JobSystemScope()
        {
            if (bOwned)
            {
                NorvesLib::Thread::JobSystem::Get().Initialize(2);
            }
        }
        ~JobSystemScope()
        {
            if (bOwned)
            {
                NorvesLib::Thread::JobSystem::Get().Shutdown();
            }
        }
    };
} // namespace

int main()
{
    JobSystemScope jobs;
    if (NorvesLib::Thread::JobSystem::Get().GetWorkerThreadCount() < 2)
    {
        std::fprintf(stderr, "非同期保存の試験にはワーカーが2本以上必要です\n");
        return 1;
    }
    const bool bPassed = TestCoalescedAutosaves() && TestSaveInhibit() && TestSerializedWrites() &&
        TestFlushAndWait() && TestProgressRoundTrip() && TestVersionOneFixture();
    if (bPassed)
    {
        std::printf("SaveServiceTest: 6項目すべて成功\n");
    }
    return bPassed ? 0 : 1;
}
