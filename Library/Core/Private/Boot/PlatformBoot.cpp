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
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <Shellapi.h>
#pragma comment(lib, "Shell32.lib")
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
                std::string TraceFilePath = "NorvesLib.trace.csv";
            };

            bool StartsWith(const std::string &value, const std::string &prefix)
            {
                return value.rfind(prefix, 0) == 0;
            }

            TraceLaunchOptions ParseTraceLaunchOptions(const BootConfig &config)
            {
                TraceLaunchOptions options;

#ifdef _WIN32
                std::string commandLine = config.lpCmdLine ? std::string(config.lpCmdLine) : std::string{};
#else
                std::string commandLine;
                (void)config;
#endif

                std::istringstream stream(commandLine);
                std::vector<std::string> args;
                std::string token;
                while (stream >> token)
                {
                    args.push_back(token);
                }

                for (size_t i = 0; i < args.size(); ++i)
                {
                    const std::string &arg = args[i];
                    if (arg == "--trace" || arg == "-trace" || arg == "/trace")
                    {
                        options.bEnableTrace = true;
                    }
                    else if (StartsWith(arg, "--trace-file="))
                    {
                        options.bEnableTrace = true;
                        options.TraceFilePath = arg.substr(std::string("--trace-file=").size());
                    }
                    else if (StartsWith(arg, "-traceFile="))
                    {
                        options.bEnableTrace = true;
                        options.TraceFilePath = arg.substr(std::string("-traceFile=").size());
                    }
                    else if ((arg == "--trace-file" || arg == "-traceFile") && i + 1 < args.size())
                    {
                        options.bEnableTrace = true;
                        options.TraceFilePath = args[++i];
                    }
                }

                if (options.TraceFilePath.empty())
                {
                    options.TraceFilePath = "NorvesLib.trace.csv";
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

#ifdef _WIN32
            bool PlatformInitialize(const Container::String &commandLine)
            {
                (void)commandLine;

                // コンソールの割り当て（デバッグビルドのみ）
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

                // 実行ファイルのディレクトリを作業ディレクトリに設定
                Container::String execPath = GetExecutablePath();
                Container::String execDir = execPath.Substring(0, execPath.FindLast(TEXT("\\")));
                if (!SetWorkingDirectory(execDir))
                {
                    std::wcerr << L"Failed to set working directory to: " << execDir.c_str() << std::endl;
                    return false;
                }

                return true;
            }

            bool InitializePlatform(HINSTANCE hInstance, const Container::String &commandLine)
            {
                (void)hInstance;
                return PlatformInitialize(commandLine);
            }
#endif

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

#ifdef _WIN32
            int ProcessWindowsMessages()
            {
                MSG msg = {};
                int messageCount = 0;

                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    ++messageCount;

                    if (msg.message == WM_QUIT)
                    {
                        break;
                    }

                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                return messageCount;
            }
#endif

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

#ifdef _WIN32
            int RunApplication(HINSTANCE hInstance, const BootConfig &config)
            {
                (void)hInstance;
                return RunApplication(config);
            }

            // レガシーAPI（後方互換性のため維持）
            int RunApplication(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
            {
                // 未使用パラメータの警告を抑制
                (void)hInstance;
                (void)hPrevInstance;
                (void)lpCmdLine;
                (void)nCmdShow;

                // レガシーAPIでは旧来の実装を使用
                // 新しいBootConfigベースのAPIへの移行を推奨

                Container::String commandLine;
                if (!PlatformInitialize(commandLine))
                {
                    return -1;
                }

                // コマンドライン引数をContainer::String型の配列に変換
                Container::VariableArray<Container::String> args;
                int argc = 0;
                LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

                if (argv != nullptr)
                {
                    for (int i = 0; i < argc; ++i)
                    {
                        args.emplace_back(reinterpret_cast<const TCHAR *>(argv[i]));
                    }
                    LocalFree(argv);
                }

                // アプリケーション作成と実行
                auto app = CreateDefaultApplication();
                if (!app)
                {
                    ShutdownPlatform();
                    return -1;
                }

                // 初期化
                if (!app->Initialize(args))
                {
                    ShutdownPlatform();
                    return -1;
                }

                // 実行
                int exitCode = app->Run();

                // 終了
                app->Shutdown();

                // プラットフォーム終了処理
                ShutdownPlatform();

                return exitCode;
            }

            int Boot(const BootConfig &config)
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
                    !Debug::StatsManager::Get().StartTrace(Container::String(traceOptions.TraceFilePath.c_str())))
                {
                    NORVES_LOG_WARNING("Trace", "Failed to open trace file: %s", traceOptions.TraceFilePath.c_str());
                }
#endif

                LOG_INFO("PlatformBoot::Boot() - Starting application...");

                // アプリケーション実行
                int result = RunApplication(config);

#if NORVES_ENABLE_STATS
                Debug::StatsManager::Get().StopTrace();
#endif

                LOG_INFO("PlatformBoot::Boot() - Application finished");

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

            int Boot(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow, BootConfig config)
            {
                // WinMain引数をBootConfigに設定
                config.hInstance = hInstance;
                config.hPrevInstance = hPrevInstance;
                config.lpCmdLine = lpCmdLine;
                config.nCmdShow = nCmdShow;

                // 通常のBootを呼び出し
                return Boot(config);
            }
#endif

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib
