#pragma once

#include "Container/PointerTypes.h"
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
                static Core::Container::TUniquePtr<IApplication> CreateWindowsApplication();

                /**
                 * @brief Windows環境向けウィンドウインスタンスを生成
                 * @return ウィンドウのインスタンス
                 */
                static Core::Container::TSharedPtr<IWindow> CreateWindowsWindow();
            };

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib