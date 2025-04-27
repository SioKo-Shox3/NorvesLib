#include "Public/Application/ApplicationFactory.h"
#include "DefaultApplication.h"
#include <memory>

namespace NorvesLib {
namespace Core {
namespace Boot {

std::unique_ptr<IApplication> ApplicationFactory::CreateDefaultApplication()
{
    // デフォルトアプリケーションインスタンスを作成して返す
    return std::make_unique<DefaultApplication>();
}

std::unique_ptr<IApplication> ApplicationFactory::CreateCustomApplication()
{
    // カスタムアプリケーションを作成する処理を実装
    // 現在はデフォルトと同じ実装を返すが、将来的にはゲーム固有の実装を返すことを想定
    return std::make_unique<DefaultApplication>();
}

} // namespace Boot
} // namespace Core
} // namespace NorvesLib

// エントリーポイント（WinMain.cpp）から呼び出される関数の実装
std::unique_ptr<NorvesLib::IApplication> CreateApplication()
{
    // ApplicationFactoryを通じてアプリケーションインスタンスを作成
    return NorvesLib::Core::Boot::ApplicationFactory::CreateCustomApplication();
}