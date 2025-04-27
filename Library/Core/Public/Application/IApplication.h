#pragma once

#include <memory>
#include "Container/VariableArray.h"
#include "Container/String.h"
#include "EngineGlobals/GlobalMemoryAllocator.h"

namespace NorvesLib 
{

class IWindow;

/**
 * @brief アプリケーション抽象インターフェース
 *
 * OS毎のエントリーポイントを抽象化し、起動引数のサニタイズや
 * アプリケーションのライフサイクル管理を提供する
 */
class IApplication
{
public:
    /**
     * @brief 仮想デストラクタ
     */
    virtual ~IApplication() = default;

    /**
     * @brief アプリケーションの初期化
     * @param args コマンドライン引数
     * @return 初期化の成否
     */
    virtual bool Initialize(const Core::Container::VariableArray<Core::Container::String> &args) = 0;

    /**
     * @brief アプリケーションの実行
     * @return 終了コード
     */
    virtual int Run() = 0;

    /**
     * @brief アプリケーションの終了
     */
    virtual void Shutdown() = 0;

    /**
     * @brief メインウィンドウの取得
     * @return メインウィンドウへの参照
     */
    virtual IWindow *GetMainWindow() = 0;

    /**
     * @brief アプリケーションにウィンドウを登録
     * @param window 登録するウィンドウ
     */
    virtual void RegisterWindow(std::shared_ptr<IWindow> window) = 0;

    /**
     * @brief ウィンドウの登録解除
     * @param window 解除するウィンドウ
     */
    virtual void UnregisterWindow(std::shared_ptr<IWindow> window) = 0;
};

} // namespace NorvesLib