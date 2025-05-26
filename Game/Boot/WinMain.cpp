#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include "Library/Core/Public/Boot/PlatformBoot.h"

// コンソールを割り当てる関数
void AllocateConsoleForDebug()
{
    if (AllocConsole())
    {
        // 標準出力をコンソールにリダイレクト
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
        freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
        
        // C++のストリームも同期
        std::ios::sync_with_stdio(true);
        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();
    }
}

// Windowsアプリケーションのエントリーポイント
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // デバッグ用コンソールを割り当て
    AllocateConsoleForDebug();
    
    std::wcout << L"WinMain called - Starting application..." << std::endl;
    
    // PlatformBootの統一エントリーポイントを呼び出すだけ
    int result = NorvesLib::Core::Boot::RunApplication(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    
    std::wcout << L"Application finished with result: " << result << std::endl;
    std::wcout << L"Press any key to continue..." << std::endl;
    
    // 結果を確認するために一時停止
    system("pause");
    
    return result;
}