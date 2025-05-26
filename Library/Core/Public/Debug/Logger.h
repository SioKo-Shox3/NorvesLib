#pragma once

#include "Core/Public/Container/Containers.h"
#include "Thread/Public/RingBuffer.h"
#include <mutex>
#include <fstream>
#include <chrono>
#include <iostream>

namespace NorvesLib::Core::Debug
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief ログレベル列挙型
     */
    enum class LogLevel : uint8_t
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Critical = 4
    };

    /**
     * @brief ログエントリ構造体
     */
    struct LogEntry
    {
        LogLevel level;
        String message;
        String file;
        int line;
        std::chrono::system_clock::time_point timestamp;

        LogEntry() = default;
        LogEntry(LogLevel lvl, const String& msg, const String& f, int l)
            : level(lvl), message(msg), file(f), line(l)
            , timestamp(std::chrono::system_clock::now())
        {
        }
    };

    /**
     * @brief 高性能ログ出力システム
     * 
     * スレッドセーフかつ高性能なログ出力機能を提供します。
     * リングバッファを使用した非同期ログ出力により、
     * アプリケーションのパフォーマンスへの影響を最小限に抑えます。
     */
    class Logger
    {
    public:
        /**
         * @brief シングルトンインスタンスを取得
         */
        static Logger& GetInstance();

        /**
         * @brief ログレベルを設定
         * @param level 最小ログレベル
         */
        void SetLogLevel(LogLevel level);

        /**
         * @brief ファイル出力を有効化
         * @param filePath 出力ファイルパス
         */
        void EnableFileOutput(const String& filePath);

        /**
         * @brief コンソール出力を有効化
         */
        void EnableConsoleOutput(bool enable = true);

        /**
         * @brief デバッグ出力を有効化
         * @param enable デバッグ出力を有効にするかどうか
         */
        void EnableDebugOutput(bool enable = true);

        /**
         * @brief ログメッセージを出力
         * @param level ログレベル
         * @param message メッセージ
         * @param file ファイル名
         * @param line 行番号
         */
        void Log(LogLevel level, const String& message, const String& file, int line);

        /**
         * @brief ログバッファをフラッシュ
         */
        void Flush();

        /**
         * @brief 非同期出力を開始
         */
        void StartAsyncOutput();

        /**
         * @brief 非同期出力を停止
         */
        void StopAsyncOutput();

    private:
        Logger() = default;
        ~Logger();

        // コピー・ムーブ禁止
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        /**
         * @brief ログエントリを実際に出力
         * @param entry ログエントリ
         */
        void WriteLogEntry(const LogEntry& entry);

        /**
         * @brief ログレベルを文字列に変換
         * @param level ログレベル
         * @return ログレベル文字列
         */
        String LogLevelToString(LogLevel level) const;

        /**
         * @brief 非同期出力ワーカースレッド
         */
        void AsyncOutputWorker();

    private:
        // 設定
        LogLevel m_minLogLevel = LogLevel::Info;
        bool m_bConsoleOutput = true;
        bool m_bFileOutput = false;
        bool m_bDebugOutput = true;

        // ファイル出力
        String m_logFilePath;
        TUniquePtr<std::ofstream> m_logFile;

        // 非同期出力
        bool m_bAsyncMode = false;
        bool m_bStopWorker = false;
        TUniquePtr<std::thread> m_workerThread;

        // ログバッファ（リングバッファ使用）
        static constexpr size_t LOG_BUFFER_SIZE = 4096;
        NorvesLib::Thread::RingBuffer<LogEntry, LOG_BUFFER_SIZE> m_logBuffer;

        // 同期オブジェクト
        mutable std::mutex m_mutex;
    };

    // 便利なマクロ定義
    #define NORVES_LOG_DEBUG(msg) \
        NorvesLib::Core::Debug::Logger::GetInstance().Log( \
            NorvesLib::Core::Debug::LogLevel::Debug, msg, __FILE__, __LINE__)

    #define NORVES_LOG_INFO(msg) \
        NorvesLib::Core::Debug::Logger::GetInstance().Log( \
            NorvesLib::Core::Debug::LogLevel::Info, msg, __FILE__, __LINE__)

    #define NORVES_LOG_WARNING(msg) \
        NorvesLib::Core::Debug::Logger::GetInstance().Log( \
            NorvesLib::Core::Debug::LogLevel::Warning, msg, __FILE__, __LINE__)

    #define NORVES_LOG_ERROR(msg) \
        NorvesLib::Core::Debug::Logger::GetInstance().Log( \
            NorvesLib::Core::Debug::LogLevel::Error, msg, __FILE__, __LINE__)

    #define NORVES_LOG_CRITICAL(msg) \
        NorvesLib::Core::Debug::Logger::GetInstance().Log( \
            NorvesLib::Core::Debug::LogLevel::Critical, msg, __FILE__, __LINE__)

} // namespace NorvesLib::Core::Debug