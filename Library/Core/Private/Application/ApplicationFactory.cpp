#include "Application/ApplicationFactory.h"
#include "Platform/Windows/WindowsApplicationFactory.h"
#include <memory>

namespace NorvesLib
{
    namespace Core
    {
        namespace Boot
        {

            std::unique_ptr<IApplication> ApplicationFactory::CreateDefaultApplication()
            {
                // プラットフォーム固有の実装を使用
                return GetPlatformSpecificImplementation();
            }

            std::unique_ptr<IApplication> ApplicationFactory::CreateCustomApplication()
            {
                // プラットフォーム固有の実装を使用
                // 将来的にカスタマイズが必要になった場合、ここで拡張可能
                return GetPlatformSpecificImplementation();
            }

            std::unique_ptr<IApplication> ApplicationFactory::GetPlatformSpecificImplementation()
            {
// プラットフォームに応じて適切なファクトリーを選択
#ifdef _WIN32
                // Windows環境
                return Core::Platform::WindowsApplicationFactory::CreateWindowsApplication();
#else
// 未対応のプラットフォーム
#error "対応していないプラットフォームです。"
#endif
            }

        } // namespace Boot
    } // namespace Core
} // namespace NorvesLib