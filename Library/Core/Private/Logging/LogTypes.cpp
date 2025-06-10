#include "../Public/Logging/LogTypes.h"
#include <iomanip>
#include <sstream>
#include <ctime>

namespace NorvesLib::Core::Logging
{

    String StandardLogFormatter::Format(const LogEntry &entry) const
    {
        std::ostringstream oss;

        // タイムスタンプの生成
        auto timeT = std::chrono::system_clock::to_time_t(entry.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      entry.timestamp.time_since_epoch()) %
                  1000;

        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &timeT);
#else
        localtime_r(&timeT, &localTime);
#endif

        // フォーマット: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [THREAD] [CATEGORY] [FILE:FUNCTION:LINE] MESSAGE
        oss << "[" << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";

        // ログレベル（カラー付き）
        oss << "[" << GetLogLevelColor(entry.level) << LogLevelToString(entry.level)
            << RESET_COLOR << "] ";

        // スレッドID
        oss << "[T:" << std::setfill('0') << std::setw(4) << entry.threadId << "] ";

        // カテゴリ
        if (!entry.category.empty())
        {
            oss << "[" << entry.category.c_str() << "] ";
        }

        // ソース情報
        if (!entry.filename.empty())
        {
            oss << "[" << entry.filename.c_str() << ":" << entry.function.c_str()
                << ":" << entry.lineNumber << "] ";
        }        // メッセージ
        oss << entry.message.c_str();

        // std::stringを経由せずに直接変換
        return String(oss.str().c_str());
    }

    String JsonLogFormatter::Format(const LogEntry &entry) const
    {
        std::ostringstream oss;

        // タイムスタンプの生成（ISO 8601形式）
        auto timeT = std::chrono::system_clock::to_time_t(entry.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      entry.timestamp.time_since_epoch()) %
                  1000;

        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &timeT);
#else
        localtime_r(&timeT, &localTime);
#endif

        oss << "{"
            << "\"timestamp\":\"" << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z\","
            << "\"level\":\"" << LogLevelToString(entry.level) << "\","
            << "\"thread_id\":" << entry.threadId << ","
            << "\"category\":\"" << entry.category.c_str() << "\","
            << "\"message\":\"" << entry.message.c_str() << "\"";

        if (!entry.filename.empty())
        {
            oss << ",\"source\":{"
                << "\"file\":\"" << entry.filename.c_str() << "\","
                << "\"function\":\"" << entry.function.c_str() << "\","
                << "\"line\":" << entry.lineNumber
                << "}";
        }        oss << "}";

        // std::stringを経由せずに直接変換
        return String(oss.str().c_str());
    }

} // namespace NorvesLib::Core::Logging