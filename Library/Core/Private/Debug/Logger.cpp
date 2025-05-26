#include "Debug/Logger.h"
#include <thread>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <Windows.h>
#include <debugapi.h>
#endif

namespace NorvesLib::Core::Debug
{
    Logger& Logger::GetInstance()
    {
        static Logger instance;
        return instance;
    }

    Logger::~Logger()
    {
        StopAsyncOutput();
        Flush();
        
        if (m_logFile && m_logFile->is_open())
        {
            m_logFile->close();
        }
    }

    void Logger::SetLogLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_minLogLevel = level;
    }

    void Logger::EnableFileOutput(const String& filePath)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_logFilePath = filePath;
        m_logFile = MakeUnique<std::ofstream>(filePath.c_str(), std::ios::app);
        m_bFileOutput = m_logFile && m_logFile->is_open();
        
        if (m_bFileOutput)
        {
            // ファイル出力開始のログ
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            *m_logFile << "\n=== Log Session Started at " 
                       << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") 
                       << " ===\n" << std::flush;
        }
    }

    void Logger::EnableConsoleOutput(bool enable)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_bConsoleOutput = enable;
    }

    void Logger::EnableDebugOutput(bool enable)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_bDebugOutput = enable;
    }

    void Logger::Log(LogLevel level, const String& message, const String& file, int line)
    {
        // レベルチェック
        if (level < m_minLogLevel)
        {
            return;
        }

        LogEntry entry(level, message, file, line);

        if (m_bAsyncMode)
        {
            // 非同期モード：リングバッファに追加
            if (!m_logBuffer.TryWrite(entry))
            {
                // バッファが満杯の場合は同期出力にフォールバック
                WriteLogEntry(entry);
            }
        }
        else
        {
            // 同期モード：即座に出力
            WriteLogEntry(entry);
        }
    }

    void Logger::Flush()
    {
        if (m_bAsyncMode)
        {
            // 非同期モードの場合、バッファ内の全エントリを処理
            LogEntry entry;
            while (m_logBuffer.TryRead(entry))
            {
                WriteLogEntry(entry);
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_bConsoleOutput)
        {
            std::cout.flush();
        }
        
        if (m_bFileOutput && m_logFile)
        {
            m_logFile->flush();
        }
    }

    void Logger::StartAsyncOutput()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_bAsyncMode)
        {
            m_bAsyncMode = true;
            m_bStopWorker = false;
            m_workerThread = MakeUnique<std::thread>(&Logger::AsyncOutputWorker, this);
        }
    }

    void Logger::StopAsyncOutput()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_bAsyncMode)
            {
                return;
            }
            
            m_bStopWorker = true;
            m_bAsyncMode = false;
        }
        
        if (m_workerThread && m_workerThread->joinable())
        {
            m_workerThread->join();
            m_workerThread.reset();
        }
    }

    void Logger::WriteLogEntry(const LogEntry& entry)
    {
        // タイムスタンプの文字列化
        auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count()
           << "][" << LogLevelToString(entry.level) << "] "
           << entry.message;

        // ファイル情報を追加（Debug/Errorレベルの場合）
        if (entry.level == LogLevel::Debug || entry.level >= LogLevel::Error)
        {
            // ファイル名のみ抽出（フルパスではなく）
            size_t pos = entry.file.find_last_of("/\\");
            String fileName = (pos != String::npos) ? entry.file.substr(pos + 1) : entry.file;
            ss << " (" << fileName << ":" << entry.line << ")";
        }

        String formattedMessage = ss.str();

        std::lock_guard<std::mutex> lock(m_mutex);

        // コンソール出力
        if (m_bConsoleOutput)
        {
            std::cout << formattedMessage << std::endl;
        }

        // ファイル出力
        if (m_bFileOutput && m_logFile && m_logFile->is_open())
        {
            *m_logFile << formattedMessage << std::endl;
        }

        // デバッグ出力（Windows）
        if (m_bDebugOutput)
        {
#ifdef _WIN32
            OutputDebugStringA((formattedMessage + "\n").c_str());
#endif
        }
    }

    String Logger::LogLevelToString(LogLevel level) const
    {
        switch (level)
        {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO ";
        case LogLevel::Warning:  return "WARN ";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRIT ";
        default:                 return "UNKN ";
        }
    }

    void Logger::AsyncOutputWorker()
    {
        while (!m_bStopWorker)
        {
            LogEntry entry;
            if (m_logBuffer.TryRead(entry))
            {
                WriteLogEntry(entry);
            }
            else
            {
                // バッファが空の場合は少し待機
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // 停止時に残りのエントリを処理
        LogEntry entry;
        while (m_logBuffer.TryRead(entry))
        {
            WriteLogEntry(entry);
        }
    }

} // namespace NorvesLib::Core::Debug