#pragma once

#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief CPUアクセス可能なバッファをスロット別に保持し容量2倍リサイズ+遅延解放する汎用リング。
     *
     * frames-in-flight等のスロット単位で頂点/インデックス等の動的データをアップロードする用途に使う。
     * InstanceBufferRing と同じ構造（スロット別Buffer・容量2倍リサイズ・serialによる遅延解放）を
     * 用途固定でない汎用形へ一般化したもの。usage を引数化し、頂点バッファ/インデックスバッファ
     * いずれにも使える。特定機能（UIフレームワーク等）には依存しない。
     */
    class DynamicBufferRing
    {
    public:
        /**
         * @brief リングを初期化する。
         * @param device バッファ生成に使うデバイス
         * @param slotCount スロット数（通常 frames-in-flight）
         * @param usage 生成するバッファの用途（VertexBuffer / IndexBuffer 等）
         * @param initialBytes 各スロットの初期容量（バイト）
         * @return 成功すれば true
         */
        bool Initialize(RHI::IDevice *device, uint32_t slotCount, RHI::ResourceUsage usage, uint64_t initialBytes);

        /**
         * @brief リングを破棄する。
         */
        void Shutdown();

        /**
         * @brief 指定スロットへ data を bytes 分アップロードしバッファを返す。
         *
         * 容量不足なら2倍リサイズし、旧バッファは slotCount フレーム後に解放する。
         * @param slotIndex スロット番号
         * @param data アップロード元
         * @param bytes アップロードサイズ（バイト）
         * @return アップロード先バッファ（失敗時 nullptr）
         */
        RHI::BufferPtr Upload(uint32_t slotIndex, const void *data, uint64_t bytes);

    private:
        struct Slot
        {
            RHI::BufferPtr Buffer;
            uint64_t Capacity = 0;
        };

        struct DeferredRelease
        {
            RHI::BufferPtr Buffer;
            uint64_t ReleaseAfterSerial = 0;
        };

        RHI::BufferPtr CreateBuffer(uint64_t bytes) const;
        void ReleaseExpiredBuffers(uint64_t currentSerial);

        RHI::IDevice *m_Device = nullptr;
        RHI::ResourceUsage m_Usage = RHI::ResourceUsage::None;
        uint32_t m_SlotCount = 0;
        uint64_t m_UploadSerial = 0;
        Container::VariableArray<Slot> m_Slots;
        Container::VariableArray<DeferredRelease> m_DeferredReleases;
    };

} // namespace NorvesLib::Core::Rendering
