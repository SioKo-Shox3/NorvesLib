#include "Save/GameStateStore.h"
#include "Save/SaveMigration.h"
#include "Save/SaveSlotStore.h"
#include "Logging/Logger.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <Windows.h>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Save;

namespace
{
#define STATE_CHECK(condition) \
    do { if (!(condition)) { std::fprintf(stderr, "確認失敗: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return false; } } while (false)

    constexpr const char* Ledger = R"json({"formatVersion":1,"keys":[
        {"name":"progress.gate_open","type":"bool","scope":"persistent","default":false},
        {"name":"progress.count","type":"int64","scope":"persistent","default":9007199254740993},
        {"name":"progress.speed","type":"double","scope":"persistent","default":1.25},
        {"name":"session.paused","type":"bool","scope":"session"}
    ]})json";
    constexpr int64_t LargeValue = 9007199254740995LL;

    Container::String ToUtf8(const wchar_t* text)
    {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
        if (count <= 0) return {};
        Container::VariableArray<char> buffer(count);
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, buffer.data(), count, nullptr, nullptr) != count)
            return {};
        return Container::String(buffer.data());
    }

    struct TestDirectory
    {
        Container::WideString Wide;
        Container::String Utf8;

        TestDirectory()
        {
            wchar_t temporary[32768]{};
            const DWORD length = GetTempPathW(static_cast<DWORD>(sizeof(temporary) / sizeof(temporary[0])), temporary);
            if (length == 0 || length >= sizeof(temporary) / sizeof(temporary[0])) return;
            for (unsigned attempt = 0; attempt < 64; ++attempt)
            {
                wchar_t name[128]{};
                std::swprintf(name, sizeof(name) / sizeof(name[0]), L"NorvesLib-GameStateStoreTest-%lu-%llu-%u",
                    static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long long>(GetTickCount64()), attempt);
                Container::WideString candidate(temporary);
                candidate.append(name);
                if (CreateDirectoryW(candidate.c_str(), nullptr))
                {
                    Wide = candidate;
                    Utf8 = ToUtf8(Wide.c_str());
                    return;
                }
                if (GetLastError() != ERROR_ALREADY_EXISTS) return;
            }
        }

        ~TestDirectory()
        {
            if (Wide.empty()) return;
            Container::WideString pattern = Wide;
            pattern.append(L"\\*");
            WIN32_FIND_DATAW entry{};
            const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
            if (search != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
                    Container::WideString path = Wide;
                    path.append(L"\\");
                    path.append(entry.cFileName);
                    DeleteFileW(path.c_str());
                } while (FindNextFileW(search, &entry));
                FindClose(search);
            }
            RemoveDirectoryW(Wide.c_str());
        }

        Container::String File(const char* name) const
        {
            Container::String path = Utf8;
            path.append("\\");
            path.append(name);
            return path;
        }
    };

    struct WarningSink : Logging::ILogSink
    {
        size_t Count = 0;
        void OnLog(const Logging::LogEntry& entry) override
        {
            if (entry.level == Logging::LogLevel::Warning && entry.category == "GameStateStore") ++Count;
        }
    };

    bool RunStateTest(WarningSink& warnings)
    {
        GameStateStore store;
        Container::String error;
        STATE_CHECK(store.LoadKeyLedgerFromText(Ledger, &error));
        STATE_CHECK(error.empty());

        // 型別の読み書きと、精度を失いやすい整数の既定値を確認する。
        bool flag = true;
        int64_t count = 0;
        double speed = 0.0;
        STATE_CHECK(store.TryGetBool("progress.gate_open"_id, flag) && !flag);
        STATE_CHECK(store.TryGetInt64("progress.count"_id, count) && count == 9007199254740993LL);
        STATE_CHECK(store.TryGetDouble("progress.speed"_id, speed) && speed == 1.25);
        STATE_CHECK(store.SetBool("progress.gate_open"_id, true));
        STATE_CHECK(store.SetInt64("progress.count"_id, LargeValue));
        STATE_CHECK(store.SetDouble("progress.speed"_id, 3.5));
        STATE_CHECK(store.SetBool("session.paused"_id, true));
        STATE_CHECK(store.TryGetBool("progress.gate_open"_id, flag) && flag);
        STATE_CHECK(store.TryGetInt64("progress.count"_id, count) && count == LargeValue);
        STATE_CHECK(store.TryGetDouble("progress.speed"_id, speed) && speed == 3.5);

        // 不正な台帳を拒否しても、それまでの台帳と値を保つ。
        const char* invalidLedgers[] = {
            R"json({"formatVersion":1,"keys":[{"name":"x","type":"bool","scope":"session"},{"name":"x","type":"bool","scope":"session"}]})json",
            R"json({"formatVersion":1,"keys":[{"name":"x","type":"unknown","scope":"session"}]})json",
            R"json({"formatVersion":1,"keys":[{"name":"x","type":"bool","scope":"unknown"}]})json",
            R"json({"formatVersion":1,"keys":[{"name":"x","type":"bool","scope":"session","default":0}]})json",
            R"json({"formatVersion":1,"keys":[{"name":"x","type":"int64","scope":"session","default":9223372036854775808}]})json"
        };
        for (const char* invalid : invalidLedgers)
        {
            STATE_CHECK(!store.LoadKeyLedgerFromText(invalid, &error));
            STATE_CHECK(!error.empty());
            STATE_CHECK(store.TryGetInt64("progress.count"_id, count) && count == LargeValue);
        }

        // Persistent の列挙結果だけでセーブを作り、スロットから読み戻す。
        Container::VariableArray<GameStateEntry> entries;
        store.CollectPersistent(entries);
        STATE_CHECK(entries.size() == 3);
        JsonWriter writer;
        writer.BeginObject();
        writer.WriteInt64("formatVersion", SavePayloadFormatVersion);
        writer.BeginObject("flags");
        for (const auto& entry : entries)
            if (entry.Type == GameStateValueType::Bool) writer.WriteBool(entry.Name.c_str(), entry.BoolValue);
        writer.EndObject();
        writer.BeginObject("ints");
        for (const auto& entry : entries)
        {
            if (entry.Type != GameStateValueType::Int64) continue;
            // 既存のセーブ移行処理は大きな整数を文字列として保持する。
            char digits[32]{};
            const auto converted = std::to_chars(digits, digits + sizeof(digits) - 1, entry.IntValue);
            STATE_CHECK(converted.ec == std::errc{});
            *converted.ptr = '\0';
            writer.WriteString(entry.Name.c_str(), digits);
        }
        writer.EndObject();
        writer.BeginObject("reals");
        for (const auto& entry : entries)
            if (entry.Type == GameStateValueType::Double) writer.WriteNumber(entry.Name.c_str(), entry.DoubleValue);
        writer.EndObject();
        writer.EndObject();
        STATE_CHECK(writer.IsComplete());
        const Container::String payload = writer.ToString();
        TestDirectory directory;
        STATE_CHECK(!directory.Utf8.empty());
        SaveSlotStore slots(directory.Utf8);
        STATE_CHECK(slots.SaveSlot(0, {reinterpret_cast<const uint8_t*>(payload.data()), payload.size()}).bSuccess);
        const auto loaded = slots.LoadSlot(0);
        STATE_CHECK(loaded.Status == SaveLoadStatus::Loaded);
        JsonDocument document;
        STATE_CHECK(JsonDocument::TryParseUtf8(loaded.PayloadUtf8, document));
        const auto flags = document.GetRoot().FindMember("flags");
        const auto ints = document.GetRoot().FindMember("ints");
        const auto reals = document.GetRoot().FindMember("reals");
        STATE_CHECK(flags.IsObject() && flags.GetObjectSize() == 1 && flags.FindMember("progress.gate_open").AsBool());
        STATE_CHECK(ints.IsObject() && ints.GetObjectSize() == 1 && ints.FindMember("progress.count").IsString());
        STATE_CHECK(reals.IsObject() && reals.GetObjectSize() == 1 && reals.FindMember("progress.speed").AsNumber() == 3.5);
        STATE_CHECK(!flags.HasMember("session.paused") && !ints.HasMember("session.paused") && !reals.HasMember("session.paused"));
        const Container::String& savedCount = ints.FindMember("progress.count").AsString();
        const auto parsed = std::from_chars(savedCount.data(), savedCount.data() + savedCount.size(), count);
        STATE_CHECK(parsed.ec == std::errc{} && parsed.ptr == savedCount.data() + savedCount.size() && count == LargeValue);

        store.ResetScope(GameStateScope::Persistent);
        STATE_CHECK(store.TryGetBool("progress.gate_open"_id, flag) && !flag);
        STATE_CHECK(store.TryGetInt64("progress.count"_id, count) && count == 9007199254740993LL);
        STATE_CHECK(store.TryGetDouble("progress.speed"_id, speed) && speed == 1.25);
        STATE_CHECK(store.TryGetBool("session.paused"_id, flag) && flag);
        STATE_CHECK(store.ApplyPersistentBool("progress.gate_open", flags.FindMember("progress.gate_open").AsBool()));
        count = 0;
        const auto applied = std::from_chars(savedCount.data(), savedCount.data() + savedCount.size(), count);
        STATE_CHECK(applied.ec == std::errc{} && applied.ptr == savedCount.data() + savedCount.size());
        STATE_CHECK(store.ApplyPersistentInt64("progress.count", count));
        STATE_CHECK(store.ApplyPersistentDouble("progress.speed", reals.FindMember("progress.speed").AsNumber()));
        STATE_CHECK(store.TryGetBool("progress.gate_open"_id, flag) && flag);
        STATE_CHECK(store.TryGetInt64("progress.count"_id, count) && count == LargeValue);
        STATE_CHECK(store.TryGetDouble("progress.speed"_id, speed) && speed == 3.5);
        STATE_CHECK(!store.ApplyPersistentBool("session.paused", false));
        STATE_CHECK(!store.ApplyPersistentBool("progress.count", false));
        STATE_CHECK(!store.ApplyPersistentBool("missing", false));

        // ファイル入口は BOM 付き UTF-8 の台帳も受け付ける。
        Container::VariableArray<uint8_t> ledgerBytes = {0xef, 0xbb, 0xbf};
        for (const char* ch = Ledger; *ch; ++ch) ledgerBytes.push_back(static_cast<uint8_t>(*ch));
        STATE_CHECK(Platform::WriteFileAtomic(directory.File("ledger.json"), ledgerBytes, false).bSuccess);
        GameStateStore fromFile;
        STATE_CHECK(fromFile.LoadKeyLedgerFile(directory.File("ledger.json")));
        STATE_CHECK(fromFile.TryGetInt64("progress.count"_id, count) && count == 9007199254740993LL);

        STATE_CHECK(fromFile.SetBool("progress.gate_open"_id, true));
        STATE_CHECK(fromFile.LoadKeyLedgerFromText(Ledger));
        STATE_CHECK(fromFile.TryGetBool("progress.gate_open"_id, flag) && !flag);
        STATE_CHECK(fromFile.LoadKeyLedgerFromText(R"json({"formatVersion":1,"keys":[]})json"));
        fromFile.CollectPersistent(entries);
        STATE_CHECK(entries.empty());

        // 未登録と型不一致では値を変えず、それぞれ警告を出す。
        const size_t beforeUnknownGet = warnings.Count;
        flag = true;
        STATE_CHECK(!store.TryGetBool("missing"_id, flag) && flag);
#if NORVES_ENABLE_LOGGING
        STATE_CHECK(warnings.Count == beforeUnknownGet + 1);
#endif
        const size_t beforeUnknownSet = warnings.Count;
        STATE_CHECK(!store.SetBool("missing"_id, false));
#if NORVES_ENABLE_LOGGING
        STATE_CHECK(warnings.Count == beforeUnknownSet + 1);
#endif
        STATE_CHECK(!store.SetBool("progress.count"_id, false));
        STATE_CHECK(store.TryGetInt64("progress.count"_id, count) && count == LargeValue);

        // 値が変わる set は各 1 回、同じ値は 0 回通知する。
        size_t notifications = 0;
        Identity lastKey;
        store.OnValueChanged().Add([&](Identity key) { ++notifications; lastKey = key; });
        STATE_CHECK(store.SetBool("progress.gate_open"_id, false));
        STATE_CHECK(notifications == 1 && lastKey == "progress.gate_open"_id && lastKey.ToString() == "progress.gate_open");
        STATE_CHECK(store.SetBool("progress.gate_open"_id, false) && notifications == 1);
        STATE_CHECK(store.SetInt64("progress.count"_id, LargeValue - 1));
        STATE_CHECK(notifications == 2 && lastKey == "progress.count"_id);
        STATE_CHECK(store.SetInt64("progress.count"_id, LargeValue - 1) && notifications == 2);
        STATE_CHECK(store.SetDouble("progress.speed"_id, 4.5));
        STATE_CHECK(notifications == 3 && lastKey == "progress.speed"_id);
        STATE_CHECK(store.SetDouble("progress.speed"_id, 4.5) && notifications == 3);
        return true;
    }
}

int main()
{
    Logging::LogConfig config;
    config.minLevel = Logging::LogLevel::Trace;
    config.outputType = Logging::LogOutput::None;
    config.bAsyncLogging = false;
    config.bAutoFlush = false;
    auto& logger = Logging::Logger::GetInstance();
    logger.Shutdown();
    if (!logger.Initialize(config)) return 1;
    WarningSink warnings;
    logger.AddSink(&warnings);
    const bool passed = RunStateTest(warnings);
    logger.RemoveSink(&warnings);
    logger.Shutdown();
    if (passed) std::printf("GameStateStoreTest passed\n");
    return passed ? 0 : 1;
}
