#pragma once

#include "LogTypes.h"
#include "Thread/JobSystem.h"
#include "Thread/RingBuffer.h"
#include "Thread/Atomic.h"
#include <memory>
#include <functional>
#include <cstdio>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::Logging
{
    namespace Detail
    {
        template <typename T>
        class PrintfArgWrapper
        {
        public:
            using StoredType = std::decay_t<T>;

            explicit PrintfArgWrapper(T &&value)
                : m_value(std::forward<T>(value))
            {
            }

            decltype(auto) Get() const
            {
                if constexpr (std::is_enum_v<StoredType>)
                {
                    return static_cast<std::underlying_type_t<StoredType>>(m_value);
                }
                else
                {
                    return (m_value);
                }
            }

        private:
            StoredType m_value;
        };

        template <>
        class PrintfArgWrapper<const String &>
        {
        public:
            explicit PrintfArgWrapper(const String &value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.empty() ? "" : m_value.c_str();
            }

        private:
            String m_value;
        };

        template <>
        class PrintfArgWrapper<String &>
        {
        public:
            explicit PrintfArgWrapper(String &value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.empty() ? "" : m_value.c_str();
            }

        private:
            String m_value;
        };

        template <>
        class PrintfArgWrapper<String &&>
        {
        public:
            explicit PrintfArgWrapper(String &&value)
                : m_value(std::move(value))
            {
            }

            const char *Get() const
            {
                return m_value.empty() ? "" : m_value.c_str();
            }

        private:
            String m_value;
        };

        template <>
        class PrintfArgWrapper<const StringView &>
        {
        public:
            explicit PrintfArgWrapper(const StringView &value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.empty() ? "" : m_value.c_str();
            }

        private:
            String m_value;
        };

        template <>
        class PrintfArgWrapper<StringView &>
        {
        public:
            explicit PrintfArgWrapper(StringView &value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.empty() ? "" : m_value.c_str();
            }

        private:
            String m_value;
        };

        template <>
        class PrintfArgWrapper<StringView &&>
        {
        public:
            explicit PrintfArgWrapper(StringView &&value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.empty() ? "" : m_value.c_str();
            }

        private:
            String m_value;
        };

        template <>
        class PrintfArgWrapper<const std::string &>
        {
        public:
            explicit PrintfArgWrapper(const std::string &value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.c_str();
            }

        private:
            std::string m_value;
        };

        template <>
        class PrintfArgWrapper<std::string &>
        {
        public:
            explicit PrintfArgWrapper(std::string &value)
                : m_value(value)
            {
            }

            const char *Get() const
            {
                return m_value.c_str();
            }

        private:
            std::string m_value;
        };

        template <>
        class PrintfArgWrapper<std::string &&>
        {
        public:
            explicit PrintfArgWrapper(std::string &&value)
                : m_value(std::move(value))
            {
            }

            const char *Get() const
            {
                return m_value.c_str();
            }

        private:
            std::string m_value;
        };

        template <typename... Args>
        String FormatPrintfString(const char *format, Args &&...args)
        {
            auto wrappedArgs = std::make_tuple(PrintfArgWrapper<Args>(std::forward<Args>(args))...);

            int requiredSize = std::apply(
                [format](const auto &...wrapped)
                {
                    return std::snprintf(nullptr, 0, format, wrapped.Get()...);
                },
                wrappedArgs);

            if (requiredSize < 0)
            {
                return {};
            }

            std::string buffer(static_cast<size_t>(requiredSize) + 1, '\0');
            std::apply(
                [&](const auto &...wrapped)
                {
                    std::snprintf(buffer.data(), buffer.size(), format, wrapped.Get()...);
                },
                wrappedArgs);

            return String(buffer.c_str());
        }
    } // namespace Detail

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
         *
         * `%d`, `%u`, `%s`, `%zu`, `%.2f` などの printf 形式を使用します。
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
         * @brief ログシンクを登録する
         *
         * 登録後、各 @c LogEntry が配送経路から sink の @c OnLog へ届きます。
         * @c nullptr と重複登録は無視されます。@c Logger は sink を所有せず、
         * 破棄もしません。スレッドモデル・再入禁止・高速返却・所有権の契約は
         * @c ILogSink を参照してください。
         * @param sink 登録する sink（非所有。@c RemoveSink まで生存必須）
         */
        void AddSink(ILogSink *sink);

        /**
         * @brief ログシンクの登録を解除する
         *
         * sink を破棄する前、かつ @c Shutdown の前に必ず呼んでください。
         * 登録されていない sink を渡した場合は何もしません。
         * @param sink 解除する sink
         */
        void RemoveSink(ILogSink *sink);

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

        // sink（受け側）の登録リストと専用ロック
        VariableArray<ILogSink *> m_sinks;
        mutable NorvesLib::Thread::Mutex m_sinkMutex;
    };

    /**
     * @brief グローバルログ関数群
     */
    inline void LogTrace(const String &category, const String &message)
    {
#if NORVES_ENABLE_LOGGING
        Logger::GetInstance().Log(LogLevel::Trace, category, message);
#else
        (void)category;
        (void)message;
#endif
    }

    inline void LogDebug(const String &category, const String &message)
    {
#if NORVES_ENABLE_LOGGING
        Logger::GetInstance().Log(LogLevel::Debug, category, message);
#else
        (void)category;
        (void)message;
#endif
    }

    inline void LogInfo(const String &category, const String &message)
    {
#if NORVES_ENABLE_LOGGING
        Logger::GetInstance().Log(LogLevel::Info, category, message);
#else
        (void)category;
        (void)message;
#endif
    }

    inline void LogWarning(const String &category, const String &message)
    {
#if NORVES_ENABLE_LOGGING
        Logger::GetInstance().Log(LogLevel::Warning, category, message);
#else
        (void)category;
        (void)message;
#endif
    }

    inline void LogError(const String &category, const String &message)
    {
#if NORVES_ENABLE_LOGGING
        Logger::GetInstance().Log(LogLevel::Error, category, message);
#else
        (void)category;
        (void)message;
#endif
    }

    inline void LogFatal(const String &category, const String &message)
    {
#if NORVES_ENABLE_LOGGING
        Logger::GetInstance().Log(LogLevel::Fatal, category, message);
#else
        (void)category;
        (void)message;
#endif
    }

    // ========================================
    // LogFormat テンプレート実装
    // ========================================
    template <typename... Args>
    void Logger::LogFormat(LogLevel level, const String &category,
                           const char *filename, const char *function, int32_t lineNumber,
                           const char *format, Args &&...args)
    {
#if !NORVES_ENABLE_LOGGING
        (void)level;
        (void)category;
        (void)filename;
        (void)function;
        (void)lineNumber;
        (void)format;
        ((void)args, ...);
        return;
#else
        if (!IsLevelActive(level))
        {
            return;
        }

        if (format == nullptr)
        {
            Log(level, category, String{}, filename, function, lineNumber);
            return;
        }

        if constexpr (sizeof...(Args) == 0)
        {
            Log(level, category, String(format), filename, function, lineNumber);
            return;
        }

        String formattedMessage = Detail::FormatPrintfString(format, std::forward<Args>(args)...);
        if (formattedMessage.empty() && format[0] != '\0')
        {
            String errorMsg = String("Log format error (printf-style): ") + format;
            Log(LogLevel::Error, "Logger", errorMsg, filename, function, lineNumber);
            return;
        }

        Log(level, category, formattedMessage, filename, function, lineNumber);
#endif
    }

} // namespace NorvesLib::Core::Logging
