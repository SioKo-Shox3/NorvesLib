#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI 
{

/**
 * @brief サンプラーインターフェース
 * サンプラーはテクスチャからデータをサンプリングする方法を定義します。
 */
class ISampler 
{
public:
    virtual ~ISampler() = default;

    /**
     * @brief フィルタモード（最小化）を取得
     * @return 最小化フィルタモード
     */
    virtual FilterMode GetFilterMin() const = 0;

    /**
     * @brief フィルタモード（拡大）を取得
     * @return 拡大フィルタモード
     */
    virtual FilterMode GetFilterMag() const = 0;

    /**
     * @brief フィルタモード（ミップマップ）を取得
     * @return ミップマップフィルタモード
     */
    virtual FilterMode GetFilterMip() const = 0;

    /**
     * @brief テクスチャアドレスモード（U方向）を取得
     * @return U方向のテクスチャアドレスモード
     */
    virtual TextureAddressMode GetAddressModeU() const = 0;

    /**
     * @brief テクスチャアドレスモード（V方向）を取得
     * @return V方向のテクスチャアドレスモード
     */
    virtual TextureAddressMode GetAddressModeV() const = 0;

    /**
     * @brief テクスチャアドレスモード（W方向）を取得
     * @return W方向のテクスチャアドレスモード
     */
    virtual TextureAddressMode GetAddressModeW() const = 0;

    /**
     * @brief 最大異方性を取得
     * @return 最大異方性値
     */
    virtual uint32_t GetMaxAnisotropy() const = 0;

    /**
     * @brief 比較関数を取得
     * @return 比較関数
     */
    virtual CompareFunc GetCompareFunc() const = 0;
};

} // namespace NorvesLib::RHI