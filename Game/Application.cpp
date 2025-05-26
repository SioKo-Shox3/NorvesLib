#include "Core/Public/Container/PointerTypes.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Platform/Windows/WindowsApplicationFactory.h"
#include <iostream>

using namespace NorvesLib::Core::Container;

// ゲームアプリケーションクラスの実装
class GameApplication : public NorvesLib::IApplication
{
private:
    TUniquePtr<NorvesLib::IApplication> m_platformApp;

public:
    virtual ~GameApplication() = default;

    virtual bool Initialize(const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> &args) override
    {
        std::wcout << L"GameApplication::Initialize() called" << std::endl;

        // Windows環境向けのアプリケーションを作成
        m_platformApp = NorvesLib::Core::Platform::WindowsApplicationFactory::CreateWindowsApplication();

        if (!m_platformApp)
        {
            std::wcerr << L"Failed to create platform application" << std::endl;
            return false;
        }

        std::wcout << L"Platform application created successfully" << std::endl;

        // プラットフォームアプリケーションを初期化
        if (!m_platformApp->Initialize(args))
        {
            std::wcerr << L"Failed to initialize platform application" << std::endl;
            return false;
        }

        std::wcout << L"Platform application initialized successfully" << std::endl;

        // メインウィンドウを作成
        auto mainWindow = NorvesLib::Core::Platform::WindowsApplicationFactory::CreateWindowsWindow();
        if (!mainWindow)
        {
            std::wcerr << L"Failed to create main window" << std::endl;
            return false;
        }

        std::wcout << L"Main window created successfully" << std::endl;

        // ウィンドウを作成（タイトル、幅、高さを指定）
        if (!mainWindow->Create("NorvesLib Game Application", 1024, 768))
        {
            std::wcerr << L"Failed to create window" << std::endl;
            return false;
        }

        std::wcout << L"Window created successfully" << std::endl;

        // ウィンドウを表示
        mainWindow->Show();

        std::wcout << L"Window shown successfully" << std::endl;

        // アプリケーションにウィンドウを登録
        m_platformApp->RegisterWindow(mainWindow);

        std::wcout << L"Window registered successfully" << std::endl;
        std::wcout << L"Initialization completed. Starting message loop..." << std::endl;

        return true;
    }

    virtual int Run() override
    {
        std::wcout << L"GameApplication::Run() called" << std::endl;

        if (!m_platformApp)
        {
            std::wcerr << L"Platform application is null" << std::endl;
            return -1;
        }

        std::wcout << L"Starting platform application message loop..." << std::endl;

        // プラットフォームアプリケーションのメッセージループを実行
        int exitCode = m_platformApp->Run();

        std::wcout << L"Message loop ended with exit code: " << exitCode << std::endl;

        return exitCode;
    }

    virtual void Shutdown() override
    {
        std::wcout << L"GameApplication::Shutdown() called" << std::endl;

        if (m_platformApp)
        {
            m_platformApp->Shutdown();
        }

        std::wcout << L"Shutdown completed" << std::endl;
    }

    virtual NorvesLib::IWindow *GetMainWindow() override
    {
        return m_platformApp ? m_platformApp->GetMainWindow() : nullptr;
    }

    virtual void RegisterWindow(TSharedPtr<NorvesLib::IWindow> window) override
    {
        if (m_platformApp)
        {
            m_platformApp->RegisterWindow(window);
        }
    }

    virtual void UnregisterWindow(TSharedPtr<NorvesLib::IWindow> window) override
    {
        if (m_platformApp)
        {
            m_platformApp->UnregisterWindow(window);
        }
    }
};

// アプリケーションインスタンスを作成する関数の実装
TUniquePtr<NorvesLib::IApplication> CreateApplication()
{
    std::wcout << L"CreateApplication() called" << std::endl;
    return MakeUnique<GameApplication>();
}

// PlatformBootから呼ばれるデフォルトアプリケーション作成関数
TUniquePtr<NorvesLib::IApplication> CreateDefaultApplication()
{
    std::wcout << L"CreateDefaultApplication() called" << std::endl;
    return MakeUnique<GameApplication>();
}