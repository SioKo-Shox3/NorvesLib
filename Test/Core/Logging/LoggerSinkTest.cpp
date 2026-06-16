#include "Logging/Logger.h"
#include "Logging/LogTypes.h"
#include "Thread/JobSystem.h"
#include <cassert>
#include <iostream>
#include <vector>

using NorvesLib::Core::Logging::ILogSink;
using NorvesLib::Core::Logging::LogConfig;
using NorvesLib::Core::Logging::LogEntry;
using NorvesLib::Core::Logging::Logger;
using NorvesLib::Core::Logging::LogLevel;
using NorvesLib::Core::Logging::LogOutput;

#if NORVES_ENABLE_LOGGING

// テスト用 sink。OnLog で受け取った LogEntry を蓄積する。
// テスト実行ファイルなので std::vector を使用してよい。
struct CapturingSink : ILogSink
{
    std::vector<LogEntry> captured;

    void OnLog(const LogEntry &entry) override
    {
        captured.push_back(entry);
    }
};

// 目的の設定で Logger を確定状態にする（Init 済み early-return を回避）。
static void ResetLogger(const LogConfig &config)
{
    Logger &logger = Logger::GetInstance();
    logger.Shutdown();
    assert(logger.Initialize(config));
}

static LogConfig MakeSyncConfig(LogLevel minLevel)
{
    LogConfig config;
    config.minLevel = minLevel;
    config.outputType = LogOutput::None;
    config.bAsyncLogging = false;
    config.bAutoFlush = false;
    return config;
}

// (a) 各レベルの同期配送。level/message/category が一致することを確認。
static void TestSyncDeliversAllLevels()
{
    std::cout << "  TestSyncDeliversAllLevels...\n";
    ResetLogger(MakeSyncConfig(LogLevel::Trace));

    CapturingSink sink;
    Logger &logger = Logger::GetInstance();
    logger.AddSink(&sink);

    const LogLevel levels[] = {
        LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
        LogLevel::Warning, LogLevel::Error, LogLevel::Fatal};

    for (LogLevel level : levels)
    {
        logger.Log(level, "Cat", "msg");
    }

    assert(sink.captured.size() == 6);
    for (size_t i = 0; i < sink.captured.size(); ++i)
    {
        assert(sink.captured[i].level == levels[i]);
        assert(sink.captured[i].message == "msg");
        assert(sink.captured[i].category == "Cat");
    }

    logger.RemoveSink(&sink);
}

// (b) RemoveSink 後は配送されない。
static void TestRemoveSinkStopsDelivery()
{
    std::cout << "  TestRemoveSinkStopsDelivery...\n";
    ResetLogger(MakeSyncConfig(LogLevel::Trace));

    CapturingSink sink;
    Logger &logger = Logger::GetInstance();
    logger.AddSink(&sink);

    logger.Log(LogLevel::Info, "Cat", "before");
    assert(sink.captured.size() == 1);

    logger.RemoveSink(&sink);
    logger.Log(LogLevel::Info, "Cat", "after");
    assert(sink.captured.size() == 1);
}

// (c) 2 つの sink へ同時配送される。
static void TestMultipleSinks()
{
    std::cout << "  TestMultipleSinks...\n";
    ResetLogger(MakeSyncConfig(LogLevel::Trace));

    CapturingSink sinkA;
    CapturingSink sinkB;
    Logger &logger = Logger::GetInstance();
    logger.AddSink(&sinkA);
    logger.AddSink(&sinkB);

    logger.Log(LogLevel::Warning, "Cat", "dual");

    assert(sinkA.captured.size() == 1);
    assert(sinkB.captured.size() == 1);
    assert(sinkA.captured[0].message == "dual");
    assert(sinkB.captured[0].message == "dual");

    logger.RemoveSink(&sinkA);
    logger.RemoveSink(&sinkB);
}

// (d) UpdateConfig で minLevel=Warning にすると Info は不達、Error は配送。
static void TestLevelFilterAffectsDelivery()
{
    std::cout << "  TestLevelFilterAffectsDelivery...\n";
    ResetLogger(MakeSyncConfig(LogLevel::Trace));

    CapturingSink sink;
    Logger &logger = Logger::GetInstance();
    logger.AddSink(&sink);

    LogConfig warnConfig = MakeSyncConfig(LogLevel::Warning);
    logger.UpdateConfig(warnConfig);

    logger.Log(LogLevel::Info, "Cat", "filtered");
    assert(sink.captured.empty());

    logger.Log(LogLevel::Error, "Cat", "passed");
    assert(sink.captured.size() == 1);
    assert(sink.captured[0].level == LogLevel::Error);
    assert(sink.captured[0].message == "passed");

    logger.RemoveSink(&sink);
}

// async 経路（本番経路）。worker でキューイングされたエントリが
// Shutdown のドレイン合流後にすべて配送されることを確認する。
static void TestAsyncDeliversAllEntries()
{
    std::cout << "  TestAsyncDeliversAllEntries...\n";

    // async worker は JobSystem のワーカースレッド上で実行されるため、
    // ワーカーが存在しないと Logger::Shutdown の Task::Wait() が
    // 永久ブロックする。明示的に JobSystem を初期化する。
    NorvesLib::Thread::JobSystem::Get().Initialize(2);

    CapturingSink sink;
    {
        LogConfig config;
        config.minLevel = LogLevel::Trace;
        config.outputType = LogOutput::None;
        config.bAsyncLogging = true;
        config.bAutoFlush = false;
        ResetLogger(config);

        Logger &logger = Logger::GetInstance();
        logger.AddSink(&sink);

        const int N = 200;
        for (int i = 0; i < N; ++i)
        {
            logger.Log(LogLevel::Info, "Async", "entry");
        }

        // Flush() では worker のドレインを待てない。Shutdown を同期点に使う。
        // Shutdown は worker を Wait 合流し、残りを同スレッドで配送する。
        // 注意: Shutdown 自身が冒頭で "Logger" カテゴリの内部ログを 1 件出すため、
        // captured には N + 1 件が入り得る。本テストでは投入した "Async"
        // カテゴリのエントリだけを数え、全件が配送されたことを検証する。
        logger.Shutdown();

        size_t asyncCount = 0;
        for (const LogEntry &entry : sink.captured)
        {
            if (entry.category == "Async")
            {
                assert(entry.level == LogLevel::Info);
                assert(entry.message == "entry");
                ++asyncCount;
            }
        }
        assert(asyncCount == static_cast<size_t>(N));

        // Shutdown 完了後に RemoveSink（sink は本ブロック終端まで生存）。
        logger.RemoveSink(&sink);
    }

    NorvesLib::Thread::JobSystem::Get().Shutdown();
}

#endif // NORVES_ENABLE_LOGGING

int main()
{
    std::cout << "LoggerSinkTest start\n";

#if !NORVES_ENABLE_LOGGING
    // ロギング無効ビルドでは sink 機構ごと消える。
    // AddSink/RemoveSink が no-op として呼べることだけ確認する。
    std::cout << "  NORVES_ENABLE_LOGGING is off; skipping delivery tests\n";
    Logger &logger = Logger::GetInstance();
    logger.AddSink(nullptr);
    logger.RemoveSink(nullptr);
    std::cout << "LoggerSinkTest passed (skipped)\n";
    return 0;
#else
    TestSyncDeliversAllLevels();
    TestRemoveSinkStopsDelivery();
    TestMultipleSinks();
    TestLevelFilterAffectsDelivery();
    TestAsyncDeliversAllEntries();

    Logger::GetInstance().Shutdown();

    std::cout << "LoggerSinkTest passed\n";
    return 0;
#endif
}
