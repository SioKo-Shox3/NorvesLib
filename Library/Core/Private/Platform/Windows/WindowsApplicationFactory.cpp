#include "Platform/Windows/WindowsApplicationFactory.h"
#include "Platform/Windows/WindowsApplication.h"
#include "Platform/Windows/WindowsWindow.h"

namespace NorvesLib {
namespace Core {
namespace Platform {

std::unique_ptr<IApplication> WindowsApplicationFactory::CreateWindowsApplication()
{
    return std::make_unique<WindowsApplication>();
}

std::shared_ptr<IWindow> WindowsApplicationFactory::CreateWindowsWindow()
{
    return std::make_shared<WindowsWindow>();
}

} // namespace Platform
} // namespace Core
} // namespace NorvesLib

// 既存のCreateApplication関数を修正して、Windows向け実装を使用するようにする
std::unique_ptr<NorvesLib::IApplication> CreateApplication()
{
    // Windows向けのアプリケーションを作成して返す
    return NorvesLib::Core::Platform::WindowsApplicationFactory::CreateWindowsApplication();
}