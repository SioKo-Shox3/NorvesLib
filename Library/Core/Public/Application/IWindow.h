#pragma once

#include "Container/String.h"

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

    /**
     * @brief ウィンドウハンドルの取得
     * @return プラットフォーム固有のウィンドウハンドル
     */
    virtual void* GetNativeHandle() const = 0;
};

} // namespace NorvesLib