#pragma once

#include "Core/Public/Container/Containers.h"
#include "Thread/Public/Mutex.h"
#include "FileStream/Public/FileStreamModule.h"
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
        LogLevel minLevel = LogLevel::Info;
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