#include "Platform/Windows/WindowsApplicationFactory.h"
#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib
{
    namespace Core
    {
        namespace Platform
        {

            Core::Container::TUniquePtr<IApplication> WindowsApplicationFactory::CreateWindowsApplication()
            {
                // Windows向けアプリケーション実装を返却
                auto app = MakeUnique<WindowsApplication>();

                // 必要に応じてWindowsアプリケーションの初期設定を行う
                // ...

                return app;
            }

            Core::Container::TSharedPtr<IWindow> WindowsApplicationFactory::CreateWindowsWindow()
            {
                // Windows向けウィンドウ実装を返却
                return MakeShared<WindowsWindow>();
            }

        } // namespace Platform
    } // namespace Core
} // namespace NorvesLib
