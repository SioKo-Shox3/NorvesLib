#pragma once

#include "RHITypes.h"
#include <vector>

namespace NorvesLib::RHI 
{

/**
 * @brief フレームバッファインターフェース
 * フレームバッファはレンダーパスで使用するテクスチャ群を管理するオブジェクトです。
 */
class IFramebuffer 
{
public:
    virtual ~IFramebuffer() = default;

    /**
     * @brief フレームバッファの幅を取得
     * @return フレームバッファの幅
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * @brief フレームバッファの高さを取得
     * @return フレームバッファの高さ
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * @brief 関連付けられたレンダーパスを取得
     * @return レンダーパス
     */
    virtual RenderPassPtr GetRenderPass() const = 0;

    /**
     * @brief カラーアタッチメントを取得
     * @param index アタッチメントインデックス
     * @return カラーアタッチメント
     */
    virtual TexturePtr GetColorAttachment(uint32_t index) const = 0;

    /**
     * @brief デプスステンシルアタッチメントを取得
     * @return デプスステンシルアタッチメント
     */
    virtual TexturePtr GetDepthStencilAttachment() const = 0;

    /**
     * @brief カラーアタッチメント数を取得
     * @return カラーアタッチメント数
     */
    virtual uint32_t GetColorAttachmentCount() const = 0;

    /**
     * @brief デプスステンシルアタッチメントを持つかどうか
     * @return デプスステンシルアタッチメントを持つ場合true
     */
    virtual bool HasDepthStencilAttachment() const = 0;
};

} // namespace NorvesLib::RHI
