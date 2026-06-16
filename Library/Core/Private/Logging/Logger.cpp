#include "../Public/Logging/Logger.h"
#include <thread>
#include <filesystem>
#include <format>
#include <algorithm>
#include <cassert>

#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace NorvesLib::Core::Logging
{

#if NORVES_ENABLE_LOGGING
    // sink 配送中フラグ（Debug の再入検出用、スレッドローカル）
    static thread_local bool t_bInSinkDispatch = false;

    // sink 配送中フラグの RAII ガード。例外でスコープを抜けても確実に false へ戻す
    // （ILogSink::OnLog からの Logger 再入を Debug assert で検出するための状態）。
    struct SinkDispatchGuard
    {
        SinkDispatchGuard() { t_bInSinkDispatch = true; }
        ~SinkDispatchGuard() { t_bInSinkDispatch = false; }
    };
#endif

    Logger &Logger::GetInstance()
    {
        static Logger instance;
        return instance;
    }

    Logger::~Logger()
    {
        Shutdown();
    }
    bool Logger::Initialize(const LogConfig &config)
    {
#if !NORVES_ENABLE_LOGGING
        (void)config;
        return true;
#else
        if (m_bInitialized.Load())
        {
            return true;
        }

        NorvesLib::Thread::ScopedLock lock(m_mutex);

        m_config = config;
        m_bShutdown.Store(false);

        // デフォルトフォーマッターを設定
        if (!m_formatter)
        {
            m_formatter = MakeShared<StandardLogFormatter>();
        } // ファイル出力の初期化
        if (m_config.outputType == LogOutput::File || m_config.outputType == LogOutput::Both)
        {
            // まずWriteモードでファイルを開いて古いデータをクリア
            m_logFileStream = NorvesLib::FileStream::FileStream::Create(
                m_config.logFilePath,
                NorvesLib::FileStream::FileMode::Write,
                NorvesLib::FileStream::FileAccess::Write);

            if (m_logFileStream && m_logFileStream->IsOpen())
            {
                // ファイルをクリアしたので、一度閉じて再度Appendモードで開く
                m_logFileStream->Close();
                m_logFileStream = NorvesLib::FileStream::FileStream::Create(
                    m_config.logFilePath,
                    NorvesLib::FileStream::FileMode::Append,
                    NorvesLib::FileStream::FileAccess::Write);
            }

            if (!m_logFileStream || !m_logFileStream->IsOpen())
            {
                // ファイル出力に失敗した場合、コンソール出力のみに変更
                m_config.outputType = LogOutput::Console;
                std::cerr << "Failed to open log file: " << m_config.logFilePath.c_str()
                          << ". Switching to console output only." << std::endl;
            }
        } // Windows コンソールでのUTF-8サポート
#ifdef _WIN32
        // 一時的にUTF-8設定をスキップ
        /*
        SetConsoleOutputCP(CP_UTF8);
        _setmode(_fileno(stdout), _O_U8TEXT);
        _setmode(_fileno(stderr), _O_U8TEXT);
        */
#endif

        // 非同期ログ処理の開始
        if (m_config.bAsyncLogging)
        {
            m_workerTask = NorvesLib::Thread::Task::Create([this]()
                                                           { AsyncLogWorker(); });
            NorvesLib::Thread::JobSystem::Get().SubmitTask(m_workerTask);
        }

        m_bInitialized.Store(true);

        // 初期化完了ログ（一時的に無効化してダブルフリーエラーを回避）
        // Log(LogLevel::Info, "Logger", "Logger initialized successfully");

        return true;
#endif
    }

    void Logger::Shutdown()
    {
#if !NORVES_ENABLE_LOGGING
        return;
#else
        if (!m_bInitialized.Load())
        {
            return;
        }

        Log(LogLevel::Info, "Logger", "Shutting down logger...");

        m_bShutdown.Store(true);

        // 非同期ワーカーの停止を待機
        if (m_workerTask)
        {
            m_workerTask->Wait();
            m_workerTask.reset();
        }

        // 残りのログエントリを処理
        LogEntry entry;
        while (m_logQueue.TryRead(entry))
        {
            ProcessLogEntry(entry);
        }

        // ファイルストリームを閉じる
        if (m_logFileStream)
        {
            m_logFileStream->Flush();
            m_logFileStream->Close();
            m_logFileStream.reset();
        }

        m_bInitialized.Store(false);
#endif
    }

    void Logger::Log(LogLevel level, const String &category, const String &message,
                     const char *filename, const char *function, int32_t lineNumber)
    {
#if !NORVES_ENABLE_LOGGING
        (void)level;
        (void)category;
        (void)message;
        (void)filename;
        (void)function;
        (void)lineNumber;
        return;
#else
        if (!IsLevelActive(level) || m_bShutdown.Load())
        {
            return;
        }

        LogEntry entry;
        entry.level = level;
        entry.message = message;
        entry.category = category;
        entry.filename = filename ? String(filename) : String{};
        entry.function = function ? String(function) : String{};
        entry.lineNumber = lineNumber;
        entry.timestamp = std::chrono::system_clock::now();
        entry.threadId = GetCurrentThreadId();
        if (m_config.bAsyncLogging && m_bInitialized.Load())
        {
            // 非同期処理でキューに追加
            if (!m_logQueue.TryWrite(entry))
            {
                // キューが満杯の場合、ドロップされたログをカウント
                m_droppedLogs.FetchAdd(1);
            }
        }
        else
        {
            // 同期処理で即座に出力
            ProcessLogEntry(entry);
        }
#endif
    }

    void Logger::UpdateConfig(const LogConfig &config)
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);
        m_config = config;
    }

    void Logger::SetFormatter(LogFormatterPtr formatter)
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);
        m_formatter = formatter ? formatter : MakeShared<StandardLogFormatter>();
    }

    void Logger::AddSink(ILogSink *sink)
    {
#if !NORVES_ENABLE_LOGGING
        (void)sink;
#else
        // 配送中（OnLog 内）からの再入は禁止（m_sinkMutex は非再帰）
        assert(!t_bInSinkDispatch && "ILogSink::OnLog must not re-enter the logger");

        if (sink == nullptr)
        {
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_sinkMutex);
        if (std::find(m_sinks.begin(), m_sinks.end(), sink) != m_sinks.end())
        {
            return; // 重複登録を無視
        }
        m_sinks.push_back(sink);
#endif
    }

    void Logger::RemoveSink(ILogSink *sink)
    {
#if !NORVES_ENABLE_LOGGING
        (void)sink;
#else
        // 配送中（OnLog 内）からの再入は禁止（m_sinkMutex は非再帰）
        assert(!t_bInSinkDispatch && "ILogSink::OnLog must not re-enter the logger");

        NorvesLib::Thread::ScopedLock lock(m_sinkMutex);
        auto it = std::find(m_sinks.begin(), m_sinks.end(), sink);
        if (it != m_sinks.end())
        {
            m_sinks.erase(it);
        }
#endif
    }

    bool Logger::IsLevelActive(LogLevel level) const
    {
#if !NORVES_ENABLE_LOGGING
        (void)level;
        return false;
#else
        return level >= m_config.minLevel;
#endif
    }

    void Logger::Flush()
    {
        if (m_logFileStream && m_logFileStream->IsOpen())
        {
            m_logFileStream->Flush();
        }

        std::cout.flush();
        std::cerr.flush();
    }

    void Logger::RotateLogFile()
    {
        if (!m_logFileStream)
        {
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_mutex);

        // 現在のファイルを閉じる
        m_logFileStream->Close();

        // バックアップファイルをローテーション
        for (uint32_t i = m_config.maxLogFiles - 1; i > 0; --i)
        {
            String oldFile = GenerateBackupFileName(i - 1);
            String newFile = GenerateBackupFileName(i);

            if (std::filesystem::exists(oldFile.c_str()))
            {
                if (std::filesystem::exists(newFile.c_str()))
                {
                    std::filesystem::remove(newFile.c_str());
                }
                std::filesystem::rename(oldFile.c_str(), newFile.c_str());
            }
        }

        // 現在のファイルを .1 にリネーム
        if (std::filesystem::exists(m_config.logFilePath.c_str()))
        {
            std::filesystem::rename(m_config.logFilePath.c_str(), GenerateBackupFileName(0).c_str());
        }

        // 新しいファイルを開く
        m_logFileStream = NorvesLib::FileStream::FileStream::Create(
            m_config.logFilePath,
            NorvesLib::FileStream::FileMode::Write,
            NorvesLib::FileStream::FileAccess::Write);

        m_currentFileSize.Store(0);
    }

    void Logger::ProcessLogEntry(const LogEntry &entry)
    {
        if (!m_formatter)
        {
            return;
        }

        String formattedMessage = m_formatter->Format(entry);

        // コンソール出力
        if ((m_config.outputType == LogOutput::Console || m_config.outputType == LogOutput::Both) &&
            entry.level >= m_config.consoleMinLevel)
        {
            WriteToConsole(formattedMessage, entry.level);
        }

        // ファイル出力
        if (m_config.outputType == LogOutput::File || m_config.outputType == LogOutput::Both)
        {
            WriteToFile(formattedMessage);
        }

        m_totalLogsWritten.FetchAdd(1);

        if (m_config.bAutoFlush)
        {
            Flush();
        }

#if NORVES_ENABLE_LOGGING
        // 登録済み sink へ配送（m_sinkMutex 下で直列化、m_mutex は取らない）
        {
            NorvesLib::Thread::ScopedLock sinkLock(m_sinkMutex);
            SinkDispatchGuard dispatchGuard;
            for (ILogSink *sink : m_sinks)
            {
                if (sink != nullptr)
                {
                    sink->OnLog(entry);
                }
            }
        }
#endif
    }

    void Logger::AsyncLogWorker()
    {
        while (!m_bShutdown.Load())
        {
            LogEntry entry;
            if (m_logQueue.TryRead(entry))
            {
                ProcessLogEntry(entry);
            }
            else
            {
                // キューが空の場合、少し待機
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // シャットダウン時に残りのエントリを処理
        LogEntry entry;
        while (m_logQueue.TryRead(entry))
        {
            ProcessLogEntry(entry);
        }
    }

    void Logger::WriteToConsole(const String &formattedMessage, LogLevel level)
    {
        if (level >= LogLevel::Error)
        {
            std::cerr << formattedMessage.c_str() << std::endl;
        }
        else
        {
            std::cout << formattedMessage.c_str() << std::endl;
        }
    }
    void Logger::WriteToFile(const String &formattedMessage)
    {
        if (!m_logFileStream || !m_logFileStream->IsOpen())
        {
            return;
        }

        // 修正されたStringクラスを使用した安全な文字列連結
        String messageWithNewline = formattedMessage;
        messageWithNewline += "\n";

        size_t bytesWritten = m_logFileStream->WriteString(messageWithNewline);

        if (bytesWritten > 0)
        {
            m_currentFileSize.FetchAdd(bytesWritten);
            CheckAndRotateLogFile();
        }
    }
    uint32_t Logger::GetCurrentThreadId() const
    {
#ifdef _WIN32
        return static_cast<uint32_t>(::GetCurrentThreadId());
#else
        return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
    }

    void Logger::CheckAndRotateLogFile()
    {
        if (m_currentFileSize.Load() > m_config.maxLogFileSize)
        {
            RotateLogFile();
        }
    }

    String Logger::GenerateBackupFileName(uint32_t index) const
    {
        std::filesystem::path logPath(m_config.logFilePath.c_str());
        String stem = String(logPath.stem().string());
        String extension = String(logPath.extension().string());

        return stem + "." + String(std::to_string(index + 1)) + extension;
    }
} // namespace NorvesLib::Core::Logging
