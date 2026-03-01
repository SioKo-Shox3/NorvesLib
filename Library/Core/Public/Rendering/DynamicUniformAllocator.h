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
        bool Initialize(RHI::IDevice* device, uint32_t uboSize, uint32_t maxSlots,
                        const RHI::DescriptorSetDesc& descriptorSetDesc);

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

    private:
        struct Slot
        {
            Container::TSharedPtr<RHI::IBuffer> UniformBuffer;
            Container::TSharedPtr<RHI::IDescriptorSet> DescriptorSet;
        };

        Container::VariableArray<Slot> m_Slots;
        uint32_t m_MaxSlots = 0;
        uint32_t m_UBOSize = 0;
        uint32_t m_CurrentIndex = 0;
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
