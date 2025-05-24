#pragma once

#include "Application/IApplication.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <Windows.h>
#include <memory>

namespace NorvesLib
{
    namespace Core
    {
        namespace Platform
        {

            /**
             * @brief Windows向けのアプリケーション実装クラス
             *
             * IApplicationインターフェースを実装し、Windows環境での
             * アプリケーションライフサイクルを管理します。
             */
            class WindowsApplication : public NorvesLib::IApplication
            {
            public:
                /**
                 * @brief コンストラクタ
                 */
                WindowsApplication();

                /**
                 * @brief デストラクタ
                 */
                virtual ~WindowsApplication() override;

                // IApplicationインターフェースの実装
                virtual bool Initialize(const Container::VariableArray<Container::String> &args) override;
                virtual int Run() override;
                virtual void Shutdown() override;
                virtual NorvesLib::IWindow *GetMainWindow() override;
                virtual void RegisterWindow(std::shared_ptr<NorvesLib::IWindow> window) override;
                virtual void UnregisterWindow(std::shared_ptr<NorvesLib::IWindow> window) override;

                /**
                 * @brief HINSTANCEの取得
                 * @return アプリケーションのインスタンスハンドル
                 */
                HINSTANCE GetInstance() const;

                /**
                 * @brief コマンドライン引数の取得
                 * @return コマンドライン引数の配列
                 */
                const Container::VariableArray<Container::String> &GetCommandLineArgs() const;

            private:
                /**
                 * @brief Windowsメッセージループの処理
                 * @return 終了コード
                 */
                int ProcessWindowsMessages();

            private:
                HINSTANCE m_hInstance;                                                   // アプリケーションインスタンスハンドル
                Container::VariableArray<std::shared_ptr<NorvesLib::IWindow>> m_windows; // ウィンドウリスト
                std::shared_ptr<NorvesLib::IWindow> m_mainWindow;                        // メインウィンドウ
                bool m_isRunning;                                                        // 実行中フラグ
                Container::VariableArray<Container::String> m_args;                      // コマンドライン引数
            };

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib