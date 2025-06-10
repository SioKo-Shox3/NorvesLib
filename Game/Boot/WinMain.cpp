#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include "Library/Core/Public/Boot/PlatformBoot.h"
#include "Library/Core/Public/Logging/LoggingModule.h"

// コンソールを割り当てる関数
void AllocateConsoleForDebug()
{
    if (AllocConsole())
    {
        // 標準出力をコンソールにリダイレクト
        freopen_s((FILE **)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE **)stderr, "CONOUT$", "w", stderr);
        freopen_s((FILE **)stdin, "CONIN$", "r", stdin);

        // C++のストリームも同期
        std::ios::sync_with_stdio(true);
        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();
        // コンソールタイトルを設定
        SetConsoleTitleA("NorvesLib Debug Console");

        // 即座にテスト出力
        printf("Debug console allocated successfully!\n");
        fflush(stdout);
        std::cout << "C++ streams test output" << std::endl;
        std::cout.flush();
    }
    else
    {
        // Alloc失敗の場合、既存のコンソールに出力を試みる
        printf("Failed to allocate console\n");
        fflush(stdout);
    }
}

// Windowsアプリケーションのエントリーポイント
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // **問題の特定のため、最初にメッセージボックスを追加**
    MessageBoxA(NULL, "WinMain called - starting debug", "Debug", MB_OK);

    // デバッグコンソールを先に割り当てる
    AllocateConsoleForDebug();

    std::wcout << L"Starting application..." << std::endl;

    // **問題の特定のため、ログ初期化前にメッセージボックスを追加**
    MessageBoxA(NULL, "About to initialize logging system", "Debug", MB_OK);

    // ロガーシステムの初期化（同期モードでテスト）
    NorvesLib::Core::Logging::LogConfig logConfig = NorvesLib::Core::Logging::CreateLogConfig(
        NorvesLib::Core::Logging::LogLevel::Info,
        NorvesLib::Core::Logging::LogOutput::Both,
        NorvesLib::Core::Container::String("Game.log"),
        false); // 非同期ログを無効にする

    std::wcout << L"Initializing logging system..." << std::endl;

    if (!NorvesLib::Core::Logging::InitializeLogging(logConfig))
    {
        std::wcout << L"Failed to initialize logging system" << std::endl;
        MessageBoxA(NULL, "Failed to initialize logging system", "Error", MB_OK);
        return -1;
    }

    // **問題の特定のため、ログ初期化後にメッセージボックスを追加**
    MessageBoxA(NULL, "Logging system initialized successfully", "Debug", MB_OK);

    std::wcout << L"Logging system initialized successfully" << std::endl;
    LOG_INFO("WinMain called - Starting application...");

    // ログが確実に出力されるように明示的にフラッシュ
    NorvesLib::Core::Logging::Logger::GetInstance().Flush();

    std::wcout << L"About to call RunApplication..." << std::endl;
    std::wcout.flush();

    // PlatformBootの統一エントリーポイントを呼び出すだけ
    int result = NorvesLib::Core::Boot::RunApplication(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    std::wcout << L"RunApplication returned: " << result << std::endl;
    std::wcout.flush();

    LOG_INFO_F("Application finished with result: %d", result);

    // ログが確実に出力されるように明示的にフラッシュ
    NorvesLib::Core::Logging::Logger::GetInstance().Flush();

    // ロガーシステムの終了処理
    NorvesLib::Core::Logging::ShutdownLogging();

    // デバッグ用の一時停止
    std::wcout << L"Press any key to exit..." << std::endl;
    std::wcout.flush();
    std::cin.get();

    return result;
}