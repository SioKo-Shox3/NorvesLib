#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Application/ApplicationFactory.h"
#include "Engine/ApplicationProcessor.h"
#include "Debug/DebugConfig.h"
#include "Debug/Stats.h"
#include "Logging/LoggingModule.h"
#include "Logging/LogMacros.h"
#include "Platform/PlatformProcess.h"
#include "Platform/PlatformConsole.h"
#include "Container/Containers.h"

namespace
{

#if NORVES_ENABLE_STATS
    struct TraceLaunchOptions
    {
        bool bEnableTrace = false;
        NorvesLib::Core::Container::String TraceFilePath = TEXT("NorvesLib.trace.csv");
    };

    bool StartsWithStr(const NorvesLib::Core::Container::String& value, const TCHAR* prefix)
    {
        const NorvesLib::Core::Container::String prefixStr(prefix);
        if (value.size() < prefixStr.size())
        {
            return false;
        }
        return value.substr(0, prefixStr.size()).compare(prefixStr) == 0;
    }

    TraceLaunchOptions ParseTraceLaunchOptions(const NorvesLib::Core::Boot::BootConfig& config)
    {
        TraceLaunchOptions options;

        const auto& args = config.Arguments;
        for (size_t i = 0; i < args.size(); ++i)
        {
            const NorvesLib::Core::Container::String& arg = args[i];
            if (arg == TEXT("--trace") || arg == TEXT("-trace") || arg == TEXT("/trace"))
            {
                options.bEnableTrace = true;
            }
            else if (StartsWithStr(arg, TEXT("--trace-file=")))
            {
                options.bEnableTrace = true;
                const size_t prefixLen = NorvesLib::Core::Container::String(TEXT("--trace-file=")).size();
                options.TraceFilePath = NorvesLib::Core::Container::String(arg.c_str() + prefixLen);
            }
            else if (StartsWithStr(arg, TEXT("-traceFile=")))
            {
                options.bEnableTrace = true;
                const size_t prefixLen = NorvesLib::Core::Container::String(TEXT("-traceFile=")).size();
                options.TraceFilePath = NorvesLib::Core::Container::String(arg.c_str() + prefixLen);
            }
            else if ((arg == TEXT("--trace-file") || arg == TEXT("-traceFile")) && i + 1 < args.size())
            {
                options.bEnableTrace = true;
                options.TraceFilePath = args[++i];
            }
        }

        if (options.TraceFilePath.empty())
        {
            options.TraceFilePath = TEXT("NorvesLib.trace.csv");
        }

        return options;
    }
#endif

    /**
     * @brief プラットフォーム初期化（内部ヘルパ）
     *
     * 実行ファイルディレクトリを作業ディレクトリに設定する。
     * コンソール割当は LaunchApplication 冒頭で行うため、ここでは行わない。
     *
     * @param config 起動設定
     * @return 成功した場合 true
     */
    bool InitializePlatform(const NorvesLib::Core::Boot::BootConfig& config)
    {
        (void)config;

        // 実行ファイルのディレクトリを作業ディレクトリに設定
        NorvesLib::Core::Container::String execDir = NorvesLib::Core::Platform::GetExecutableDirectory();
        if (execDir.empty())
        {
            NORVES_LOG_WARNING("AppLauncher", "Failed to get executable directory. Working directory not changed.");
        }
        else
        {
            if (!NorvesLib::Core::Platform::SetWorkingDirectory(execDir))
            {
                NORVES_LOG_ERROR("AppLauncher", "Failed to set working directory to: %s", execDir.c_str());
                return false;
            }
        }

        return true;
    }

    /**
     * @brief プラットフォーム終了処理（内部ヘルパ）
     *
     * @param config 起動設定
     */
    void ShutdownPlatform(const NorvesLib::Core::Boot::BootConfig& config)
    {
        if (config.bEnableDebugConsole)
        {
            NorvesLib::Core::Platform::CloseDebugConsole();
        }
    }

} // namespace

namespace NorvesLib::Core::Boot
{

    Container::TUniquePtr<IApplication> CreateDefaultApplication()
    {
        // ApplicationFactory を使用してプラットフォーム固有の実装を取得
        return ApplicationFactory::CreateDefaultApplication();
    }

    int RunApplication(const BootConfig& config)
    {
        // プラットフォーム初期化（作業ディレクトリ設定）
        if (!InitializePlatform(config))
        {
            return -1;
        }

        // ApplicationProcessor を使用
        auto& processor = Engine::ApplicationProcessor::GetInstance();

        if (!processor.Initialize(config))
        {
            ShutdownPlatform(config);
            return -1;
        }

        int exitCode = processor.Run();

        processor.Shutdown();

        ShutdownPlatform(config);

        return exitCode;
    }

    int LaunchApplication(const BootConfig& config)
    {
        // コンソール割当（ロガー初期化より前に行う必要があるため LaunchApplication 冒頭で実施）
        if (config.bEnableDebugConsole)
        {
            Platform::OpenDebugConsole();
        }

        // ロガー初期化
        Container::String logFileName = config.LogFileName.empty()
                                            ? Container::String(TEXT("Game.log"))
                                            : config.LogFileName;

        Logging::LogConfig logConfig = Logging::CreateLogConfig(
            Logging::LogLevel::Trace,
            Logging::LogOutput::Both,
            logFileName,
            false);

        if (!Logging::InitializeLogging(logConfig))
        {
            return -1;
        }

#if NORVES_ENABLE_STATS
        TraceLaunchOptions traceOptions = ParseTraceLaunchOptions(config);
        if (traceOptions.bEnableTrace &&
            !Debug::StatsManager::Get().StartTrace(traceOptions.TraceFilePath))
        {
            NORVES_LOG_WARNING("Trace", "Failed to open trace file: %s", traceOptions.TraceFilePath.c_str());
        }
#endif

        LOG_INFO("AppLauncher::LaunchApplication() - Starting application...");

        // アプリケーション実行
        int result = RunApplication(config);

#if NORVES_ENABLE_STATS
        Debug::StatsManager::Get().StopTrace();
#endif

        LOG_INFO("AppLauncher::LaunchApplication() - Application finished");

        // ロガー終了
        Logging::ShutdownLogging();

        return result;
    }

} // namespace NorvesLib::Core::Boot
