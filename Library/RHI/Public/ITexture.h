#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI 
{

/**
 * @brief テクスチャインターフェース
 * テクスチャは画像データを格納するリソースです。
 * レンダーターゲット、デプスステンシルバッファ、サンプリングテクスチャなどに使用されます。
 */
class ITexture 
{
public:
    virtual ~ITexture() = default;

    /**
     * @brief テクスチャの幅を取得
     * @return テクスチャの幅（ピクセル）
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * @brief テクスチャの高さを取得
     * @return テクスチャの高さ（ピクセル）
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * @brief テクスチャの深さを取得
     * @return テクスチャの深さ（3Dテクスチャの場合）
     */
    virtual uint32_t GetDepth() const = 0;

    /**
     * @brief テクスチャのミップレベル数を取得
     * @return ミップレベル数
     */
    virtual uint32_t GetMipLevels() const = 0;

    /**
     * @brief テクスチャの配列サイズを取得
     * @return 配列サイズ
     */
    virtual uint32_t GetArraySize() const = 0;

    /**
     * @brief テクスチャのフォーマットを取得
     * @return テクスチャのフォーマット
     */
    virtual Format GetFormat() const = 0;

    /**
     * @brief テクスチャの使用用途を取得
     * @return テクスチャの使用用途
     */
    virtual ResourceUsage GetUsage() const = 0;

    /**
     * @brief キューブマップかどうかを取得
     * @return キューブマップの場合true
     */
    virtual bool IsCubemap() const = 0;

    /**
     * @brief テクスチャデータを更新
     * @param data 更新するデータへのポインタ
     * @param rowPitch 1行あたりのバイト数
     * @param slicePitch 1スライスあたりのバイト数
     * @param mipLevel 更新するミップレベル
     * @param arrayIndex 更新する配列インデックス
     */
    virtual void Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel = 0, uint32_t arrayIndex = 0) = 0;
};

} // namespace NorvesLib::RHI