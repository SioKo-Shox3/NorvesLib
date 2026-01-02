#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include "GameBoot.h"
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

        std::cout << "Debug console allocated successfully!" << std::endl;
    }
}

// Windowsアプリケーションのエントリーポイント
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 未使用パラメータの警告を抑制
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

#ifdef _DEBUG
    // デバッグビルドではコンソールを先に割り当てる
    AllocateConsoleForDebug();
#endif

    std::wcout << L"Starting application with new boot system..." << std::endl;

    // ロガーシステムの初期化
    NorvesLib::Core::Logging::LogConfig logConfig = NorvesLib::Core::Logging::CreateLogConfig(
        NorvesLib::Core::Logging::LogLevel::Info,
        NorvesLib::Core::Logging::LogOutput::Both,
        NorvesLib::Core::Container::String("Game.log"),
        false); // 非同期ログを無効にする

    if (!NorvesLib::Core::Logging::InitializeLogging(logConfig))
    {
        std::wcerr << L"Failed to initialize logging system" << std::endl;
        return -1;
    }

    LOG_INFO("WinMain called - Starting application with new boot system...");

    // ゲーム固有のBootConfigを取得
    NorvesLib::Core::Boot::BootConfig config = Game::Boot::GetBootConfig();

    // 新しいAPIでアプリケーションを実行
    int result = NorvesLib::Core::Boot::RunApplication(hInstance, config);

    LOG_INFO_F("Application finished with result: %d", result);

    // ロガーシステムの終了処理
    NorvesLib::Core::Logging::ShutdownLogging();

#ifdef _DEBUG
    // デバッグ用の一時停止
    std::wcout << L"Press any key to exit..." << std::endl;
    std::wcout.flush();
    std::cin.get();
#endif

    return result;
}