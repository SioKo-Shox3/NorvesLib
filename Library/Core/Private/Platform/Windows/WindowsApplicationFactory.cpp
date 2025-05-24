#include "Platform/Windows/WindowsApplicationFactory.h"
#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"

namespace NorvesLib
{
    namespace Core
    {
        namespace Platform
        {

            std::unique_ptr<IApplication> WindowsApplicationFactory::CreateWindowsApplication()
            {
                // Windows向けアプリケーション実装を返却
                auto app = std::make_unique<WindowsApplication>();

                // 必要に応じてWindowsアプリケーションの初期設定を行う
                // ...

                return app;
            }

            std::shared_ptr<IWindow> WindowsApplicationFactory::CreateWindowsWindow()
            {
                // Windows向けウィンドウ実装を返却
                return std::make_shared<WindowsWindow>();
            }

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib