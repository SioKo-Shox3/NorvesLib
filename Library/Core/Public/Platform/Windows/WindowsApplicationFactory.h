#pragma once

#include <memory>
#include "Application/IApplication.h" // IApplicationの完全な定義をインクルード
#include "Application/IWindow.h"      // IWindowの完全な定義をインクルード

namespace NorvesLib
{

    namespace Core
    {
        namespace Platform
        {

            /**
             * @brief Windows環境向けアプリケーションファクトリークラス
             *
             * Windows固有のアプリケーション実装を生成するファクトリー
             */
            class WindowsApplicationFactory
            {
            public:
                /**
                 * @brief Windows環境向けアプリケーションインスタンスを生成
                 * @return アプリケーションのインスタンス
                 */
                static std::unique_ptr<IApplication> CreateWindowsApplication();

                /**
                 * @brief Windows環境向けウィンドウインスタンスを生成
                 * @return ウィンドウのインスタンス
                 */
                static std::shared_ptr<IWindow> CreateWindowsWindow();
            };

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib