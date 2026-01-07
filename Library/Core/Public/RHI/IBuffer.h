#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI 
{

/**
 * @brief バッファインターフェース
 * バッファはGPUメモリを管理する基本的なリソースです。
 * 頂点バッファ、インデックスバッファ、定数バッファなどに使用されます。
 */
class IBuffer 
{
public:
    virtual ~IBuffer() = default;

    /**
     * @brief バッファサイズを取得
     * @return バッファサイズ（バイト）
     */
    virtual uint64_t GetSize() const = 0;

    /**
     * @brief バッファのマッピング
     * CPUからアクセス可能なメモリにマッピングします。
     * @param offset マッピング開始位置
     * @param size マッピングサイズ。0の場合は全体をマッピング
     * @return マッピングされたメモリへのポインタ
     */
    virtual void* Map(uint64_t offset = 0, uint64_t size = 0) = 0;

    /**
     * @brief バッファのアンマッピング
     * Map関数でマッピングしたメモリを解放します。
     */
    virtual void Unmap() = 0;

    /**
     * @brief バッファのデータを更新
     * @param data 更新するデータへのポインタ
     * @param size データのサイズ
     * @param offset バッファ内の更新開始オフセット
     */
    virtual void Update(const void* data, uint64_t size, uint64_t offset = 0) = 0;

    /**
     * @brief バッファの使用用途を取得
     * @return バッファの使用用途
     */
    virtual ResourceUsage GetUsage() const = 0;
};

} // namespace NorvesLib::RHI
