#pragma once

#include <string>
#include <vector>
#include <memory>

namespace NorvesLib {
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
bool PlatformInitialize(const std::wstring& commandLine);

/**
 * @brief プラットフォーム固有の終了処理を行う
 */
void PlatformShutdown();

/**
 * @brief コマンドライン引数をパースする
 * @param commandLine 生のコマンドライン文字列
 * @return パースされたコマンドライン引数の配列
 */
std::vector<std::wstring> ParseCommandLine(const std::wstring& commandLine);

/**
 * @brief 実行可能ファイルのパスを取得する
 * @return 実行可能ファイルの完全パス
 */
std::wstring GetExecutablePath();

/**
 * @brief アプリケーションの作業ディレクトリを設定する
 * @param path 設定するディレクトリパス
 * @return 設定の成否
 */
bool SetWorkingDirectory(const std::wstring& path);

} // namespace Boot
} // namespace Core
} // namespace NorvesLib