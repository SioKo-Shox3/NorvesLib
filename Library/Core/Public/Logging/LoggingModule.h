#pragma once

/**
 * @file LoggingModule.h
 * @brief ログシステムのメインヘッダーファイル
 *
 * このファイルをインクルードすることで、NorvesLibの
 * ログシステムのすべての機能にアクセスできます。
 */

#include "LogTypes.h"
#include "Logger.h"
#include "LogMacros.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Logging
{
    using namespace NorvesLib::Core::Container;
    /**
     * @brief ログシステムを初期化する便利関数
     * @param config ログ設定（デフォルト設定を使用する場合は省略可能）
     * @return 初期化に成功した場合true
     */
    inline bool InitializeLogging(const LogConfig &config = LogConfig{})
    {
        return Logger::GetInstance().Initialize(config);
    }

    /**
     * @brief ログシステムを終了する便利関数
     */
    inline void ShutdownLogging()
    {
        Logger::GetInstance().Shutdown();
    }

    /**
     * @brief ログレベルを設定する便利関数
     * @param level 最小ログレベル
     */
    inline void SetLogLevel(LogLevel level)
    {
        LogConfig config = Logger::GetInstance().GetConfig();
        config.minLevel = level;
        Logger::GetInstance().UpdateConfig(config);
    }

    /**
     * @brief ログフォーマッターを設定する便利関数
     * @param useJsonFormat JSON形式を使用する場合true
     */
    inline void SetLogFormat(bool useJsonFormat = false)
    {
        if (useJsonFormat)
        {
            Logger::GetInstance().SetFormatter(MakeShared<JsonLogFormatter>());
        }
        else
        {
            Logger::GetInstance().SetFormatter(MakeShared<StandardLogFormatter>());
        }
    }

    /**
     * @brief カスタムログ設定を作成するヘルパー関数
     */
    inline LogConfig CreateLogConfig(LogLevel minLevel = LogLevel::Info,
                                     LogOutput outputType = LogOutput::Both,
                                     const String &logFilePath = String("NorvesLib.log"),
                                     bool asyncLogging = true)
    {
        LogConfig config;
        config.minLevel = minLevel;
        config.outputType = outputType;
        config.logFilePath = logFilePath;
        config.bAsyncLogging = asyncLogging;
        return config;
    }

} // namespace NorvesLib::Core::Logging

// より簡単にアクセスするためのエイリアス
namespace NorvesLib
{
    using LogLevel = Core::Logging::LogLevel;
    using LogConfig = Core::Logging::LogConfig;
    using LogOutput = Core::Logging::LogOutput;

    // 便利な初期化・終了関数
    using Core::Logging::CreateLogConfig;
    using Core::Logging::InitializeLogging;
    using Core::Logging::SetLogFormat;
    using Core::Logging::SetLogLevel;
    using Core::Logging::ShutdownLogging;
}

/**
 * @brief ログシステムの使用例
 *
 * @code
 * // 初期化
 * NorvesLib::InitializeLogging();
 *
 * // 基本的なログ出力
 * LOG_INFO("Application started");
 * LOG_ERROR("Something went wrong");
 *
 * // フォーマット付きログ出力
 * // printf 形式のフォーマット指定子を使用
 * LOG_INFO_F("User %s logged in with ID %d", userName, userId);
 *
 * // カテゴリ付きログ出力
 * NORVES_LOG_DEBUG("Graphics", "Rendering frame");
 *
 * // 条件付きログ出力
 * LOG_ERROR_IF(errorCode != 0, "Operation failed");
 *
 * // パフォーマンス測定
 * NORVES_LOG_PERFORMANCE_START(rendering);
 * // ... 測定したい処理 ...
 * NORVES_LOG_PERFORMANCE_END(rendering, "Graphics");
 *
 * // 終了処理
 * NorvesLib::ShutdownLogging();
 * @endcode
 */
