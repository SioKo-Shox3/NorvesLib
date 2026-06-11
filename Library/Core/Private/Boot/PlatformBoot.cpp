#include "Boot/PlatformBoot.h"
#include "Boot/BootConfig.h"
#include "Application/ApplicationFactory.h"
#include "Engine/ApplicationProcessor.h"
#include "Debug/DebugConfig.h"
#include "Debug/Stats.h"
#include "Logging/LoggingModule.h"
#include "Logging/LogMacros.h"
#include "Container/Containers.h"
#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace NorvesLib
{
    namespace Core
    {
        namespace Boot
        {
            // 前方宣言
            Container::String GetExecutablePath();
            bool SetWorkingDirectory(const Container::String &path);

#if NORVES_ENABLE_STATS
            struct TraceLaunchOptions
            {
                bool bEnableTrace = false;
                Container::String TraceFilePath = TEXT("NorvesLib.trace.csv");
            };

            bool StartsWithStr(const Container::String &value, const TCHAR *prefix)
            {
                const Container::String prefixStr(prefix);
                if (value.size() < prefixStr.size())
                {
                    return false;
                }
                return value.substr(0, prefixStr.size()).compare(prefixStr) == 0;
            }

            TraceLaunchOptions ParseTraceLaunchOptions(const BootConfig &config)
            {
                TraceLaunchOptions options;

                const auto &args = config.Arguments;
                for (size_t i = 0; i < args.size(); ++i)
                {
                    const Container::String &arg = args[i];
                    if (arg == TEXT("--trace") || arg == TEXT("-trace") || arg == TEXT("/trace"))
                    {
                        options.bEnableTrace = true;
                    }
                    else if (StartsWithStr(arg, TEXT("--trace-file=")))
                    {
                        options.bEnableTrace = true;
                        const size_t prefixLen = Container::String(TEXT("--trace-file=")).size();
                        options.TraceFilePath = Container::String(arg.c_str() + prefixLen);
                    }
                    else if (StartsWithStr(arg, TEXT("-traceFile=")))
                    {
                        options.bEnableTrace = true;
                        const size_t prefixLen = Container::String(TEXT("-traceFile=")).size();
                        options.TraceFilePath = Container::String(arg.c_str() + prefixLen);
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

            bool InitializePlatform(const BootConfig &config)
            {
#ifdef _WIN32
                // コンソールの割り当て（デバッグビルドのみ、または設定による）
                if (config.bEnableDebugConsole)
                {
#if NORVES_ENABLE_DEBUG_OUTPUT
                    if (!AttachConsole(ATTACH_PARENT_PROCESS))
                    {
                        AllocConsole();
                        FILE *fp;
                        freopen_s(&fp, "CONOUT$", "w", stdout);
                        freopen_s(&fp, "CONOUT$", "w", stderr);
                        freopen_s(&fp, "CONIN$", "r", stdin);
                    }
#endif
                }

                // 実行ファイルのディレクトリを作業ディレクトリに設定
                Container::String execPath = GetExecutablePath();
                Container::String execDir = execPath.Substring(0, execPath.FindLast(TEXT("\\")));
                if (!SetWorkingDirectory(execDir))
                {
                    std::wcerr << L"Failed to set working directory to: " << execDir.c_str() << std::endl;
                    return false;
                }
#else
                (void)config;
#endif
                return true;
            }


            void ShutdownPlatform()
            {
#ifdef _WIN32
                // コンソールのクリーンアップ（デバッグビルドのみ）
#if NORVES_ENABLE_DEBUG_OUTPUT
                FreeConsole();
#endif
#endif
                // その他のプラットフォーム固有のクリーンアップ処理
            }

            Container::String GetExecutablePath()
            {
#ifdef _WIN32
                TCHAR buffer[MAX_PATH];
                GetModuleFileName(NULL, buffer, MAX_PATH);
                return Container::String(buffer);
#else
                return Container::String();
#endif
            }

            bool SetWorkingDirectory(const Container::String &path)
            {
#ifdef _WIN32
                return SetCurrentDirectory(path.c_str()) != 0;
#else
                (void)path;
                return false;
#endif
            }

            Container::TUniquePtr<IApplication> CreateDefaultApplication()
            {
                // ApplicationFactoryを使用してプラットフォーム固有の実装を取得
                return Core::Boot::ApplicationFactory::CreateDefaultApplication();
            }

            // 新しいAPIを使用したRunApplication
            int RunApplication(const BootConfig &config)
            {
                // プラットフォーム初期化
                if (!InitializePlatform(config))
                {
                    return -1;
                }

                // ApplicationProcessorを使用
                auto &processor = Engine::ApplicationProcessor::GetInstance();

                if (!processor.Initialize(config))
                {
                    ShutdownPlatform();
                    return -1;
                }

                int exitCode = processor.Run();

                processor.Shutdown();

                ShutdownPlatform();

                return exitCode;
            }

            int LaunchApplication(const BootConfig &config)
            {
                // デバッグコンソールの割り当て
                if (config.bEnableDebugConsole)
                {
#if NORVES_ENABLE_DEBUG_OUTPUT
                    if (AllocConsole())
                    {
                        FILE *fp;
                        freopen_s(&fp, "CONOUT$", "w", stdout);
                        freopen_s(&fp, "CONOUT$", "w", stderr);
                        freopen_s(&fp, "CONIN$", "r", stdin);
                        std::ios::sync_with_stdio(true);
                        SetConsoleTitleA("NorvesLib Debug Console");
                    }
#endif
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
                    std::wcerr << L"Failed to initialize logging system" << std::endl;
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

                LOG_INFO("PlatformBoot::LaunchApplication() - Starting application...");

                // アプリケーション実行
                int result = RunApplication(config);

#if NORVES_ENABLE_STATS
                Debug::StatsManager::Get().StopTrace();
#endif

                LOG_INFO("PlatformBoot::LaunchApplication() - Application finished");

                // ロガー終了
                Logging::ShutdownLogging();

#if NORVES_ENABLE_DEBUG_OUTPUT
                if (config.bEnableDebugConsole)
                {
                    std::wcout << L"Press any key to exit..." << std::endl;
                    std::wcout.flush();
                    std::cin.get();
                }
#endif

                return result;
            }

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib
