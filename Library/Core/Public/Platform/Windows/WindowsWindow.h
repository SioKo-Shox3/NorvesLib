#pragma once

#include "Application/IWindow.h"
#include "Container/String.h"
#include <Windows.h>
#include <memory>

namespace NorvesLib
{
    namespace Core
    {
        namespace Platform
        {

            /**
             * @brief Windows向けウィンドウの実装クラス
             *
             * IWindowインターフェースを実装し、Windows APIを使用した
             * ウィンドウの作成と管理を行います。
             */
            class WindowsWindow : public NorvesLib::IWindow
            {
            public:
                /**
                 * @brief コンストラクタ
                 */
                WindowsWindow();

                /**
                 * @brief デストラクタ
                 */
                virtual ~WindowsWindow();

                /**
                 * @brief ウィンドウクラス名の取得
                 * @return Windowsウィンドウクラス名
                 */
                static const TCHAR *GetWindowClassName();

                /**
                 * @brief ウィンドウメッセージプロシージャ
                 * @param hWnd ウィンドウハンドル
                 * @param message メッセージID
                 * @param wParam 追加のメッセージ情報（依存）
                 * @param lParam 追加のメッセージ情報（依存）
                 * @return メッセージ処理結果
                 */
                static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

                // IWindowインターフェースの実装
                virtual bool Create(const Container::String &title, int width, int height) override;
                virtual void Destroy() override;
                virtual void Show() override;
                virtual void Hide() override;
                virtual void SetTitle(const Container::String &title) override;
                virtual void Resize(int width, int height) override;
                virtual bool IsActive() const override;
                virtual void *GetNativeHandle() const override;

            private:
                /**
                 * @brief ウィンドウクラスの登録
                 * @return 登録の成否
                 */
                bool RegisterWindowClass();

                /**
                 * @brief ウィンドウスタイルとクライアント領域サイズを調整
                 * @param style ウィンドウスタイル
                 * @param exStyle 拡張ウィンドウスタイル
                 * @param width クライアント領域の幅（入出力）
                 * @param height クライアント領域の高さ（入出力）
                 */
                void AdjustWindowSize(DWORD style, DWORD exStyle, int &width, int &height);

            private:
                HWND m_hWnd;                   // ウィンドウハンドル
                HINSTANCE m_hInstance;         // アプリケーションインスタンスハンドル
                bool m_isActive;               // アクティブ状態フラグ
                Container::String m_title;     // ウィンドウタイトル
                int m_width;                   // ウィンドウ幅
                int m_height;                  // ウィンドウ高さ
                static bool s_classRegistered; // ウィンドウクラス登録状態
            };

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib