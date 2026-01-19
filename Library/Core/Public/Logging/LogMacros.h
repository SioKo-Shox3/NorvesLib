#pragma once

#include "Logger.h"
#include <sstream>

/**
 * @file LogMacros.h
 * @brief ログ出力用の便利なマクロ定義
 *
 * このファイルで定義されたマクロを使用することで、
 * 簡単にログ出力とファイル情報の記録ができます。
 */

namespace NorvesLib::Core::Logging
{
    /**
     * @brief ファイル名から拡張子とパスを取り除く
     */
    constexpr const char *ExtractFileName(const char *path)
    {
        const char *filename = path;
        for (const char *p = path; *p; ++p)
        {
            if (*p == '/' || *p == '\\')
            {
                filename = p + 1;
            }
        }
        return filename;
    }

} // namespace NorvesLib::Core::Logging

// ===== 基本ログマクロ =====

#define NORVES_LOG(level, category, message)                                      \
    do                                                                            \
    {                                                                             \
        if (NorvesLib::Core::Logging::Logger::GetInstance().IsLevelActive(level)) \
        {                                                                         \
            NorvesLib::Core::Logging::Logger::GetInstance().Log(                  \
                level, category, message,                                         \
                NorvesLib::Core::Logging::ExtractFileName(__FILE__),              \
                __FUNCTION__, __LINE__);                                          \
        }                                                                         \
    } while (0)

#define NORVES_LOG_FORMAT(level, category, format, ...)                           \
    do                                                                            \
    {                                                                             \
        if (NorvesLib::Core::Logging::Logger::GetInstance().IsLevelActive(level)) \
        {                                                                         \
            NorvesLib::Core::Logging::Logger::GetInstance().LogFormat(            \
                level, category,                                                  \
                NorvesLib::Core::Logging::ExtractFileName(__FILE__),              \
                __FUNCTION__, __LINE__,                                           \
                format, ##__VA_ARGS__);                                           \
        }                                                                         \
    } while (0)

// ===== レベル別ログマクロ =====

#define NORVES_LOG_TRACE(category, message) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Trace, category, message)

#define NORVES_LOG_DEBUG(category, message) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Debug, category, message)

#define NORVES_LOG_INFO(category, message) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Info, category, message)

#define NORVES_LOG_WARNING(category, message) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Warning, category, message)

#define NORVES_LOG_ERROR(category, message) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Error, category, message)

#define NORVES_LOG_FATAL(category, message) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Fatal, category, message)

// ===== フォーマット付きレベル別ログマクロ =====

#define NORVES_LOG_TRACE_F(category, format, ...) \
    NORVES_LOG_FORMAT(NorvesLib::Core::Logging::LogLevel::Trace, category, format, ##__VA_ARGS__)

#define NORVES_LOG_DEBUG_F(category, format, ...) \
    NORVES_LOG_FORMAT(NorvesLib::Core::Logging::LogLevel::Debug, category, format, ##__VA_ARGS__)

#define NORVES_LOG_INFO_F(category, format, ...) \
    NORVES_LOG_FORMAT(NorvesLib::Core::Logging::LogLevel::Info, category, format, ##__VA_ARGS__)

#define NORVES_LOG_WARNING_F(category, format, ...) \
    NORVES_LOG_FORMAT(NorvesLib::Core::Logging::LogLevel::Warning, category, format, ##__VA_ARGS__)

#define NORVES_LOG_ERROR_F(category, format, ...) \
    NORVES_LOG_FORMAT(NorvesLib::Core::Logging::LogLevel::Error, category, format, ##__VA_ARGS__)

#define NORVES_LOG_FATAL_F(category, format, ...) \
    NORVES_LOG_FORMAT(NorvesLib::Core::Logging::LogLevel::Fatal, category, format, ##__VA_ARGS__)

// ===== 簡易ログマクロ（デフォルトカテゴリ使用） =====

#define LOG_TRACE(message) \
    NORVES_LOG_TRACE("General", message)

#define LOG_DEBUG(message) \
    NORVES_LOG_DEBUG("General", message)

#define LOG_INFO(message) \
    NORVES_LOG_INFO("General", message)

#define LOG_WARNING(message) \
    NORVES_LOG_WARNING("General", message)

#define LOG_ERROR(message) \
    NORVES_LOG_ERROR("General", message)

#define LOG_FATAL(message) \
    NORVES_LOG_FATAL("General", message)

// ===== フォーマット付き簡易ログマクロ =====

#define LOG_TRACE_F(format, ...) \
    NORVES_LOG_TRACE_F("General", format, ##__VA_ARGS__)

#define LOG_DEBUG_F(format, ...) \
    NORVES_LOG_DEBUG_F("General", format, ##__VA_ARGS__)

#define LOG_INFO_F(format, ...) \
    NORVES_LOG_INFO_F("General", format, ##__VA_ARGS__)

#define LOG_WARNING_F(format, ...) \
    NORVES_LOG_WARNING_F("General", format, ##__VA_ARGS__)

#define LOG_ERROR_F(format, ...) \
    NORVES_LOG_ERROR_F("General", format, ##__VA_ARGS__)

#define LOG_FATAL_F(format, ...) \
    NORVES_LOG_FATAL_F("General", format, ##__VA_ARGS__)

// ===== 条件付きログマクロ =====

#define NORVES_LOG_IF(condition, level, category, message)                                       \
    do                                                                                           \
    {                                                                                            \
        if ((condition) && NorvesLib::Core::Logging::Logger::GetInstance().IsLevelActive(level)) \
        {                                                                                        \
            NORVES_LOG(level, category, message);                                                \
        }                                                                                        \
    } while (0)

#define LOG_ERROR_IF(condition, message) \
    NORVES_LOG_IF(condition, NorvesLib::Core::Logging::LogLevel::Error, "General", message)

#define LOG_WARNING_IF(condition, message) \
    NORVES_LOG_IF(condition, NorvesLib::Core::Logging::LogLevel::Warning, "General", message)

// ===== デバッグ専用マクロ（リリースビルドでは無効化） =====

#ifdef _DEBUG
#define NORVES_DEBUG_LOG(category, message) \
    NORVES_LOG_DEBUG(category, message)

#define NORVES_DEBUG_LOG_F(category, format, ...) \
    NORVES_LOG_DEBUG_F(category, format, ##__VA_ARGS__)

#define DEBUG_LOG(message) \
    LOG_DEBUG(message)

#define DEBUG_LOG_F(format, ...) \
    LOG_DEBUG_F(format, ##__VA_ARGS__)
#else
#define NORVES_DEBUG_LOG(category, message) \
    do                                      \
    {                                       \
    } while (0)
#define NORVES_DEBUG_LOG_F(category, format, ...) \
    do                                            \
    {                                             \
    } while (0)
#define DEBUG_LOG(message) \
    do                     \
    {                      \
    } while (0)
#define DEBUG_LOG_F(format, ...) \
    do                           \
    {                            \
    } while (0)
#endif

// ===== 関数スコープトレース用マクロ =====

#define NORVES_FUNCTION_TRACE() \
    NORVES_LOG_TRACE("Function", NorvesLib::Core::Container::String("Entering ") + __FUNCTION__)

#define NORVES_FUNCTION_TRACE_CATEGORY(category) \
    NORVES_LOG_TRACE(category, NorvesLib::Core::Container::String("Entering ") + __FUNCTION__)

// ===== パフォーマンス測定用マクロ =====

#define NORVES_LOG_PERFORMANCE_START(name) \
    auto start_time_##name = std::chrono::high_resolution_clock::now()

#define NORVES_LOG_PERFORMANCE_END(name, category)                                                           \
    do                                                                                                       \
    {                                                                                                        \
        auto end_time = std::chrono::high_resolution_clock::now();                                           \
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_##name); \
        NORVES_LOG_INFO_F(category, "Performance [%s]: %lld microseconds", #name, duration.count());         \
    } while (0)
