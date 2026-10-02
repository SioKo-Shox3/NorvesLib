#pragma once

#include "Container/String.h"
#include "Platform/NativeWindowHandle.h"
#include "Delegate/MulticastDelegate.h"

namespace NorvesLib {

/**
 * @brief ウィンドウ抽象インターフェース
 *
 * プラットフォーム固有のウィンドウ実装を抽象化する
 * インターフェースクラスです。
 */
class IWindow
{
public:
    /**
     * @brief 仮想デストラクタ
     */
    virtual ~IWindow() = default;

    /**
     * @brief ウィンドウの作成
     * @param title ウィンドウタイトル
     * @param width ウィンドウ幅
     * @param height ウィンドウ高さ
     * @return 作成の成否
     */
    virtual bool Create(const Core::Container::String& title, int width, int height) = 0;

    /**
     * @brief ウィンドウの破棄
     */
    virtual void Destroy() = 0;

    /**
     * @brief ウィンドウの表示
     */
    virtual void Show() = 0;

    /**
     * @brief ウィンドウの非表示
     */
    virtual void Hide() = 0;

    /**
     * @brief ウィンドウタイトルの設定
     * @param title 新しいウィンドウタイトル
     */
    virtual void SetTitle(const Core::Container::String& title) = 0;

    /**
     * @brief ウィンドウサイズの変更
     * @param width 新しい幅
     * @param height 新しい高さ
     */
    virtual void Resize(int width, int height) = 0;

    /**
     * @brief ウィンドウがアクティブかどうかを確認
     * @return アクティブかどうか
     */
    virtual bool IsActive() const = 0;

    /// ゲーム入力を受け付けるkeyboard focus。既存実装はactive状態を既定とする。
    virtual bool IsInputFocused() const { return IsActive(); }
    /// GameThreadでmain windowのRaw mouseを明示登録する。別の登録操作も同threadへ直列化する。
    /// 非対応/所有競合/OS失敗はfalse。
    /// disableはOS解除の成否によらず論理配送を止め、解除失敗なら所有情報を再試行まで残す。
    virtual bool SetRawMouseEnabled(bool enabled) noexcept { return !enabled; }
    virtual bool IsRawMouseEnabled() const noexcept { return false; }
    /// OS処理内の通知。購読側はwindow表示/activation/破棄、Engine破棄、
    /// 購読変更、再入配送、例外送出を行わない。一般処理はApplicationHandlerへ遅延する。
    Core::MulticastDelegate<bool>& OnInputFocusChanged() { return m_InputFocusChanged; }

    /**
     * @brief ウィンドウハンドルの取得
     * @return プラットフォーム固有のウィンドウハンドル
     */
    virtual Core::Platform::NativeWindowHandle GetNativeHandle() const = 0;

protected:
    void NotifyInputFocusChanged(bool focused) { m_InputFocusChanged.Broadcast(focused); }

private:
    Core::MulticastDelegate<bool> m_InputFocusChanged;
};

} // namespace NorvesLib
