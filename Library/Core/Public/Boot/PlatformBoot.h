#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Container/PointerTypes.h"
#include "BootConfig.h"

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
             * @brief アプリケーションを実行する統一エントリーポイント
             * @param config 起動設定
             * @return アプリケーションの終了コード
             */
            int RunApplication(const BootConfig &config);

            /**
             * @brief アプリケーションを起動する統合エントリポイント
             *
             * コンソール割当・ロガー初期化・アプリケーション実行・終了処理を
             * 一括で行います。プラットフォーム固有のエントリポイント
             * （WindowsEntryPoint の WinMain 等）から呼び出されます。
             *
             * @param config 起動設定（Arguments フィールドには呼び出し前に引数を設定すること）
             * @return アプリケーションの終了コード
             */
            int LaunchApplication(const BootConfig &config);

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib
