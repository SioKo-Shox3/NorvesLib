#include "Platform/Windows/WindowsWindow.h"
#include <stdexcept>

using namespace NorvesLib::Core::Container;

namespace NorvesLib
{
    namespace Core
    {
        namespace Platform
        {

            // 静的変数の初期化
            bool WindowsWindow::s_classRegistered = false;

            WindowsWindow::WindowsWindow()
                : m_hWnd(nullptr), m_hInstance(GetModuleHandle(nullptr)), m_isActive(false), m_title(TEXT("NorvesLib Window")), m_width(800), m_height(600)
            {
            }

            WindowsWindow::~WindowsWindow()
            {
                // ウィンドウが破棄されていない場合は破棄する
                if (m_hWnd)
                {
                    Destroy();
                }
            }

            const TCHAR *WindowsWindow::GetWindowClassName()
            {
                return TEXT("NorvesLibWindowClass");
            }

            LRESULT CALLBACK WindowsWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
            {
                // ウィンドウごとにインスタンスを取得
                WindowsWindow *pThis = nullptr;

                if (message == WM_NCCREATE)
                {
                    // ウィンドウ作成時にインスタンスを設定
                    CREATESTRUCT *createStruct = reinterpret_cast<CREATESTRUCT *>(lParam);
                    pThis = static_cast<WindowsWindow *>(createStruct->lpCreateParams);

                    // ウィンドウにインスタンスを関連付け
                    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
                }
                else
                {
                    // 既に関連付けられたインスタンスを取得
                    pThis = reinterpret_cast<WindowsWindow *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
                }

                // メッセージ処理
                switch (message)
                {
                case WM_ACTIVATE:
                    if (pThis)
                    {
                        pThis->m_isActive = (LOWORD(wParam) != WA_INACTIVE);
                    }
                    break;

                case WM_DESTROY:
                    // ウィンドウの破棄
                    PostQuitMessage(0);
                    return 0;
                }

                // 標準のウィンドウプロシージャを呼び出す
                return DefWindowProc(hWnd, message, wParam, lParam);
            }

            bool WindowsWindow::RegisterWindowClass()
            {
                // ウィンドウクラスが既に登録されている場合は成功とする
                if (s_classRegistered)
                {
                    return true;
                }

                // ウィンドウクラスの設定
                WNDCLASSEX wcex = {};
                wcex.cbSize = sizeof(WNDCLASSEX);
                wcex.style = CS_HREDRAW | CS_VREDRAW;
                wcex.lpfnWndProc = WindowProc;
                wcex.cbClsExtra = 0;
                wcex.cbWndExtra = 0;
                wcex.hInstance = m_hInstance;
                wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
                wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
                wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
                wcex.lpszMenuName = nullptr;
                wcex.lpszClassName = GetWindowClassName();
                wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

                // ウィンドウクラスの登録
                if (!RegisterClassEx(&wcex))
                {
                    // 登録失敗
                    return false;
                }

                // 登録成功
                s_classRegistered = true;
                return true;
            }

            void WindowsWindow::AdjustWindowSize(DWORD style, DWORD exStyle, int &width, int &height)
            {
                // クライアント領域が指定されたサイズになるようにウィンドウサイズを調整
                RECT rect = {0, 0, width, height};
                AdjustWindowRectEx(&rect, style, FALSE, exStyle);
                width = rect.right - rect.left;
                height = rect.bottom - rect.top;
            }

            bool WindowsWindow::Create(const String &title, int width, int height)
            {
                // タイトルと寸法を保存
                m_title = title;
                m_width = width;
                m_height = height;

                // ウィンドウクラスの登録
                if (!RegisterWindowClass())
                {
                    return false;
                }

                // ウィンドウスタイルの設定
                DWORD style = WS_OVERLAPPEDWINDOW;
                DWORD exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

                // ウィンドウサイズの調整
                int windowWidth = width;
                int windowHeight = height;
                AdjustWindowSize(style, exStyle, windowWidth, windowHeight);

                // ウィンドウの中央配置のための座標計算
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                int posX = (screenWidth - windowWidth) / 2;
                int posY = (screenHeight - windowHeight) / 2;

                // ウィンドウの作成
                m_hWnd = CreateWindowEx(
                    exStyle,              // 拡張ウィンドウスタイル
                    GetWindowClassName(), // ウィンドウクラス名
                    m_title.c_str(),      // ウィンドウタイトル
                    style,                // ウィンドウスタイル
                    posX,                 // ウィンドウX座標
                    posY,                 // ウィンドウY座標
                    windowWidth,          // ウィンドウ幅
                    windowHeight,         // ウィンドウ高さ
                    nullptr,              // 親ウィンドウ
                    nullptr,              // メニュー
                    m_hInstance,          // インスタンスハンドル
                    this                  // ユーザーデータ（自身のポインタ）
                );

                // ウィンドウ作成チェック
                if (!m_hWnd)
                {
                    return false;
                }

                return true;
            }

            void WindowsWindow::Destroy()
            {
                // ウィンドウが存在する場合のみ
                if (m_hWnd)
                {
                    DestroyWindow(m_hWnd);
                    m_hWnd = nullptr;
                }
            }

            void WindowsWindow::Show()
            {
                if (m_hWnd)
                {
                    ShowWindow(m_hWnd, SW_SHOW);
                    UpdateWindow(m_hWnd);
                }
            }

            void WindowsWindow::Hide()
            {
                if (m_hWnd)
                {
                    ShowWindow(m_hWnd, SW_HIDE);
                }
            }

            void WindowsWindow::SetTitle(const String &title)
            {
                m_title = title;
                if (m_hWnd)
                {
                    SetWindowText(m_hWnd, m_title.c_str());
                }
            }

            void WindowsWindow::Resize(int width, int height)
            {
                m_width = width;
                m_height = height;

                if (m_hWnd)
                {
                    // クライアント領域のサイズを調整
                    DWORD style = static_cast<DWORD>(GetWindowLong(m_hWnd, GWL_STYLE));
                    DWORD exStyle = static_cast<DWORD>(GetWindowLong(m_hWnd, GWL_EXSTYLE));

                    // ウィンドウサイズの調整
                    int windowWidth = width;
                    int windowHeight = height;
                    AdjustWindowSize(style, exStyle, windowWidth, windowHeight);

                    // ウィンドウサイズの変更
                    SetWindowPos(
                        m_hWnd,
                        nullptr,
                        0, 0, // 座標は変更しない
                        windowWidth, windowHeight,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }

            bool WindowsWindow::IsActive() const
            {
                return m_isActive;
            }

            void *WindowsWindow::GetNativeHandle() const
            {
                return m_hWnd;
            }

            HWND WindowsWindow::GetHWND() const
            {
                return m_hWnd;
            }

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib
