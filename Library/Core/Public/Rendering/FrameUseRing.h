#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cassert>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief FrameUseRing が区別できる飛行中のフレームの数の上限（スワップチェーンの飛行中のフレーム数はこれ以下でなければならない） */
    inline constexpr uint32_t FrameUseRingMaxInFlightSlots = 4;

    /**
     * @brief 1フレームに何回でも資源を使える、フレームごとの使い捨て資源のリング
     *
     * 計算パスやまとめた描画は、同じパスのインスタンスが 1 フレームに何回も Execute されうる
     * （同じ SceneView が複数のビューポートを描く、エディタの複数のビューポート・反射・キャプチャなど。
     * 回数に上限は無い）。UBO・ディスクリプタセット・ホストが書くバッファは、提出前や GPU が読み終わる前に
     * 上書きしてはならないので、Execute の回数ではなく「フレーム」で枠を決める。
     *
     * 飛行中のフレームの番号（ViewRenderContext::FrameIndex。スワップチェーンのフェンスが、同じ番号の前のフレームの
     * GPU の完了を待ってから返す）ごとに枠を持ち、枠の中の資源を Acquire のたびに次へ進めて渡す。
     * 枠の資源を使い切ったら足す（上限は無い）。同じ枠の使用済みの位置を戻すのは、フレームの通し番号が変わった
     * 最初の BeginFrame だけで、その時点で同じ番号の前のフレームの GPU の仕事は終わっている。
     * 1 フレームの間（通し番号が同じ間）に BeginFrame を何回呼んでも位置は戻らない。
     * 通し番号 0 は「未設定」の予約値で渡してはならない（フレームの境目が分からず、位置が戻らないまま資源が増え続ける）。
     * 通し番号を持たない呼び出し側は、渡す前に自分で 0 以外の代わりの番号を決める。
     *
     * 渡した資源の参照は、リングを Clear するまで有効（資源は個別に確保する）。
     * RenderThread だけから使う（排他は持たない）。
     */
    template <typename Use>
    class FrameUseRing
    {
    public:
        /** @brief 飛行中のフレームの番号を区別できる数。これ以上の番号は枠が重なって別のフレームの資源を上書きするので渡せない */
        static constexpr uint32_t MaxInFlightSlots = FrameUseRingMaxInFlightSlots;

        /**
         * @brief 枠を選ぶ。frameSerial が前回その枠を使ったときと違えば、使用済みの位置を戻す
         * @param inFlightIndex 飛行中のフレームの番号（スワップチェーンの現在のフレームの番号。MaxInFlightSlots 未満）
         * @param frameSerial フレームごとに必ず増える通し番号（同じフレームの間は同じ値。0 は渡せない）
         */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
        {
            assert(inFlightIndex < MaxInFlightSlots && "飛行中のフレームの番号が FrameUseRing の枠の数を超えた");
            assert(frameSerial != 0 && "FrameUseRing に未設定の通し番号 0 を渡した");
            m_ActiveSlot = inFlightIndex % MaxInFlightSlots;
            Slot& slot = m_Slots[m_ActiveSlot];
            if (!slot.bBegun || slot.FrameSerial != frameSerial)
            {
                slot.bBegun = true;
                slot.FrameSerial = frameSerial;
                slot.Cursor = 0;
            }
        }

        /** @brief 選んだ枠の次の資源。使い切っていれば既定の構築で足す。BeginFrame を呼ぶ前は枠 0 を使う */
        Use& Acquire()
        {
            Slot& slot = m_Slots[m_ActiveSlot];
            if (slot.Cursor >= slot.Uses.size())
            {
                slot.Uses.push_back(Container::MakeUnique<Use>());
            }
            return *slot.Uses[slot.Cursor++];
        }

        /** @brief 選んだ枠で、今のフレームにここまで渡した資源の数 */
        uint32_t GetUsedCount() const { return m_Slots[m_ActiveSlot].Cursor; }
        /** @brief 選んだ枠が持っている資源の数（使用済みの位置を戻しても減らない） */
        uint32_t GetCapacity() const { return static_cast<uint32_t>(m_Slots[m_ActiveSlot].Uses.size()); }
        uint32_t GetActiveSlot() const { return m_ActiveSlot; }

        /** @brief 全部の枠の資源を捨てて初期状態へ戻す */
        void Clear()
        {
            for (Slot& slot : m_Slots)
            {
                slot = Slot{};
            }
            m_ActiveSlot = 0;
        }

    private:
        struct Slot
        {
            Container::VariableArray<Container::TUniquePtr<Use>> Uses;
            uint64_t FrameSerial = 0;
            uint32_t Cursor = 0;
            bool bBegun = false;
        };

        Slot m_Slots[MaxInFlightSlots];
        uint32_t m_ActiveSlot = 0;
    };
} // namespace NorvesLib::Core::Rendering
