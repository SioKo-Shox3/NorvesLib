#include "Application/ApplicationFactory.h"
// GameApplicationの作成のため、直接インクルード
// プラットフォーム固有のアプリケーションではなく、ゲームアプリケーションを作成

extern NorvesLib::Core::Container::TUniquePtr<NorvesLib::IApplication> CreateDefaultApplication();

using namespace NorvesLib::Core::Container;

namespace NorvesLib
{
    namespace Core
    {
        namespace Boot
        {

            Core::Container::TUniquePtr<IApplication> ApplicationFactory::CreateDefaultApplication()
            {
                // ゲームアプリケーションを作成（Game/Application.cppで定義）
                return ::CreateDefaultApplication();
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