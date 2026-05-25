#pragma once

#include "Logger.h"
#include <sstream>

/**
 * @file LogMacros.h
 * @brief ログ出力用の便利なマクロ定義
 *
 * このファイルで定義されたマクロを使用することで、
 * 簡単にログ出力とファイル情報の記録ができます。
 * 
 * ## 使用方法
 * 
 * すべてのログマクロは printf 形式のフォーマット機能を統合しています：
 * - 単純な文字列: `NORVES_LOG_INFO("Category", "メッセージ")`
 * - フォーマット付き: `NORVES_LOG_INFO("Category", "値: %d, 名前: %s", value, name)`
 * - フォーマット書式は `%d`, `%u`, `%s`, `%zu`, `%.2f` などの printf スタイルです
 * 
 * 従来の `_F` サフィックス付きマクロも互換性のために残されています。
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

// ===== 内部実装用マクロ（直接使用しないでください） =====

// 引数の有無を判定するヘルパーマクロ
#define NORVES_LOG_INTERNAL_EXPAND(x) x
#define NORVES_LOG_INTERNAL_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, NAME, ...) NAME

// 基本ログマクロ（フォーマットなし）
#define NORVES_LOG_SIMPLE(level, category, message)                               \
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

// 基本ログマクロ（フォーマット付き）
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

// ===== 統合ログマクロ =====
// 文字列のみでも printf 形式でも同じ入口を使用する

#define NORVES_LOG(level, category, ...)                                          \
    NORVES_LOG_FORMAT(level, category, __VA_ARGS__)

// ===== レベル別ログマクロ（統合版 - フォーマット対応） =====

#define NORVES_LOG_TRACE(category, ...) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Trace, category, __VA_ARGS__)

#define NORVES_LOG_DEBUG(category, ...) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Debug, category, __VA_ARGS__)

#define NORVES_LOG_INFO(category, ...) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Info, category, __VA_ARGS__)

#define NORVES_LOG_WARNING(category, ...) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Warning, category, __VA_ARGS__)

#define NORVES_LOG_ERROR(category, ...) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Error, category, __VA_ARGS__)

#define NORVES_LOG_FATAL(category, ...) \
    NORVES_LOG(NorvesLib::Core::Logging::LogLevel::Fatal, category, __VA_ARGS__)

// ===== フォーマット付きレベル別ログマクロ（後方互換性のため維持） =====

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

// ===== 簡易ログマクロ（デフォルトカテゴリ使用・フォーマット対応） =====

#define LOG_TRACE(...) \
    NORVES_LOG_TRACE("General", __VA_ARGS__)

#define LOG_DEBUG(...) \
    NORVES_LOG_DEBUG("General", __VA_ARGS__)

#define LOG_INFO(...) \
    NORVES_LOG_INFO("General", __VA_ARGS__)

#define LOG_WARNING(...) \
    NORVES_LOG_WARNING("General", __VA_ARGS__)

#define LOG_ERROR(...) \
    NORVES_LOG_ERROR("General", __VA_ARGS__)

#define LOG_FATAL(...) \
    NORVES_LOG_FATAL("General", __VA_ARGS__)

// ===== フォーマット付き簡易ログマクロ（後方互換性のため維持） =====

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
