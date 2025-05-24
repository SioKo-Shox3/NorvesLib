#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"
#include <algorithm>

namespace NorvesLib {
namespace Core {
namespace Platform {

WindowsApplication::WindowsApplication()
    : m_hInstance(GetModuleHandle(nullptr))
    , m_mainWindow(nullptr)
    , m_isRunning(false)
{
}

WindowsApplication::~WindowsApplication()
{
    // 明示的なシャットダウンが呼ばれていなかった場合の保険
    if (m_isRunning)
    {
        Shutdown();
    }
}

bool WindowsApplication::Initialize(const Container::VariableArray<Container::String>& args)
{
    // コマンドライン引数を保存
    m_args = args;
    
    // 初期化成功
    m_isRunning = true;
    return true;
}

int WindowsApplication::Run()
{
    if (!m_isRunning)
    {
        // 初期化されていない場合はエラー終了
        return -1;
    }
    
    // Windowsメッセージループを処理
    return ProcessWindowsMessages();
}

void WindowsApplication::Shutdown()
{
    // 実行フラグをOFF
    m_isRunning = false;
    
    // すべてのウィンドウをクリーンアップ
    m_windows.clear();
    m_mainWindow = nullptr;
}

IWindow* WindowsApplication::GetMainWindow()
{
    return m_mainWindow.get();
}

void WindowsApplication::RegisterWindow(std::shared_ptr<IWindow> window)
{
    // 新しいウィンドウを登録
    m_windows.push_back(window);
    
    // メインウィンドウが未設定の場合は、最初のウィンドウをメインウィンドウとして設定
    if (!m_mainWindow)
    {
        m_mainWindow = window;
    }
}

void WindowsApplication::UnregisterWindow(std::shared_ptr<IWindow> window)
{
    // ウィンドウの検索と削除
    auto it = std::find(m_windows.begin(), m_windows.end(), window);
    if (it != m_windows.end())
    {
        // メインウィンドウだった場合は nullptr に設定
        if (*it == m_mainWindow)
        {
            m_mainWindow = nullptr;
        }
        
        m_windows.erase(it);
    }
}

HINSTANCE WindowsApplication::GetInstance() const
{
    return m_hInstance;
}

const Container::VariableArray<Container::String>& WindowsApplication::GetCommandLineArgs() const
{
    return m_args;
}

int WindowsApplication::ProcessWindowsMessages()
{
    // 基本的なメッセージループの実装
    MSG msg = {};
    BOOL ret = 0;
    
    // アプリケーションの実行ループ
    while (m_isRunning)
    {
        // Windowsメッセージの処理
        while ((ret = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) != 0)
        {
            // WM_QUITメッセージを受け取ったらループを終了
            if (msg.message == WM_QUIT)
            {
                m_isRunning = false;
                break;
            }
            
            // メッセージの変換と送信
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // 全てのウィンドウが閉じられたら終了
        if (m_windows.empty())
        {
            m_isRunning = false;
        }
        
        // アプリケーション固有の処理を行う
        // ここにゲームループや描画などを追加
        
        // スリープして CPU 使用率を下げる (必要に応じて調整)
        Sleep(1);
    }
    
    return static_cast<int>(msg.wParam);
}

} // namespace Platform
} // namespace Core
} // namespace NorvesLib