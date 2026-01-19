#pragma once

#include "LogTypes.h"
#include "Thread/JobSystem.h"
#include "Thread/RingBuffer.h"
#include "Thread/Atomic.h"
#include <memory>
#include <functional>

namespace NorvesLib::Core::Logging
{

    /**
     * @brief 高性能ログシステムのメインクラス
     *
     * マルチスレッド対応で、非同期ログ出力をサポートします。
     * シングルトンパターンで実装され、グローバルにアクセス可能です。
     */
    class Logger
    {
    public:
        /**
         * @brief シングルトンインスタンスを取得
         */
        static Logger &GetInstance();

        /**
         * @brief ログシステムを初期化
         * @param config ログ設定
         * @return 初期化に成功した場合true
         */
        bool Initialize(const LogConfig &config = LogConfig{});

        /**
         * @brief ログシステムを終了
         */
        void Shutdown();

        /**
         * @brief ログエントリを記録
         * @param level ログレベル
         * @param category カテゴリ
         * @param message メッセージ
         * @param filename ファイル名
         * @param function 関数名
         * @param lineNumber 行番号
         */
        void Log(LogLevel level, const String &category, const String &message,
                 const char *filename = "", const char *function = "", int32_t lineNumber = 0);

        /**
         * @brief フォーマット付きログエントリを記録
         */
        template <typename... Args>
        void LogFormat(LogLevel level, const String &category,
                       const char *filename, const char *function, int32_t lineNumber,
                       const char *format, Args &&...args);

        /**
         * @brief 設定を更新
         */
        void UpdateConfig(const LogConfig &config);

        /**
         * @brief 現在の設定を取得
         */
        const LogConfig &GetConfig() const { return m_config; }

        /**
         * @brief カスタムフォーマッターを設定
         */
        void SetFormatter(LogFormatterPtr formatter);

        /**
         * @brief ログレベルがアクティブかどうか確認
         */
        bool IsLevelActive(LogLevel level) const;

        /**
         * @brief 即座にフラッシュ
         */
        void Flush();

        /**
         * @brief ログファイルのローテーション
         */
        void RotateLogFile();

    private:
        Logger() = default;
        ~Logger();

        // コピー・ムーブを禁止
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger(Logger &&) = delete;
        Logger &operator=(Logger &&) = delete;

        /**
         * @brief ログエントリを処理（内部用）
         */
        void ProcessLogEntry(const LogEntry &entry);

        /**
         * @brief 非同期ログ処理ワーカー
         */
        void AsyncLogWorker();

        /**
         * @brief コンソールに出力
         */
        void WriteToConsole(const String &formattedMessage, LogLevel level);

        /**
         * @brief ファイルに出力
         */
        void WriteToFile(const String &formattedMessage);

        /**
         * @brief 現在のスレッドIDを取得
         */
        uint32_t GetCurrentThreadId() const;

        /**
         * @brief ファイルサイズをチェックしてローテーション
         */
        void CheckAndRotateLogFile();

        /**
         * @brief バックアップファイル名を生成
         */
        String GenerateBackupFileName(uint32_t index) const;

    private:
        // 設定とステート
        LogConfig m_config;
        LogFormatterPtr m_formatter;
        NorvesLib::Thread::Atomic<bool> m_bInitialized{false};
        NorvesLib::Thread::Atomic<bool> m_bShutdown{false}; // 非同期処理用
        NorvesLib::Thread::RingBuffer<LogEntry, 1024> m_logQueue;
        NorvesLib::Thread::TaskPtr m_workerTask;
        mutable NorvesLib::Thread::Mutex m_mutex;

        // ファイル出力用
        NorvesLib::FileStream::FileStreamPtr m_logFileStream;
        NorvesLib::Thread::Atomic<size_t> m_currentFileSize{0};

        // 統計情報
        NorvesLib::Thread::Atomic<uint64_t> m_totalLogsWritten{0};
        NorvesLib::Thread::Atomic<uint64_t> m_droppedLogs{0};
    };

    /**
     * @brief グローバルログ関数群
     */
    inline void LogTrace(const String &category, const String &message)
    {
        Logger::GetInstance().Log(LogLevel::Trace, category, message);
    }

    inline void LogDebug(const String &category, const String &message)
    {
        Logger::GetInstance().Log(LogLevel::Debug, category, message);
    }

    inline void LogInfo(const String &category, const String &message)
    {
        Logger::GetInstance().Log(LogLevel::Info, category, message);
    }

    inline void LogWarning(const String &category, const String &message)
    {
        Logger::GetInstance().Log(LogLevel::Warning, category, message);
    }

    inline void LogError(const String &category, const String &message)
    {
        Logger::GetInstance().Log(LogLevel::Error, category, message);
    }

    inline void LogFatal(const String &category, const String &message)
    {
        Logger::GetInstance().Log(LogLevel::Fatal, category, message);
    }

} // namespace NorvesLib::Core::Logging
