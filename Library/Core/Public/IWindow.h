#pragma once

#include <string>

namespace NorvesLib 
{

/**
 * @brief ウィンドウの抽象インターフェース
 */
class IWindow
{
public:
    virtual ~IWindow() = default;

    /**
     * @brief ウィンドウを表示する
     */
    virtual void Show() = 0;

    /**
     * @brief ウィンドウを非表示にする
     */
    virtual void Hide() = 0;

    /**
     * @brief ウィンドウのタイトルを設定する
     * @param title 新しいウィンドウタイトル
     */
    virtual void SetTitle(const std::wstring& title) = 0;

    /**
     * @brief ウィンドウのタイトルを取得する
     * @return 現在のウィンドウタイトル
     */
    virtual std::wstring GetTitle() const = 0;
};

} // namespace NorvesLib