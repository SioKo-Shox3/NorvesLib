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
    class FrameResourceLane;

    /**
     * @brief 物体ごとの定数（UBO）と、それを binding 0 に結んだ記述子セットを 1 組ずつ借りる口
     *
     * GBufferPass・ForwardPass・ShadowMapPass が描画ごとに違う定数を書くために使う。
     *
     * 使い方:
     * 1. Initialize(device, uboSize, descriptorSetDesc) で、定数の大きさと記述子セットの束縛の形を決める
     *    （レイアウトは同じ形のパスと共有する 1 つをデバイスから引く）
     * 2. Execute の中で、描画ごとに Allocate(context.FrameResources) を呼ぶ。定数の切れ端と記述子セットを
     *    フレームの持ち主から借り、切れ端を binding 0 に結んで返す
     * 3. Write で定数を書き、残りの束縛を Bind* で書いて Update し、SetDescriptorSet（DrawCommand）に渡す
     *
     * 借りた物はそのフレームの回収まで動かず、上書きもされない。同じフレームで Execute が何回走っても、
     * 前の Execute の定数・記述子は書き換わらない。呼び出し側は飛行中のフレームの番号も、フレームの区切り
     * （Reset）も扱わない（パスを書く人向けの約束は Docs/Architecture/FrameResources.md）。
     *
     * フレームの持ち主が無い文脈（FrameResources が null。RenderingCoordinator を通さない検査など）では、
     * Allocate のたびに定数のバッファと記述子セットを新しく作って返す（使い回さないので上書きは起きない）。
     *
     * 事前確保の形（Initialize に maxSlots を渡し、Reset で先頭へ戻して使い回す形）は旧来の検査のためだけに
     * 残っている。前のフレームの GPU が読む定数・記述子を書き換えるので、パスからは使わない。
     */
    class DynamicUniformAllocator
    {
    public:
        /**
         * @brief 借りた結果
         */
        struct Allocation
        {
            /** 定数が入るバッファ（借りたときは持ち主のページ。Offset からの Size バイトが自分の分） */
            Container::TSharedPtr<RHI::IBuffer> UniformBuffer;
            Container::TSharedPtr<RHI::IDescriptorSet> DescriptorSet;
            uint64_t Offset = 0;
            uint32_t Size = 0;
            /** 借りたときだけ、書き込み先のマップしたままのアドレス（それ以外は nullptr） */
            void *CPUAddress = nullptr;
            /** 事前確保の形のスロットの番号（借りたとき・新しく作ったときは 0） */
            uint32_t SlotIndex = 0;

            bool IsValid() const { return UniformBuffer && DescriptorSet; }

            /**
             * @brief 定数を書く（size は Size 以下）
             *
             * 借りたときはマップしたままのページへ写す（フラッシュは要らない）。それ以外は IBuffer::Update で書く。
             */
            void Write(const void *data, uint32_t size) const;
        };

        DynamicUniformAllocator() = default;
        ~DynamicUniformAllocator() = default;

        /**
         * @brief 初期化（借りる形）
         * @param device RHIデバイス
         * @param uboSize 1 回に借りる定数の大きさ（バイト）
         * @param descriptorSetDesc 記述子セットの束縛の形（binding 0 は定数）
         * @return 初期化成功時true
         */
        bool Initialize(RHI::IDevice *device, uint32_t uboSize, const RHI::DescriptorSetDesc &descriptorSetDesc);

        /**
         * @brief 初期化（事前確保の形。旧来の検査のためだけに残す。パスからは使わない）
         * @param device RHIデバイス
         * @param uboSize 1スロットあたりのUBOサイズ（バイト）
         * @param maxSlots 最大スロット数
         * @param descriptorSetDesc DescriptorSetのレイアウト記述
         * @return 初期化成功時true
         */
        bool Initialize(RHI::IDevice *device, uint32_t uboSize, uint32_t maxSlots,
                        const RHI::DescriptorSetDesc &descriptorSetDesc);

        /**
         * @brief 事前確保の形で、足りなくなったらスロットを増やせるようにする
         *
         * Initialize 済みのあとに呼ぶ。Allocate() が事前確保のスロットを使い切ると、上限までスロットを足して返す
         * （足したスロットは Reset のあとも残る）。呼ばなければ事前確保の数が上限のまま。
         * @param hardLimit 増やしてよいスロット数の上限（事前確保の数以下なら増やさない）
         */
        void SetGrowthLimit(uint32_t hardLimit);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 事前確保の形のカーソルを先頭へ戻す（借りる形では何もしない）
         */
        void Reset();

        /**
         * @brief 定数の切れ端と記述子セットを 1 組借りる（借りる形。Execute の中で呼ぶ）
         *
         * 切れ端は記述子セットの binding 0 に結んである（Update はまだ。残りの束縛と一緒に呼び出し側が呼ぶ）。
         * frameResources が null なら新しく作って返す。借りられなかったときは IsValid() が false。
         * 自分の状態を変えないので、並列の記録の区間から同時に呼んでよい（区間ごとの口を渡す）。
         */
        Allocation Allocate(FrameResourceLane *frameResources) const;

        /**
         * @brief UBOスロットを1つ確保して返す（事前確保の形）
         * @return Allocation（無効な場合はバッファがnullptr）
         */
        Allocation Allocate();

        /**
         * @brief 初期化済みか
         */
        bool IsInitialized() const { return m_bInitialized; }

        /**
         * @brief 事前確保の形の残りスロット数を取得
         */
        uint32_t GetRemainingSlots() const { return m_MaxSlots - m_CurrentIndex; }

        /**
         * @brief 事前確保の形で今あるスロット数（事前確保と、増やした分の合計）
         */
        uint32_t GetSlotCount() const { return m_MaxSlots; }

    private:
        struct Slot
        {
            Container::TSharedPtr<RHI::IBuffer> UniformBuffer;
            Container::TSharedPtr<RHI::IDescriptorSet> DescriptorSet;
        };

        Allocation CreateUnowned() const;
        bool CreateSlot(uint32_t slotIndex);
        bool Grow();

        Container::VariableArray<Slot> m_Slots;
        RHI::IDevice *m_Device = nullptr;
        RHI::DescriptorSetDesc m_DescriptorSetDesc;
        // 借りる形で記述子セットを借りるときの共有のレイアウト（デバイスが共有のレイアウトに対応しないときは null）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        uint32_t m_GrowthLimit = 0;
        uint32_t m_MaxSlots = 0;
        uint32_t m_UBOSize = 0;
        uint32_t m_CurrentIndex = 0;
        bool m_bInitialized = false;
        bool m_bBorrowing = false;
    };

} // namespace NorvesLib::Core::Rendering
