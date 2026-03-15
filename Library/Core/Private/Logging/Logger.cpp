#include "../Public/Logging/Logger.h"
#include <thread>
#include <filesystem>
#include <format>

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
        if (m_bInitialized.Load())
        {
            return true;
        }

#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 1: Starting initialization\n");
        printf("Logger::Initialize - Step 1: Starting initialization\n");
        fflush(stdout);
#endif NorvesLib::Thread::ScopedLock lock(m_mutex);

#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 2: Acquired mutex lock\n");
        printf("Logger::Initialize - Step 2: Acquired mutex lock\n");
        fflush(stdout);
#endif

        m_config = config;
        m_bShutdown.Store(false);

#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 3: Config set\n");
        printf("Logger::Initialize - Step 3: Config set\n");
        fflush(stdout);
#endif

        // デフォルトフォーマッターを設定
        if (!m_formatter)
        {
#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 4: Creating StandardLogFormatter\n");
            printf("Logger::Initialize - Step 4: Creating StandardLogFormatter\n");
            fflush(stdout);
#endif
            m_formatter = MakeShared<StandardLogFormatter>();
#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 5: StandardLogFormatter created\n");
            printf("Logger::Initialize - Step 5: StandardLogFormatter created\n");
            fflush(stdout);
#endif
        } // ファイル出力の初期化
        if (m_config.outputType == LogOutput::File || m_config.outputType == LogOutput::Both)
        {
#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 6: Creating FileStream\n");
#endif
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

#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 7: FileStream created and cleared\n");
#endif

            if (!m_logFileStream || !m_logFileStream->IsOpen())
            {
                // ファイル出力に失敗した場合、コンソール出力のみに変更
                m_config.outputType = LogOutput::Console;
                std::cerr << "Failed to open log file: " << m_config.logFilePath.c_str()
                          << ". Switching to console output only." << std::endl;
            }
        } // Windows コンソールでのUTF-8サポート
#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 8: Setting console UTF-8\n");
        printf("Logger::Initialize - Step 8: Setting console UTF-8\n");
        fflush(stdout);

        // 一時的にUTF-8設定をスキップ
        /*
        SetConsoleOutputCP(CP_UTF8);
        _setmode(_fileno(stdout), _O_U8TEXT);
        _setmode(_fileno(stderr), _O_U8TEXT);
        */

        OutputDebugStringA("Logger::Initialize - Step 9: Console UTF-8 set (skipped)\n");
        printf("Logger::Initialize - Step 9: Console UTF-8 set (skipped)\n");
        fflush(stdout);
#endif

        // 非同期ログ処理の開始
        if (m_config.bAsyncLogging)
        {
#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 10: Creating async task\n");
#endif
            m_workerTask = NorvesLib::Thread::Task::Create([this]()
                                                           { AsyncLogWorker(); });
#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 11: Submitting task to JobSystem\n");
#endif
            NorvesLib::Thread::JobSystem::Get().SubmitTask(m_workerTask);
#ifdef _WIN32
            OutputDebugStringA("Logger::Initialize - Step 12: Task submitted\n");
#endif
        }

#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 13: Setting initialized flag\n");
#endif
        m_bInitialized.Store(true);

#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 14: About to log initialization message\n");
#endif
        // 初期化完了ログ（一時的に無効化してダブルフリーエラーを回避）
        // Log(LogLevel::Info, "Logger", "Logger initialized successfully");

#ifdef _WIN32
        OutputDebugStringA("Logger::Initialize - Step 15: Initialization complete\n");
#endif
        return true;
    }

    void Logger::Shutdown()
    {
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
    }

    void Logger::Log(LogLevel level, const String &category, const String &message,
                     const char *filename, const char *function, int32_t lineNumber)
    {
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

    bool Logger::IsLevelActive(LogLevel level) const
    {
        return level >= m_config.minLevel;
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
        if (m_config.outputType == LogOutput::Console || m_config.outputType == LogOutput::Both)
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
#ifdef _WIN32
        OutputDebugStringA("Logger::WriteToFile - Entry\n");
#endif

        if (!m_logFileStream || !m_logFileStream->IsOpen())
        {
#ifdef _WIN32
            OutputDebugStringA("Logger::WriteToFile - File stream not open\n");
#endif
            return;
        }

#ifdef _WIN32
        OutputDebugStringA("Logger::WriteToFile - Creating message with newline\n");
#endif

        // 修正されたStringクラスを使用した安全な文字列連結
        String messageWithNewline = formattedMessage;
        messageWithNewline += "\n";

#ifdef _WIN32
        OutputDebugStringA("Logger::WriteToFile - Writing to file stream\n");
#endif

        size_t bytesWritten = m_logFileStream->WriteString(messageWithNewline);

#ifdef _WIN32
        OutputDebugStringA("Logger::WriteToFile - File write completed\n");
#endif

        if (bytesWritten > 0)
        {
            m_currentFileSize.FetchAdd(bytesWritten);
            CheckAndRotateLogFile();
        }

#ifdef _WIN32
        OutputDebugStringA("Logger::WriteToFile - Exit\n");
#endif
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
