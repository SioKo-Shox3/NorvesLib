#pragma once

#include "Container/PointerTypes.h"
#include "Application/IApplication.h" // IApplicationの完全な定義をインクルード

namespace NorvesLib
{

    namespace Core
    {
        namespace Boot
        {

            /**
             * @brief アプリケーションファクトリークラス
             *
             * プラットフォーム固有のアプリケーション実装を抽象化するファクトリークラス
             */
            class ApplicationFactory
            {
            public:
                /**
                 * @brief デフォルトアプリケーションを生成する
                 * @return デフォルトアプリケーションのインスタンス
                 */
                static Core::Container::TUniquePtr<IApplication> CreateDefaultApplication();

                /**
                 * @brief カスタムアプリケーションを生成する
                 * @return カスタムアプリケーションのインスタンス
                 */
                static Core::Container::TUniquePtr<IApplication> CreateCustomApplication();

            private:
                // プラットフォーム固有の実装を取得する
                static Core::Container::TUniquePtr<IApplication> GetPlatformSpecificImplementation();
            };

        } // namespace Boot
    } // namespace Core

    // ゲームから実装される必要がある関数
    Core::Container::TUniquePtr<IApplication> CreateApplication();

} // namespace NorvesLib