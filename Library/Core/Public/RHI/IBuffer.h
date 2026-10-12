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
     * @brief Map したポインタ（常にマップしたままのページなど）へ書いた範囲を明示する（明示した書き込みの借り）
     *
     * 開発ビルドの守られていない上書きの見張り（FrameResourceWriteTracker）が、Update と同じ書き込みとして数える。
     * Map で得たポインタへの任意の書き込みは、これを呼ばない限り見張りには見えない。Release では何もしない。
     * @param offset 書いた範囲の開始オフセット
     * @param size 書いた範囲の大きさ
     */
    virtual void NoteExplicitWrite(uint64_t offset, uint64_t size)
    {
        (void)offset;
        (void)size;
    }

    /**
     * @brief バッファの使用用途を取得
     * @return バッファの使用用途
     */
    virtual ResourceUsage GetUsage() const = 0;

    /**
     * @brief バッファのdevice addressを取得
     * @return 要求されていないか未対応の場合は0
     */
    virtual uint64_t GetDeviceAddress() const { return 0; }
};

} // namespace NorvesLib::RHI
