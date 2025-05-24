#include <memory>
#include "Application/IApplication.h"
#include "Application/IWindow.h"

// ゲームアプリケーションクラスの簡易実装
class GameApplication : public NorvesLib::IApplication
{
public:
    virtual ~GameApplication() = default;

    virtual bool Initialize(const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> &args) override
    {
        // 初期化処理
        return true;
    }

    virtual int Run() override
    {
        // ゲームメインループ
        return 0;
    }

    virtual void Shutdown() override
    {
        // 終了処理
    }

    virtual NorvesLib::IWindow *GetMainWindow() override
    {
        return nullptr; // 実際の実装ではウィンドウを返す
    }

    virtual void RegisterWindow(std::shared_ptr<NorvesLib::IWindow> window) override
    {
        // ウィンドウ登録処理
    }

    virtual void UnregisterWindow(std::shared_ptr<NorvesLib::IWindow> window) override
    {
        // ウィンドウ登録解除処理
    }
};

// アプリケーションインスタンスを作成する関数の実装
std::unique_ptr<NorvesLib::IApplication> CreateApplication()
{
    return std::make_unique<GameApplication>();
}