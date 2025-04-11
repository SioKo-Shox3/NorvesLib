#pragma once

#include "RHITypes.h"
#include <vector>

namespace NorvesLib::RHI 
{

/**
 * @brief シェーダーインターフェース
 * シェーダーはGPUで実行されるプログラムです。
 */
class IShader 
{
public:
    virtual ~IShader() = default;

    /**
     * @brief シェーダーステージを取得
     * @return シェーダーステージ
     */
    virtual ShaderStage GetStage() const = 0;

    /**
     * @brief エントリーポイントを取得
     * @return エントリーポイント名
     */
    virtual std::string GetEntryPoint() const = 0;

    /**
     * @brief シェーダーバイトコードを取得
     * @return シェーダーバイトコード
     */
    virtual const std::vector<uint8_t>& GetByteCode() const = 0;
};

} // namespace NorvesLib::RHI