#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI 
{

/**
 * @brief スワップチェーンインターフェース
 * スワップチェーンはウィンドウ表示に使用するバックバッファを管理します。
 */
class ISwapChain 
{
public:
    virtual ~ISwapChain() = default;

    /**
     * @brief スワップチェーンの幅を取得
     * @return スワップチェーンの幅
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * @brief スワップチェーンの高さを取得
     * @return スワップチェーンの高さ
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * @brief バックバッファ数を取得
     * @return バックバッファ数
     */
    virtual uint32_t GetBufferCount() const = 0;

    /**
     * @brief 現在のバックバッファインデックスを取得
     * @return 現在のバックバッファインデックス
     */
    virtual uint32_t GetCurrentBackBufferIndex() const = 0;

    /**
     * @brief バックバッファを取得
     * @param index バッファインデックス
     * @return バックバッファテクスチャ
     */
    virtual TexturePtr GetBackBuffer(uint32_t index) const = 0;

    /**
     * @brief 現在のバックバッファを取得
     * @return 現在のバックバッファテクスチャ
     */
    virtual TexturePtr GetCurrentBackBuffer() const = 0;

    /**
     * @brief スワップチェーンのプレゼント（表示）
     * @param vsync 垂直同期を行うかどうか
     */
    virtual void Present(bool vsync = true) = 0;

    /**
     * @brief スワップチェーンのリサイズ
     * @param width 新しい幅
     * @param height 新しい高さ
     */
    virtual void Resize(uint32_t width, uint32_t height) = 0;
};

} // namespace NorvesLib::RHI
