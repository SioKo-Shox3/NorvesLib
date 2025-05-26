#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Container/PointerTypes.h"
#include <Windows.h>

namespace NorvesLib {

// 前方宣言
class IApplication;

namespace Core {
namespace Boot {

using namespace NorvesLib::Core::Container;

/**
 * @brief プラットフォーム固有の起動処理を提供する関数群
 *
 * 各プラットフォーム（Windows, Mac, Linux等）での
 * アプリケーション起動処理を統一的に扱うための関数を提供します。
 */

/**
 * @brief プラットフォーム初期化を行う
 * @param hInstance インスタンスハンドル
 * @param commandLine コマンドライン引数
 * @return 成功した場合true
 */
bool InitializePlatform(HINSTANCE hInstance, const String& commandLine);

/**
 * @brief プラットフォーム終了処理を行う
 */
void ShutdownPlatform();

/**
 * @brief デフォルトアプリケーションを作成する
 * @return 作成されたアプリケーションのインスタンス
 */
TUniquePtr<IApplication> CreateDefaultApplication();

/**
 * @brief Windowsメッセージを処理する
 * @return 処理されたメッセージ数
 */
int ProcessWindowsMessages();

/**
 * @brief アプリケーションを実行する統一エントリーポイント
 * @param hInstance アプリケーションインスタンスハンドル
 * @param hPrevInstance 前のインスタンスハンドル（現在は常にNULL）
 * @param lpCmdLine コマンドライン引数
 * @param nCmdShow ウィンドウの表示状態
 * @return アプリケーションの終了コード
 */
int RunApplication(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

} // namespace Boot
} // namespace Core
} // namespace NorvesLib