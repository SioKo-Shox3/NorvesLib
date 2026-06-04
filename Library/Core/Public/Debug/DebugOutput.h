#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include "Logging/LoggingModule.h"
#include "Container/Containers.h"
#include <chrono>
#include <type_traits>

namespace NorvesLib::Debug
{
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Logging;

    /**
     * @brief デバッグ出力用のユーティリティクラス
     *
     * 開発時のデバッグ情報出力を簡単にするためのヘルパークラス群
     */
    class DebugOutput
    {
    public:
        /**
         * @brief 変数の値をデバッグ出力
         * @param varName 変数名
         * @param value 変数の値
         * @param category カテゴリ（省略可能）
         */
        template <typename T>
        static void PrintVariable(const String &varName, const T &value, const String &category = "Debug")
        {
#if NORVES_ENABLE_DEBUG_OUTPUT
            NORVES_LOG_DEBUG_F(category, "%s = %s", varName.c_str(), ToString(value).c_str());
#else
            (void)varName;
            (void)value;
            (void)category;
#endif
        }

        /**
         * @brief 配列・コンテナの内容をデバッグ出力
         * @param containerName コンテナ名
         * @param container コンテナ
         * @param category カテゴリ（省略可能）
         */
        template <typename Container>
        static void PrintContainer(const String &containerName, const Container &container, const String &category = "Debug")
        {
#if NORVES_ENABLE_DEBUG_OUTPUT
            NORVES_LOG_DEBUG_F(category, "%s (size: %zu):", containerName.c_str(), container.size());

            size_t index = 0;
            for (const auto &item : container)
            {
                NORVES_LOG_DEBUG_F(category, "  [%zu] = %s", index++, ToString(item).c_str());
            }
#else
            (void)containerName;
            (void)container;
            (void)category;
#endif
        }

        /**
         * @brief 関数の開始・終了を自動トレース
         */
        class FunctionTracer
        {
        public:
#if NORVES_ENABLE_DEBUG_OUTPUT
            FunctionTracer(const String &functionName, const String &category = "Function")
                : m_functionName(functionName), m_category(category)
            {
                m_startTime = std::chrono::high_resolution_clock::now();
                NORVES_LOG_TRACE_F(m_category, ">>> Entering %s", m_functionName.c_str());
            }

            ~FunctionTracer()
            {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
                NORVES_LOG_TRACE_F(m_category, "<<< Exiting %s (took %lld μs)",
                                   m_functionName.c_str(), duration.count());
            }

        private:
            String m_functionName;
            String m_category;
            std::chrono::high_resolution_clock::time_point m_startTime;
#else
        public:
            FunctionTracer(const String &functionName, const String &category = "Function")
            {
                (void)functionName;
                (void)category;
            }

            ~FunctionTracer() = default;
#endif
        };

        /**
         * @brief メモリ使用量を監視
         */
        class MemoryMonitor
        {
        public:
            static void LogMemoryUsage(const String &location = "")
            {
#if NORVES_ENABLE_DEBUG_OUTPUT
#ifdef _WIN32
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
                {
                    String prefix = location.empty() ? String{} : location + ": ";
                    NORVES_LOG_INFO_F("Memory", "%sWorking Set: %.2f MB, Page File: %.2f MB",
                                      prefix.c_str(),
                                      pmc.WorkingSetSize / (1024.0 * 1024.0),
                                      pmc.PagefileUsage / (1024.0 * 1024.0));
                }
#else
                // Linux/Unix implementation can be added here
                NORVES_LOG_INFO("Memory", "Memory monitoring not implemented for this platform");
#endif
#else
                (void)location;
#endif
            }
        };

        /**
         * @brief パフォーマンス測定ユーティリティ
         */
        class PerformanceProfiler
        {
        public:
#if NORVES_ENABLE_PROFILING
            PerformanceProfiler(const String &name, const String &category = "Performance")
                : m_name(name), m_category(category)
            {
                m_startTime = std::chrono::high_resolution_clock::now();
            }

            ~PerformanceProfiler()
            {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);

                NORVES_LOG_INFO_F(m_category, "Profile [%s]: %lld μs (%.3f ms)",
                                  m_name.c_str(), duration.count(), duration.count() / 1000.0);
            }

        private:
            String m_name;
            String m_category;
            std::chrono::high_resolution_clock::time_point m_startTime;
#else
        public:
            PerformanceProfiler(const String &name, const String &category = "Performance")
            {
                (void)name;
                (void)category;
            }

            ~PerformanceProfiler() = default;
#endif
        };

    private:
        /**
         * @brief 値を文字列に変換するヘルパー
         */
        template <typename T>
        static String ToString(const T &value)
        {
            if constexpr (std::is_arithmetic_v<T>)
            {
                return String(std::to_string(value));
            }
            else if constexpr (std::is_same_v<T, String>)
            {
                return value;
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return String(value);
            }
            else if constexpr (std::is_same_v<T, const char *> || std::is_same_v<T, char *>)
            {
                return String(value);
            }
            else
            {
                return String("[Complex Type]");
            }
        }
    };

} // namespace NorvesLib::Debug

// 便利なマクロ定義

#if NORVES_ENABLE_DEBUG_OUTPUT

/**
 * @brief 変数の値をデバッグ出力するマクロ
 */
#define DEBUG_PRINT_VAR(var) \
    NorvesLib::Debug::DebugOutput::PrintVariable(#var, var)

#define DEBUG_PRINT_VAR_CATEGORY(var, category) \
    NorvesLib::Debug::DebugOutput::PrintVariable(#var, var, category)

/**
 * @brief コンテナの内容をデバッグ出力するマクロ
 */
#define DEBUG_PRINT_CONTAINER(container) \
    NorvesLib::Debug::DebugOutput::PrintContainer(#container, container)

#define DEBUG_PRINT_CONTAINER_CATEGORY(container, category) \
    NorvesLib::Debug::DebugOutput::PrintContainer(#container, container, category)

/**
 * @brief 関数トレース用マクロ
 */
#define DEBUG_FUNCTION_TRACE() \
    NorvesLib::Debug::DebugOutput::FunctionTracer __tracer(__FUNCTION__)

#define DEBUG_FUNCTION_TRACE_CATEGORY(category) \
    NorvesLib::Debug::DebugOutput::FunctionTracer __tracer(__FUNCTION__, category)

/**
 * @brief メモリ使用量ログ出力マクロ
 */
#define DEBUG_LOG_MEMORY() \
    NorvesLib::Debug::DebugOutput::MemoryMonitor::LogMemoryUsage()

#define DEBUG_LOG_MEMORY_AT(location) \
    NorvesLib::Debug::DebugOutput::MemoryMonitor::LogMemoryUsage(location)

/**
 * @brief パフォーマンス測定マクロ
 */
#define DEBUG_PROFILE_SCOPE(name) \
    NorvesLib::Debug::DebugOutput::PerformanceProfiler __profiler(name)

#define DEBUG_PROFILE_SCOPE_CATEGORY(name, category) \
    NorvesLib::Debug::DebugOutput::PerformanceProfiler __profiler(name, category)

#else

#define DEBUG_PRINT_VAR(var) \
    do                       \
    {                        \
    } while (0)

#define DEBUG_PRINT_VAR_CATEGORY(var, category) \
    do                                         \
    {                                          \
    } while (0)

#define DEBUG_PRINT_CONTAINER(container) \
    do                                  \
    {                                   \
    } while (0)

#define DEBUG_PRINT_CONTAINER_CATEGORY(container, category) \
    do                                                     \
    {                                                      \
    } while (0)

#define DEBUG_FUNCTION_TRACE() \
    do                         \
    {                          \
    } while (0)

#define DEBUG_FUNCTION_TRACE_CATEGORY(category) \
    do                                         \
    {                                          \
    } while (0)

#define DEBUG_LOG_MEMORY() \
    do                     \
    {                      \
    } while (0)

#define DEBUG_LOG_MEMORY_AT(location) \
    do                                \
    {                                 \
    } while (0)

#define DEBUG_PROFILE_SCOPE(name) \
    do                            \
    {                             \
    } while (0)

#define DEBUG_PROFILE_SCOPE_CATEGORY(name, category) \
    do                                              \
    {                                               \
    } while (0)

#endif
