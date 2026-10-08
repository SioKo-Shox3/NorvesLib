#pragma once

#include "RHI/RHITypes.h"
#include "RHI/IDescriptorSet.h"
#include "Container/PointerTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class IBuffer;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief 動的ユニフォームバッファアロケータ
     *
     * GBufferPass等でPerObjectごとに異なるUBOデータを書き込むための
     * フレーム単位リングバッファ方式のアロケータ。
     *
     * 使い方:
     * 1. フレーム開始時に Reset() を呼ぶ
     * 2. 各オブジェクト描画前に Allocate() で UBO+DescriptorSet を取得
     * 3. UBOにデータを書き込み、DescriptorSetをバインドして描画
     *
     * 内部で複数のUBOスロットとDescriptorSetを事前確保し、
     * Allocateのたびにインデックスを進めて返します。
     */
    class DynamicUniformAllocator
    {
    public:
        /**
         * @brief アロケーション結果
         */
        struct Allocation
        {
            Container::TSharedPtr<RHI::IBuffer> UniformBuffer;
            Container::TSharedPtr<RHI::IDescriptorSet> DescriptorSet;
            uint32_t SlotIndex = 0;
        };

        DynamicUniformAllocator() = default;
        ~DynamicUniformAllocator() = default;

        /**
         * @brief 初期化
         * @param device RHIデバイス
         * @param uboSize 1スロットあたりのUBOサイズ（バイト）
         * @param maxSlots 最大スロット数
         * @param descriptorSetDesc DescriptorSetのレイアウト記述
         * @return 初期化成功時true
         */
        bool Initialize(RHI::IDevice *device, uint32_t uboSize, uint32_t maxSlots,
                        const RHI::DescriptorSetDesc &descriptorSetDesc);

        /**
         * @brief 足りなくなったらスロットを増やせるようにする
         *
         * Initialize 済みのあとに呼ぶ。Allocate が事前確保のスロットを使い切ると、上限までスロットを足して返す
         * （足したスロットは Reset のあとも残る）。呼ばなければ事前確保の数が上限のまま。
         * @param hardLimit 増やしてよいスロット数の上限（事前確保の数以下なら増やさない）
         */
        void SetGrowthLimit(uint32_t hardLimit);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief フレーム開始時にカーソルをリセット
         */
        void Reset();

        /**
         * @brief UBOスロットを1つ確保して返す
         * @return Allocation（無効な場合はバッファがnullptr）
         */
        Allocation Allocate();

        /**
         * @brief 初期化済みか
         */
        bool IsInitialized() const { return m_bInitialized; }

        /**
         * @brief 残りスロット数を取得
         */
        uint32_t GetRemainingSlots() const { return m_MaxSlots - m_CurrentIndex; }

        /**
         * @brief 今あるスロット数（事前確保と、増やした分の合計）
         */
        uint32_t GetSlotCount() const { return m_MaxSlots; }

    private:
        struct Slot
        {
            Container::TSharedPtr<RHI::IBuffer> UniformBuffer;
            Container::TSharedPtr<RHI::IDescriptorSet> DescriptorSet;
        };

        bool CreateSlot(uint32_t slotIndex);
        bool Grow();

        Container::VariableArray<Slot> m_Slots;
        RHI::IDevice *m_Device = nullptr;
        RHI::DescriptorSetDesc m_DescriptorSetDesc;
        uint32_t m_GrowthLimit = 0;
        uint32_t m_MaxSlots = 0;
        uint32_t m_UBOSize = 0;
        uint32_t m_CurrentIndex = 0;
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
