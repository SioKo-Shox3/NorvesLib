#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI 
{

/**
 * @brief パイプラインインターフェース
 * パイプラインはシェーダー、ステート、入出力レイアウト等を含むレンダリングの設定です。
 */
class IPipeline 
{
public:
    virtual ~IPipeline() = default;

    /**
     * @brief パイプラインがコンピュートパイプラインかどうか
     * @return コンピュートパイプラインの場合true、グラフィックパイプラインの場合false
     */
    virtual bool IsComputePipeline() const = 0;

    /**
     * @brief バインドポイント数の取得
     * @return バインドポイント（ディスクリプタセット）の数
     */
    virtual uint32_t GetBindPointCount() const = 0;
};

} // namespace NorvesLib::RHI