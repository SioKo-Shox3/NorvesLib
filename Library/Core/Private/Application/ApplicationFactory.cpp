#include "Application/ApplicationFactory.h"
#include "Platform/Windows/WindowsApplicationFactory.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib
{
    namespace Core
    {
        namespace Boot
        {

            Core::Container::TUniquePtr<IApplication> ApplicationFactory::CreateDefaultApplication()
            {
                // プラットフォーム固有のアプリケーションを作成
#ifdef _WIN32
                return Platform::WindowsApplicationFactory::CreateWindowsApplication();
#else
                return nullptr;
#endif
            }

            Core::Container::TUniquePtr<IApplication> ApplicationFactory::CreateCustomApplication()
            {
                // 将来的にカスタマイズが必要になった場合、ここで拡張可能
                return CreateDefaultApplication();
            }

            Core::Container::TUniquePtr<IApplication> ApplicationFactory::GetPlatformSpecificImplementation()
            {
                // この関数は使用されなくなったが、互換性のため残す
                return CreateDefaultApplication();
            }

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib
