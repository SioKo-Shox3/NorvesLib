#include "Boot/PlatformBoot.h"
#include "Boot/BootConfig.h"
#include "Application/ApplicationFactory.h"
#include "Engine/ApplicationProcessor.h"
#include "Core/Public/Container/Containers.h"
#include <iostream>

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

            bool InitializePlatform(const BootConfig &config)
            {
#ifdef _WIN32
                // コンソールの割り当て（デバッグビルドのみ、または設定による）
                if (config.bEnableDebugConsole)
                {
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#endif

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib