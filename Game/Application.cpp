#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Platform/Windows/WindowsApplicationFactory.h"

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
        LOG_INFO("GameApplication::Initialize() called");

        // Windows環境向けのアプリケーションを作成
        m_platformApp = NorvesLib::Core::Platform::WindowsApplicationFactory::CreateWindowsApplication();

        if (!m_platformApp)
        {
            LOG_ERROR("Failed to create platform application");
            return false;
        }

        LOG_INFO("Platform application created successfully");

        // プラットフォームアプリケーションを初期化
        if (!m_platformApp->Initialize(args))
        {
            LOG_ERROR("Failed to initialize platform application");
            return false;
        }

        LOG_INFO("Platform application initialized successfully");

        // メインウィンドウを作成
        auto mainWindow = NorvesLib::Core::Platform::WindowsApplicationFactory::CreateWindowsWindow();
        if (!mainWindow)
        {
            LOG_ERROR("Failed to create main window");
            return false;
        }

        LOG_INFO("Main window created successfully");

        // ウィンドウを作成（タイトル、幅、高さを指定）
        if (!mainWindow->Create("NorvesLib Game Application", 1024, 768))
        {
            LOG_ERROR("Failed to create window");
            return false;
        }

        LOG_INFO("Window created successfully");

        // ウィンドウを表示
        mainWindow->Show();

        LOG_INFO("Window shown successfully");

        // アプリケーションにウィンドウを登録
        m_platformApp->RegisterWindow(mainWindow);
        LOG_INFO("Window registered successfully");
        LOG_INFO("Initialization completed. Starting message loop...");

        return true;
    }

    virtual int Run() override
    {
        LOG_INFO("GameApplication::Run() called");

        if (!m_platformApp)
        {
            LOG_ERROR("Platform application is null");
            return -1;
        }

        LOG_INFO("Starting platform application message loop...");

        // プラットフォームアプリケーションのメッセージループを実行
        int exitCode = m_platformApp->Run();

        LOG_INFO_F("Message loop ended with exit code: %d", exitCode);

        return exitCode;
    }

    virtual void Shutdown() override
    {
        LOG_INFO("GameApplication::Shutdown() called");

        if (m_platformApp)
        {
            m_platformApp->Shutdown();
        }

        LOG_INFO("Shutdown completed");
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
    LOG_INFO("CreateApplication() called");
    return MakeUnique<GameApplication>();
}

// PlatformBootから呼ばれるデフォルトアプリケーション作成関数
TUniquePtr<NorvesLib::IApplication> CreateDefaultApplication()
{
    LOG_INFO("CreateDefaultApplication() called");
    return MakeUnique<GameApplication>();
}