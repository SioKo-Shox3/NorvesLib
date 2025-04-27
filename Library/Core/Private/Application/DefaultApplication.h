#pragma once

#include "Public/Application/IApplication.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace NorvesLib {
namespace Core {
namespace Boot {

/**
 * @brief デフォルトのアプリケーション実装
 */
class DefaultApplication : public IApplication
{
public:
    /**
     * @brief コンストラクタ
     */
    DefaultApplication() = default;

    /**
     * @brief デストラクタ
     */
    virtual ~DefaultApplication() override = default;

    /**
     * @brief アプリケーションの初期化処理
     * @param args コマンドライン引数
     * @return 初期化の成否
     */
    virtual bool Initialize(const std::vector<std::wstring> &args) override;

    /**
     * @brief アプリケーションのメインループを実行
     * @return 終了コード
     */
    virtual int Run() override;

    /**
     * @brief アプリケーションのシャットダウン処理
     */
    virtual void Shutdown() override;

    /**
     * @brief メインウィンドウの取得
     * @return メインウィンドウへの参照
     */
    virtual IWindow *GetMainWindow() override;

    /**
     * @brief アプリケーションにウィンドウを登録
     * @param window 登録するウィンドウ
     */
    virtual void RegisterWindow(std::shared_ptr<IWindow> window) override;

    /**
     * @brief ウィンドウの登録解除
     * @param window 解除するウィンドウ
     */
    virtual void UnregisterWindow(std::shared_ptr<IWindow> window) override;

private:
    std::vector<std::shared_ptr<IWindow>> m_windows;
    std::shared_ptr<IWindow> m_mainWindow = nullptr;
    bool m_isRunning = false;
};

} // namespace Boot
} // namespace Core
} // namespace NorvesLib