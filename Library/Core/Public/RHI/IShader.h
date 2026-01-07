#pragma once

#include "RHITypes.h"
#include "Container/Containers.h"

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
    virtual NorvesLib::Core::Container::String GetEntryPoint() const = 0;

    /**
     * @brief シェーダーバイトコードを取得
     * @return シェーダーバイトコード
     */
    virtual const NorvesLib::Core::Container::VariableArray<uint8_t>& GetByteCode() const = 0;
};

} // namespace NorvesLib::RHI
