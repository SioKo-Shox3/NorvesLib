#pragma once

#include "Application/IWindow.h"
#include "Container/String.h"
#include "Container/PointerTypes.h"
#include <Windows.h>

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
                virtual ~WindowsWindow() override;

                // IWindowインターフェースの実装
                virtual bool Create(const Container::String& title, int width, int height) override;
                virtual void Destroy() override;
                virtual void Show() override;
                virtual void Hide() override;
                virtual void SetTitle(const Container::String& title) override;
                virtual void Resize(int width, int height) override;
                virtual bool IsActive() const override;
                virtual void* GetNativeHandle() const override;

                /**
                 * @brief HWNDを取得
                 * @return ウィンドウハンドル
                 */
                HWND GetHWND() const;

            private:
                /**
                 * @brief ウィンドウプロシージャ
                 */
                static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

                /**
                 * @brief ウィンドウクラスを登録
                 * @return 登録の成否
                 */
                bool RegisterWindowClass();

                /**
                 * @brief ウィンドウクラス名を取得
                 * @return ウィンドウクラス名
                 */
                static const TCHAR* GetWindowClassName();

                /**
                 * @brief ウィンドウサイズを調整
                 * @param style ウィンドウスタイル
                 * @param exStyle 拡張ウィンドウスタイル
                 * @param width ウィンドウ幅（調整される）
                 * @param height ウィンドウ高さ（調整される）
                 */
                static void AdjustWindowSize(DWORD style, DWORD exStyle, int& width, int& height);

            private:
                static bool s_classRegistered;      // ウィンドウクラス登録フラグ
                HWND m_hWnd;                        // ウィンドウハンドル
                HINSTANCE m_hInstance;              // アプリケーションインスタンスハンドル
                bool m_isActive;                    // アクティブ状態フラグ
                Container::String m_title;          // ウィンドウタイトル
                int m_width;                        // ウィンドウ幅
                int m_height;                       // ウィンドウ高さ
            };

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib