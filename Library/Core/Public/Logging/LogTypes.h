#pragma once

#include "Debug/DebugConfig.h"
#include "Container/Containers.h"
#include "Thread/Mutex.h"
#include "FileStream/FileStreamModule.h"
#include <chrono>
#include <iostream>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Logging
{

    /**
     * @brief ログレベル定義
     */
    enum class LogLevel : uint8_t
    {
        Trace = 0,   // 詳細なトレース情報
        Debug = 1,   // デバッグ情報
        Info = 2,    // 一般的な情報
        Warning = 3, // 警告
        Error = 4,   // エラー
        Fatal = 5    // 致命的エラー
    };

    /**
     * @brief ログ出力先の種類
     */
    enum class LogOutput : uint8_t
    {
        None = 0,
        Console = 1,
        File = 2,
        Both = 3
    };

    /**
     * @brief ログエントリ構造体
     */
    struct LogEntry
    {
        LogLevel level;
        String message;
        String category;
        String filename;
        String function;
        int32_t lineNumber;
        std::chrono::system_clock::time_point timestamp;
        uint32_t threadId;
    };

    /**
     * @brief ログ設定構造体
     */
    struct LogConfig
    {
        LogLevel minLevel = LogLevel::Trace;
        LogLevel consoleMinLevel = LogLevel::Warning;
        LogOutput outputType = LogOutput::Both;
        String logFilePath = "NorvesLib.log";
        bool bIncludeTimestamp = true;
        bool bIncludeThreadId = true;
        bool bIncludeSourceInfo = true;
        bool bAsyncLogging = true;
        size_t maxLogFileSize = 10 * 1024 * 1024; // 10MB
        uint32_t maxLogFiles = 5;
        bool bAutoFlush = true;
    };

    /**
     * @brief ログフォーマッター抽象クラス
     */
    class ILogFormatter
    {
    public:
        virtual ~ILogFormatter() = default;
        virtual String Format(const LogEntry &entry) const = 0;
    };

    /**
     * @brief ログシンク（受け側）抽象クラス
     *
     * @c Logger に @c AddSink で登録すると、各 @c LogEntry が
     * @c ProcessLogEntry の配送経路から @c OnLog へ届きます。
     *
     * @par スレッドモデル
     * @c OnLog は async ワーカースレッド / 同期 @c Log の呼び出し元スレッド /
     * @c Logger::Shutdown のドレイン処理など、任意のスレッドから呼ばれ得ます。
     * ただし配送は @c Logger 内部の @c m_sinkMutex 保持下で行われるため、
     * 同一 sink の @c OnLog が同時並行で起きることはありません（直列化）。
     *
     * @par 再入禁止
     * @c OnLog の内部から @c Logger を再入してはいけません
     * （@c Log / @c LogFormat / @c LOG_* マクロ / @c AddSink / @c RemoveSink）。
     * @c m_sinkMutex は非再帰のため、即座にデッドロックします。
     *
     * @par 高速返却の要求
     * 配送ロックを保持したまま @c OnLog を呼ぶため、処理が遅いとすべての
     * @c Log() および @c AddSink / @c RemoveSink がブロックされます。
     * 重い処理は sink 側のキューへ push し、別スレッドで処理してください。
     * @c OnLog は例外を投げないこと（throw すると Logger を通じて呼び出し元へ伝播する。重い処理同様、例外は sink 内部で処理する）。
     *
     * @par 所有権
     * @c Logger は sink を所有せず、@c delete しません。呼び出し側は
     * sink を破棄する前、かつ @c Logger::Shutdown の前に必ず @c RemoveSink を
     * 呼んでください（@c Shutdown 自身がドレイン配送を行うため、
     * @c RemoveSink 完了までは sink が生存している必要があります）。
     */
    class ILogSink
    {
    public:
        virtual ~ILogSink() = default;
        virtual void OnLog(const LogEntry &entry) = 0;

    protected:
        ILogSink() = default;
    };

    /**
     * @brief 標準ログフォーマッター
     */
    class StandardLogFormatter : public ILogFormatter
    {
    public:
        String Format(const LogEntry &entry) const override;
    };

    /**
     * @brief JSON形式ログフォーマッター
     */
    class JsonLogFormatter : public ILogFormatter
    {
    public:
        String Format(const LogEntry &entry) const override;
    }; // フォーマッター用のスマートポインタ型
    using LogFormatterPtr = TSharedPtr<ILogFormatter>;

    /**
     * @brief ログレベルを文字列に変換
     */
    constexpr const char *LogLevelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief ログレベルのカラーコードを取得
     */
    constexpr const char *GetLogLevelColor(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "\033[37m"; // White
        case LogLevel::Debug:
            return "\033[36m"; // Cyan
        case LogLevel::Info:
            return "\033[32m"; // Green
        case LogLevel::Warning:
            return "\033[33m"; // Yellow
        case LogLevel::Error:
            return "\033[31m"; // Red
        case LogLevel::Fatal:
            return "\033[35m"; // Magenta
        default:
            return "\033[0m"; // Reset
        }
    }

    constexpr const char *RESET_COLOR = "\033[0m";

} // namespace NorvesLib::Core::Logging
