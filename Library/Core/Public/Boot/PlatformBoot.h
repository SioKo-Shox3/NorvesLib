#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Container/PointerTypes.h"
#include "BootConfig.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace NorvesLib
{

    // 前方宣言
    class IApplication;

    namespace Core
    {
        namespace Boot
        {

            using namespace NorvesLib::Core::Container;

            /**
             * @brief プラットフォーム固有の起動処理を提供する関数群
             *
             * 各プラットフォーム（Windows, Mac, Linux等）での
             * アプリケーション起動処理を統一的に扱うための関数を提供します。
             */

            /**
             * @brief プラットフォーム初期化を行う
             * @param config 起動設定
             * @return 成功した場合true
             */
            bool InitializePlatform(const BootConfig &config);

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
             * @brief アプリケーションを実行する統一エントリーポイント（新API）
             * @param config 起動設定
             * @return アプリケーションの終了コード
             */
            int RunApplication(const BootConfig &config);

#ifdef _WIN32
            /**
             * @brief プラットフォーム初期化を行う（Windows用レガシーAPI）
             * @param hInstance インスタンスハンドル
             * @param commandLine コマンドライン引数
             * @return 成功した場合true
             */
            bool InitializePlatform(HINSTANCE hInstance, const String &commandLine);

            /**
             * @brief Windowsメッセージを処理する
             * @return 処理されたメッセージ数
             */
            int ProcessWindowsMessages();

            /**
             * @brief アプリケーションを実行する統一エントリーポイント（Windows用レガシーAPI）
             * @param hInstance アプリケーションインスタンスハンドル
             * @param hPrevInstance 前のインスタンスハンドル（現在は常にNULL）
             * @param lpCmdLine コマンドライン引数
             * @param nCmdShow ウィンドウの表示状態
             * @return アプリケーションの終了コード
             */
            int RunApplication(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

            /**
             * @brief アプリケーションを実行する統一エントリーポイント（Windows用新API）
             * @param hInstance アプリケーションインスタンスハンドル
             * @param config 起動設定
             * @return アプリケーションの終了コード
             */
            int RunApplication(HINSTANCE hInstance, const BootConfig &config);

            /**
             * @brief アプリケーションを起動する最もシンプルなAPI
             *
             * ロガー初期化、デバッグコンソール割り当て、アプリケーション実行、
             * 終了処理までを全て行います。
             *
             * @param config 起動設定
             * @return アプリケーションの終了コード
             */
            int Boot(const BootConfig &config);

            /**
             * @brief WinMain引数を受け取るオーバーロード
             *
             * WinMain引数をBootConfigに設定してからBootを呼び出します。
             *
             * @param hInstance アプリケーションインスタンスハンドル
             * @param hPrevInstance 前のインスタンスハンドル
             * @param lpCmdLine コマンドライン引数
             * @param nCmdShow ウィンドウ表示状態
             * @param config 起動設定（コピーされ、WinMain引数が設定されます）
             * @return アプリケーションの終了コード
             */
            int Boot(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow, BootConfig config);
#endif

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib
