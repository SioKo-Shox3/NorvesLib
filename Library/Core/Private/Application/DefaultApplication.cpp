#include "DefaultApplication.h"
#include <windows.h>

namespace NorvesLib {
namespace Core {
namespace Boot {

bool DefaultApplication::Initialize(const std::vector<std::wstring> &args)
{
    // コマンドライン引数の処理
    // 現在は特に処理しないがオプション解析などの拡張ポイント
    
    // 初期化成功
    m_isRunning = true;
    return true;
}

int DefaultApplication::Run()
{
    // 基本的なメッセージループの実装
    MSG msg = {};
    
    // アプリケーションの実行ループ
    while (m_isRunning)
    {
        // Windowsメッセージの処理
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            
            // WM_QUITメッセージを受け取ったらループを終了
            if (msg.message == WM_QUIT)
            {
                m_isRunning = false;
                break;
            }
        }
        
        // アプリケーション固有の処理を行う
        // ゲームループや描画などはここに追加
        
        // スリープして CPU 使用率を下げる (必要に応じて調整)
        Sleep(1);
    }
    
    return static_cast<int>(msg.wParam);
}

void DefaultApplication::Shutdown()
{
    m_isRunning = false;
    
    // すべてのウィンドウをクリーンアップ
    m_windows.clear();
    m_mainWindow = nullptr;
}

IWindow* DefaultApplication::GetMainWindow()
{
    return m_mainWindow.get();
}

void DefaultApplication::RegisterWindow(std::shared_ptr<IWindow> window)
{
    // 新しいウィンドウを登録
    m_windows.push_back(window);
    
    // メインウィンドウが未設定の場合は、最初のウィンドウをメインウィンドウとして設定
    if (!m_mainWindow)
    {
        m_mainWindow = window;
    }
}

void DefaultApplication::UnregisterWindow(std::shared_ptr<IWindow> window)
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

} // namespace Boot
} // namespace Core
} // namespace NorvesLib