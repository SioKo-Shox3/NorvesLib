#include "Platform/PlatformApplicationFactory.h"
#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Platform
{

    Container::TUniquePtr<NorvesLib::IApplication> CreatePlatformApplication()
    {
        // Windows向けアプリケーション実装を返却
        auto app = MakeUnique<WindowsApplication>();

        // 必要に応じてWindowsアプリケーションの初期設定を行う
        // ...

        return app;
    }

    Container::TSharedPtr<NorvesLib::IWindow> CreatePlatformWindow()
    {
        // Windows向けウィンドウ実装を返却
        return MakeShared<WindowsWindow>();
    }

} // namespace NorvesLib::Core::Platform
