#pragma once

#include <memory>
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
                static std::unique_ptr<IApplication> CreateDefaultApplication();

                /**
                 * @brief カスタムアプリケーションを生成する
                 * @return カスタムアプリケーションのインスタンス
                 */
                static std::unique_ptr<IApplication> CreateCustomApplication();

            private:
                // プラットフォーム固有の実装を取得する
                static std::unique_ptr<IApplication> GetPlatformSpecificImplementation();
            };

        } // namespace Boot
    } // namespace Core

    // ゲームから実装される必要がある関数
    std::unique_ptr<IApplication> CreateApplication();

} // namespace NorvesLib