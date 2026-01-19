#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"
#include <algorithm>
#include <iostream>

using namespace NorvesLib::Core::Container;

namespace NorvesLib
{
    namespace Core
    {
        namespace Platform
        {

            WindowsApplication::WindowsApplication()
                : m_hInstance(GetModuleHandle(nullptr)), m_mainWindow(nullptr), m_isRunning(false)
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

            bool WindowsApplication::Initialize(const Core::Container::VariableArray<Core::Container::String> &args)
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
                    std::wcerr << L"WindowsApplication::Run() - Application not initialized" << std::endl;
                    return -1;
                }

                std::wcout << L"WindowsApplication::Run() - Starting Windows message loop..." << std::endl;
                std::wcout << L"WindowsApplication::Run() - Main window: " << (m_mainWindow ? L"Valid" : L"Null") << std::endl;
                std::wcout << L"WindowsApplication::Run() - Number of windows: " << m_windows.size() << std::endl;

                // Windowsメッセージループを処理
                int exitCode = ProcessWindowsMessages();

                std::wcout << L"WindowsApplication::Run() - Message loop ended with exit code: " << exitCode << std::endl;
                return exitCode;
            }

            void WindowsApplication::Shutdown()
            {
                if (m_isRunning)
                {
                    // すべてのウィンドウを閉じる
                    for (auto &window : m_windows)
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

            IWindow *WindowsApplication::GetMainWindow()
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

            const Core::Container::VariableArray<Core::Container::String> &WindowsApplication::GetCommandLineArgs() const
            {
                return m_args;
            }

            int WindowsApplication::ProcessWindowsMessages()
            {
                MSG msg = {};
                std::wcout << L"ProcessWindowsMessages() - Starting message loop" << std::endl;

                int messageCount = 0;

                while (m_isRunning)
                {
                    bool hasMessage = false;

                    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                    {
                        hasMessage = true;
                        messageCount++;

                        if (msg.message == WM_QUIT)
                        {
                            std::wcout << L"ProcessWindowsMessages() - Received WM_QUIT message" << std::endl;
                            m_isRunning = false;
                            break;
                        }

                        // 重要なメッセージをログ出力
                        if (msg.message == WM_CLOSE || msg.message == WM_DESTROY ||
                            msg.message == WM_PAINT || msg.message == WM_KEYDOWN)
                        {
                            std::wcout << L"ProcessWindowsMessages() - Message: " << msg.message
                                       << L" (Total processed: " << messageCount << L")" << std::endl;
                        }

                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }

                    // メインウィンドウが閉じられた場合はアプリケーションを終了
                    if (!m_mainWindow || m_windows.empty())
                    {
                        std::wcout << L"ProcessWindowsMessages() - Main window lost or no windows remaining" << std::endl;
                        m_isRunning = false;
                    }

                    // 定期的な状態報告（メッセージがない場合のみ）
                    static int idleCount = 0;
                    if (!hasMessage)
                    {
                        idleCount++;
                        if (idleCount % 1000 == 0) // 1000回に1回報告
                        {
                            std::wcout << L"ProcessWindowsMessages() - Idle loop " << idleCount
                                       << L", Messages processed: " << messageCount << std::endl;
                        }
                    }

                    // CPU使用率を下げるための短いスリープ
                    Sleep(1);
                }

                std::wcout << L"ProcessWindowsMessages() - Loop ended. Total messages processed: "
                           << messageCount << std::endl;

                return static_cast<int>(msg.wParam);
            }

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib
