#include <windows.h>
#include "Library/Core/Public/Boot/PlatformBoot.h"

// Windowsアプリケーションのエントリーポイント
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // PlatformBootの統一エントリーポイントを呼び出すだけ
    return NorvesLib::Core::Boot::RunApplication(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}