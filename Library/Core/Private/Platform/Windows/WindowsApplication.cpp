#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"
#include <algorithm>

using namespace NorvesLib::Core::Container;

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

bool WindowsApplication::Initialize(const Core::Container::VariableArray<Core::Container::String>& args)
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
    if (m_isRunning)
    {
        // すべてのウィンドウを閉じる
        for (auto& window : m_windows)
        {
            if (window)
            {
                window->Destroy();
            }
        }
        
        m_windows.clear();
        m_mainWindow = nullptr;
        m_isRunning = false;
    }
}

IWindow* WindowsApplication::GetMainWindow()
{
    return m_mainWindow.get();
}

void WindowsApplication::RegisterWindow(Core::Container::TSharedPtr<IWindow> window)
{
    // 新しいウィンドウを登録
    m_windows.push_back(window);
    
    // メインウィンドウが未設定の場合は、最初のウィンドウをメインウィンドウとして設定
    if (!m_mainWindow)
    {
        m_mainWindow = window;
    }
}

void WindowsApplication::UnregisterWindow(Core::Container::TSharedPtr<IWindow> window)
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

const Core::Container::VariableArray<Core::Container::String>& WindowsApplication::GetCommandLineArgs() const
{
    return m_args;
}

int WindowsApplication::ProcessWindowsMessages()
{
    MSG msg = {};
    
    while (m_isRunning)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_isRunning = false;
                break;
            }
            
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // メインウィンドウが閉じられた場合はアプリケーションを終了
        if (!m_mainWindow || m_windows.empty())
        {
            m_isRunning = false;
        }
        
        // CPU使用率を下げるための短いスリープ
        Sleep(1);
    }
    
    return static_cast<int>(msg.wParam);
}

} // namespace Platform
} // namespace Core
} // namespace NorvesLib