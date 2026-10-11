#pragma once

// 影を落とす物の境界を、ワールドの格子の範囲ごとの AABB にまとめて保つ（CSM の光源側の深度範囲に含める投影物）。
// 物を足す・動かす・外すと、その物の範囲だけを直す。毎フレームの深度範囲は物の数ではなく範囲の数だけを見る。

#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 影を落とす物の境界（球）を、格子の範囲ごとの AABB で保つ
     *
     * 物は呼び出し側が決めた番号（MegaGeometry の席など）で指す。範囲は球の中心が入る格子（RangeSizeMeters 角）。
     * 範囲が返す境界は、範囲の AABB の中心から、範囲の物の球をすべて含む半径の球（物が 1 つなら、その物の球そのもの）。
     * 物を外した・動かした範囲は、次に境界を引くとき残りの物から作り直す（その範囲の物の数だけ見る）。
     */
    class ShadowCasterRangeSet
    {
    public:
        /** @brief 範囲（格子）の一辺（m） */
        static constexpr float RangeSizeMeters = 16.0f;

        /** @brief 物と範囲をすべて捨てる */
        void Clear();

        /**
         * @brief 番号 id の物の境界を置く（前に置いた境界は置き換える）
         * @return 置いた（境界が有限で半径が正）なら true。置けない境界は、前に置いた物を外して false
         */
        bool Set(uint32_t id, const BoundingSphere& bounds);

        /** @brief 番号 id の物を外す（置いていなければ何もしない） */
        void Remove(uint32_t id);

        /** @brief 範囲ごとの境界を out へ足す（out は空にしない）。外した・動かした範囲はここで作り直す */
        void AppendBounds(Container::VariableArray<BoundingSphere>& out);

        /** @brief 置いている物の数 */
        uint32_t GetCount() const { return m_Count; }
        /** @brief 物のある範囲の数 */
        uint32_t GetRangeCount() const { return static_cast<uint32_t>(m_Ranges.size()); }

    private:
        struct Item
        {
            BoundingSphere Bounds;
            uint64_t RangeKey = 0;
            // 範囲の Members の中の位置
            uint32_t IndexInRange = 0;
            bool bPlaced = false;
        };

        struct Range
        {
            Container::VariableArray<uint32_t> Members;
            // 物の球を囲む AABB と、AABB の中心から物の球をすべて含む半径
            float Min[3] = {};
            float Max[3] = {};
            float Radius = 0.0f;
            // 物を外した・動かした・足した（AABB と半径を作り直す）
            bool bDirty = true;
        };

        static uint64_t MakeRangeKey(const BoundingSphere& bounds);
        void Rebuild(Range& range) const;

        Container::VariableArray<Item> m_Items;
        Container::UnorderedMap<uint64_t, Range> m_Ranges;
        uint32_t m_Count = 0;
    };
} // namespace NorvesLib::Core::Rendering
