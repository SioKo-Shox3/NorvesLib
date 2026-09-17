#pragma once

#include "RHITypes.h"

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

    /**
     * @brief デプスステンシル配列アタッチメントのレイヤーを取得
     *
     * 非配列テクスチャでは常に0を返します。バックエンドは指定レイヤーだけを
     * attachment viewへ束ね、フレームバッファ自体は1 layerとして扱います。
     * @return デプスステンシル配列レイヤー
     */
    virtual uint32_t GetDepthStencilArrayLayer() const { return 0; }
};

} // namespace NorvesLib::RHI
