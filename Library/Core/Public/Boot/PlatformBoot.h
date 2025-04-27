#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include <memory>
#include <Windows.h>

namespace NorvesLib {

// 前方宣言
class IApplication;

namespace Core {
namespace Boot {

/**
 * @brief プラットフォーム固有の起動処理を提供する関数群
 *
 * 各プラットフォーム（Windows, Mac, Linux等）での
 * アプリケーション起動処理を統一的に扱うための関数を提供します。
 */

/**
 * @brief プラットフォーム初期化を行う
 * @param commandLine コマンドライン引数
 * @return 初期化の成否
 */
bool PlatformInitialize(const Container::String& commandLine);

/**
 * @brief プラットフォーム固有の終了処理を行う
 */
void PlatformShutdown();

/**
 * @brief コマンドライン引数をパースする
 * @param commandLine 生のコマンドライン文字列
 * @return パースされたコマンドライン引数の配列
 */
Container::VariableArray<Container::String> ParseCommandLine(const Container::String& commandLine);

/**
 * @brief 実行可能ファイルのパスを取得する
 * @return 実行可能ファイルの完全パス
 */
Container::String GetExecutablePath();

/**
 * @brief アプリケーションの作業ディレクトリを設定する
 * @param path 設定するディレクトリパス
 * @return 設定の成否
 */
bool SetWorkingDirectory(const Container::String& path);

/**
 * @brief 標準アプリケーションの作成
 * @return 作成されたアプリケーションインスタンス
 */
std::unique_ptr<IApplication> CreateDefaultApplication();

/**
 * @brief アプリケーションを実行する統一エントリーポイント
 * @param args コマンドライン引数の配列
 * @return アプリケーションの終了コード
 */
int RunApplication(const Container::VariableArray<Container::String>& args);

/**
 * @brief WindowsのWinMainから呼び出す統一エントリーポイント
 * @param hInstance アプリケーションインスタンスハンドル
 * @param hPrevInstance 未使用（互換性のため）
 * @param lpCmdLine コマンドライン引数
 * @param nCmdShow ウィンドウの表示状態
 * @return アプリケーションの終了コード
 */
int RunApplication(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

} // namespace Boot
} // namespace Core
} // namespace NorvesLib