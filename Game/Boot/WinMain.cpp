#include <windows.h>
#include "GameBoot.h"
#include "Library/Core/Public/Boot/PlatformBoot.h"

/**
 * @brief Windowsアプリケーションのエントリーポイント
 *
 * ライブラリのPlatformBoot::Boot()を呼び出すだけのシンプルな実装。
 * ロガー初期化、コンソール割り当て、アプリケーション実行、終了処理は
 * 全てライブラリ側で行われます。
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // ゲーム固有のBootConfigを取得し、WinMain引数と共に起動
    return NorvesLib::Core::Boot::Boot(hInstance, hPrevInstance, lpCmdLine, nCmdShow, Game::Boot::GetBootConfig());
}