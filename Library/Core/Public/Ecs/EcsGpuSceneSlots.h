#pragma once

/**
 * @file EcsGpuSceneSlots.h
 * @brief MegaGeometry の席（GPU Scene の表の番号）の割り当てと、席の寿命の規則（設計 1.10）
 *
 * - 席は行の列 EcsGpuSceneSlot が持つ。行の作成（Create・ColumnAdded）で割り当て、理由が Destroy の削除でだけ解放する。
 *   行の移動・チャンクの返却・入れ替えでは解放しない（席は行と一緒に動き、前のフレームの履歴が席に付いたまま残る）。
 * - 空きリストは範囲単位。範囲は連続した席の組で、範囲の AABB は所属する全席の境界を包む。
 * - 解放した席は、次の 2 つがそろうまで再利用しない（待ちの一覧から使える一覧へ移さない）。
 *   (1) 削除の適用の確認: 解放を運ぶパケットの番号以上が RenderThread に適用された（世代が進んでいれば、写しごと入れ替わっている）
 *   (2) その席を最後に使った提出の完了: 解放を運ぶパケットの直前に送ったパケットまでが、GPU で完了した
 * - 解放を運ぶパケットの番号は、解放の時点では決めない。解放の後の最初の Advance（同期の先頭。SceneView へ削除を
 *   反映する前で、ゲートは直前に送ったパケットの番号を持つ）で、そのゲートの番号から決める。前の同期で取り込んだ
 *   ゲートは、その後に送ったパケットを知らない（古い番号で記録すると、席を含むパケットの完了を待たずに再利用してしまう）。
 * - 描画側（消費者）がいない間（ゲートが無効）は、解放した席を次の Advance で直ちに使える一覧へ移す。
 * - 散布（設計 1.11）は、連続した範囲の組（塊）を予約し、塊の中の席を先頭から順に割り当てる（BeginBlockAllocation）。
 *   塊の範囲は散布の専用で、解放した席を個別の実体の割り当てへ回さない。塊を返した後、所属する全席の再利用の条件が
 *   そろってから、次の予約が塊ごと使い直す（周回で席と範囲の数が増え続けない）。
 *
 * EcsWorld が 1 つ持ち、GameThread だけが触る（スレッドセーフではない）。
 */

#include "Container/Containers.h"
#include "Ecs/EcsSlotGate.h"
#include "Ecs/EcsTypes.h"
#include "Math/Vector3.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace NorvesLib::Core::Ecs
{
    /// 行の席。作った行は全部 0 なので、Value は「席の番号 + 1」とし、0 を「未割り当て」にする
    struct EcsGpuSceneSlot
    {
        uint32_t Value;
        /// 席が属する範囲の番号
        uint32_t Range;

        [[nodiscard]] bool IsAssigned() const
        {
            return Value != 0;
        }

        /// 席の番号（IsAssigned のときだけ有効）
        [[nodiscard]] uint32_t GetIndex() const
        {
            return Value - 1;
        }
    };

    /// 軽い行（Object なし）の MegaGeometry の値。列の組は EcsMegaInstanceColumns で作る
    struct EcsMegaInstance
    {
        uint32_t MeshIndex;
        uint32_t Flags;
        float Scale;
    };

    /// 軽い行の持ち主の散布（BatchId）
    struct EcsScatterOwner
    {
        uint32_t BatchId;
    };

    /// 軽い行の列の組: ワールドの変換・MegaGeometry の値・席・持ち主の散布
    inline void EcsMegaInstanceColumns(EcsColumnInfo (&out)[4])
    {
        out[0] = EcsRegisterColumn<EcsWorldTransform>();
        out[1] = EcsRegisterColumn<EcsMegaInstance>();
        out[2] = EcsRegisterColumn<EcsGpuSceneSlot>();
        out[3] = EcsRegisterColumn<EcsScatterOwner>();
    }

    /// 席の表と行の食い違いの報告（検査 MEGA_SLOTS）
    struct EcsGpuSceneSlotAudit
    {
        /// 席の列を持つ行の数
        uint32_t Rows = 0;
        /// 席が未割り当ての行
        uint32_t UnassignedRows = 0;
        /// 同じ席を 2 つの行が持つ
        uint32_t DuplicateSlots = 0;
        /// 行の席が、表で使用中でない・範囲が食い違う
        uint32_t UnknownSlots = 0;
        /// 表で使用中なのに、持つ行が無い席の数（行を持たない席の差）
        uint32_t OrphanLiveSlots = 0;
        /// 範囲の AABB が、所属する席の境界を包まない席の数
        uint32_t ContainmentMisses = 0;
        /// 解放の条件がそろう前に再利用した席の数（累計）
        uint64_t PrematureReuse = 0;
        /// 使用中でない席の二重解放・未割り当ての解放の数（累計）
        uint64_t InvalidReleases = 0;
        /// 理由が Destroy でない列の除去・作成時から値が入っていた行の数（累計）
        uint64_t RuleViolations = 0;
        char FirstDetail[200] = {};

        [[nodiscard]] uint32_t Violations() const
        {
            return UnassignedRows + DuplicateSlots + UnknownSlots + OrphanLiveSlots + ContainmentMisses +
                   static_cast<uint32_t>(PrematureReuse + InvalidReleases + RuleViolations);
        }

        void Note(const char *text)
        {
            if (FirstDetail[0] == '\0')
            {
                std::snprintf(FirstDetail, sizeof(FirstDetail), "%s", text);
            }
        }
    };

    /**
     * @brief 席の割り当てと、範囲ごとの空きリスト・AABB・再利用の待ち
     */
    class EcsGpuSceneSlotTable
    {
    public:
        /// 個別の実体の席を取る範囲の大きさ（席の数）
        static constexpr uint32_t kGeneralRangeSlots = 64;
        static constexpr uint32_t kInvalidRange = 0xFFFFFFFFu;
        static constexpr uint32_t kInvalidBlock = 0xFFFFFFFFu;

        /// 席を 1 つ割り当てる。使える空き（再利用の条件がそろった席）を先に使い、無ければ範囲の未使用の末尾、無ければ新しい範囲。
        /// 塊の割り当て中（BeginBlockAllocation）は、塊の席を先頭から順に使う
        EcsGpuSceneSlot Allocate()
        {
            if (m_AllocBlock != kInvalidBlock)
            {
                const EcsGpuSceneSlot blockSlot = AllocateFromBlock(m_Blocks[m_AllocBlock]);
                if (blockSlot.IsAssigned())
                {
                    return blockSlot;
                }
                // 塊を使い切った（予約より多く作った）。個別の実体の割り当てへ回す
            }
            uint32_t rangeIndex = kInvalidRange;
            uint32_t slot = 0;
            bool bReused = false;
            while (!m_ReusableRanges.empty())
            {
                const uint32_t candidate = m_ReusableRanges.back();
                if (m_Ranges[candidate].Ready.empty())
                {
                    m_Ranges[candidate].bQueued = false;
                    m_ReusableRanges.pop_back();
                    continue;
                }
                rangeIndex = candidate;
                break;
            }
            if (rangeIndex != kInvalidRange)
            {
                Range &range = m_Ranges[rangeIndex];
                slot = range.Ready.back();
                range.Ready.pop_back();
                bReused = true;
            }
            else
            {
                if (m_ActiveRange == kInvalidRange || m_Ranges[m_ActiveRange].NextUnused >= m_Ranges[m_ActiveRange].Count)
                {
                    m_ActiveRange = CreateRange(kGeneralRangeSlots);
                }
                rangeIndex = m_ActiveRange;
                Range &range = m_Ranges[rangeIndex];
                slot = range.First + range.NextUnused;
                ++range.NextUnused;
            }

            Range &range = m_Ranges[rangeIndex];
            if (bReused && m_bAudit && m_Gate.bValid && !IsReleasable(m_Audit[slot].Release, m_Gate))
            {
                ++m_PrematureReuse;
            }
            if (bReused)
            {
                ++m_Reused;
                if (m_bAudit)
                {
                    // 前の持ち主の境界は引き継がない
                    m_Audit[slot].bBounds = false;
                }
            }
            m_State[slot] = SlotState::Live;
            ++range.Live;
            ++m_LiveCount;
            ++m_Allocated;
            return EcsGpuSceneSlot{slot + 1, rangeIndex};
        }

        /// 席を解放する（理由が Destroy の削除でだけ呼ぶ）。使用中でない席・未割り当ての席は false で、数えて何もしない
        bool Release(const EcsGpuSceneSlot &slot)
        {
            if (!slot.IsAssigned())
            {
                ++m_InvalidReleases;
                return false;
            }
            const uint32_t index = slot.GetIndex();
            if (index >= m_SlotCount || m_State[index] != SlotState::Live || m_SlotRange[index] != slot.Range)
            {
                ++m_InvalidReleases;
                return false;
            }
            Range &range = m_Ranges[slot.Range];
            --range.Live;
            --m_LiveCount;
            ++m_Released;
            // 解放を運ぶパケットの番号は次の Advance で決める（それまでは再利用の条件を判定しない）
            PendingEntry entry;
            entry.Slot = index;
            entry.Range = slot.Range;
            m_State[index] = SlotState::Pending;
            ++range.Pending;
            m_Pending.push_back(entry);
            return true;
        }

        /// 描画側の進み具合を取り込み、再利用の条件がそろった席を使える一覧へ移す。ゲートが無効なら待ちを全部移す。
        /// 同期の先頭（SceneView へ削除を反映する前）で、最後に送ったパケットの番号を持つゲートを渡す
        void Advance(const EcsSlotGate &gate)
        {
            m_Gate = gate;
            ++m_AdvanceCount;
            if (gate.bValid)
            {
                // 前の Advance の後に解放した席へ、解放を運ぶパケットの番号を記録する。
                // 削除はこの同期で SceneView へ反映されるので、運ぶのは次に送るパケット（LastSentSerial + 1）で、
                // 席を最後に使いうるのは送り終えた最後のパケット（LastSentSerial）
                for (size_t index = m_StampedEnd; index < m_Pending.size(); ++index)
                {
                    PendingEntry &entry = m_Pending[index];
                    entry.Release.Generation = gate.SceneGeneration;
                    entry.Release.ReleaseSerial = gate.LastSentSerial + 1;
                    entry.Release.LastUsedSerial = gate.LastSentSerial;
                    if (m_bAudit)
                    {
                        m_Audit[entry.Slot].Release = entry.Release;
                    }
                }
                m_StampedEnd = m_Pending.size();
            }
            while (m_PendingHead < m_Pending.size())
            {
                const PendingEntry &entry = m_Pending[m_PendingHead];
                if (gate.bValid && !IsReleasable(entry.Release, gate))
                {
                    break;
                }
                --m_Ranges[entry.Range].Pending;
                if (!gate.bValid && m_bAudit)
                {
                    // 消費者がいなくなった（次の消費者は全量から始める）ので、解放の記録は意味を持たない
                    m_Audit[entry.Slot].Release = ReleaseRecord{};
                }
                MakeReady(entry.Slot, entry.Range);
                ++m_PendingHead;
            }
            if (m_PendingHead == m_Pending.size())
            {
                m_Pending.clear();
                m_PendingHead = 0;
            }
            else if (m_PendingHead >= 4096 && m_PendingHead * 2 >= m_Pending.size())
            {
                m_Pending.erase(m_Pending.begin(), m_Pending.begin() + static_cast<std::ptrdiff_t>(m_PendingHead));
                m_PendingHead = 0;
            }
            // ゲートが有効なら全部記録済み、無効なら全部移し終えている
            m_StampedEnd = m_Pending.size();
        }

        /**
         * @brief 散布の塊（連続した範囲の組）を予約する
         * @param slotCount 席の数
         * @param rangeSlots 1 つの範囲の席の数
         * @param outFirstRange 塊の先頭の範囲の番号（範囲の番号は塊の中で連続し、席の番号も連続する）
         * @param outRangeCount 塊の範囲の数（slotCount を rangeSlots で割り上げた数）
         * @return 塊の番号（slotCount か rangeSlots が 0 なら kInvalidBlock）
         *
         * 返した塊のうち、所属する全席の再利用の条件がそろった（使用中も解放待ちも無い）同じ大きさ以上の塊があれば使い直す。
         * 無ければ新しい範囲を末尾に作る
         */
        uint32_t ReserveBlock(uint32_t slotCount, uint32_t rangeSlots, uint32_t &outFirstRange, uint32_t &outRangeCount)
        {
            outFirstRange = kInvalidRange;
            outRangeCount = 0;
            if (slotCount == 0 || rangeSlots == 0)
            {
                return kInvalidBlock;
            }
            const uint32_t rangeCount = (slotCount + rangeSlots - 1) / rangeSlots;
            uint32_t chosen = kInvalidBlock;
            for (uint32_t index = 0; index < m_Blocks.size(); ++index)
            {
                const Block &block = m_Blocks[index];
                if (block.bInUse || block.RangeSlots != rangeSlots || block.RangeCount < rangeCount || !IsBlockIdle(block))
                {
                    continue;
                }
                // 余りの最も少ない塊を使う
                if (chosen == kInvalidBlock || block.RangeCount < m_Blocks[chosen].RangeCount)
                {
                    chosen = index;
                }
            }
            if (chosen == kInvalidBlock)
            {
                Block block;
                block.RangeSlots = rangeSlots;
                block.RangeCount = rangeCount;
                block.FirstRange = static_cast<uint32_t>(m_Ranges.size());
                for (uint32_t range = 0; range < rangeCount; ++range)
                {
                    const uint32_t created = CreateRange(rangeSlots);
                    m_Ranges[created].bDedicated = true;
                }
                chosen = static_cast<uint32_t>(m_Blocks.size());
                m_Blocks.push_back(block);
            }
            Block &block = m_Blocks[chosen];
            block.bInUse = true;
            block.Cursor = 0;
            for (uint32_t range = 0; range < block.RangeCount; ++range)
            {
                Range &target = m_Ranges[block.FirstRange + range];
                target.NextUnused = 0;
                target.bBounds = false;
            }
            outFirstRange = block.FirstRange;
            outRangeCount = rangeCount;
            return chosen;
        }

        /// 以後の Allocate（行の作成のフック）を、塊の席から先頭の順に取る。EndBlockAllocation まで
        void BeginBlockAllocation(uint32_t block)
        {
            m_AllocBlock = block < m_Blocks.size() && m_Blocks[block].bInUse ? block : kInvalidBlock;
        }

        void EndBlockAllocation()
        {
            m_AllocBlock = kInvalidBlock;
        }

        /// 塊を返す。所属する席は行の削除（理由が Destroy）で先に解放しておく。塊は全席の再利用の条件がそろってから使い直される
        void ReleaseBlock(uint32_t block)
        {
            if (block >= m_Blocks.size() || !m_Blocks[block].bInUse)
            {
                ++m_InvalidReleases;
                return;
            }
            m_Blocks[block].bInUse = false;
            if (m_AllocBlock == block)
            {
                m_AllocBlock = kInvalidBlock;
            }
        }

        /// 使っている塊の数
        [[nodiscard]] uint32_t GetBlocksInUse() const
        {
            uint32_t count = 0;
            for (const Block &block : m_Blocks)
            {
                count += block.bInUse ? 1u : 0u;
            }
            return count;
        }

        [[nodiscard]] uint32_t GetBlockCount() const
        {
            return static_cast<uint32_t>(m_Blocks.size());
        }

        /// 席の境界を覚え、所属する範囲の AABB をそれを包むところまで広げる
        void NoteBounds(const EcsGpuSceneSlot &slot, const Math::Vector3 &boundsMin, const Math::Vector3 &boundsMax)
        {
            if (!slot.IsAssigned() || slot.GetIndex() >= m_SlotCount || slot.Range >= m_Ranges.size())
            {
                return;
            }
            Range &range = m_Ranges[slot.Range];
            if (!range.bBounds)
            {
                range.Min = boundsMin;
                range.Max = boundsMax;
                range.bBounds = true;
            }
            else
            {
                range.Min = Math::Vector3(std::min(range.Min.x, boundsMin.x), std::min(range.Min.y, boundsMin.y),
                                          std::min(range.Min.z, boundsMin.z));
                range.Max = Math::Vector3(std::max(range.Max.x, boundsMax.x), std::max(range.Max.y, boundsMax.y),
                                          std::max(range.Max.z, boundsMax.z));
            }
            if (m_bAudit)
            {
                AuditSlot &audit = m_Audit[slot.GetIndex()];
                audit.Min = boundsMin;
                audit.Max = boundsMax;
                audit.bBounds = true;
            }
        }

        /// 範囲の AABB。境界がまだ無ければ false
        [[nodiscard]] bool GetRangeBounds(uint32_t range, Math::Vector3 &outMin, Math::Vector3 &outMax) const
        {
            if (range >= m_Ranges.size() || !m_Ranges[range].bBounds)
            {
                return false;
            }
            outMin = m_Ranges[range].Min;
            outMax = m_Ranges[range].Max;
            return true;
        }

        /// 席ごとの履歴（解放の条件・境界）を持つ。検査 MEGA_SLOTS が有効なときだけ呼ぶ。
        /// すでにある席の履歴は空から始まる（境界が無い席は包含の照合から外れる）
        void SetAuditEnabled(bool bEnabled)
        {
            if (bEnabled && !m_bAudit)
            {
                m_bAudit = true;
                m_Audit.resize(m_SlotCount);
            }
        }

        [[nodiscard]] bool IsAuditEnabled() const
        {
            return m_bAudit;
        }

        /// 席の列に Destroy 以外の理由で除去された、または作成時から値が入っていたことを数える（規則の違反）
        void NoteRuleViolation()
        {
            ++m_RuleViolations;
        }

        /// 範囲の AABB が所属する全席（使用中と解放待ち）の境界を包むかを調べ、外れた席の数を返す。境界の履歴が要る（SetAuditEnabled）
        uint32_t AuditContainment(EcsGpuSceneSlotAudit &report) const
        {
            if (!m_bAudit)
            {
                return 0;
            }
            uint32_t misses = 0;
            for (uint32_t slot = 0; slot < m_SlotCount; ++slot)
            {
                const SlotState state = m_State[slot];
                const AuditSlot &audit = m_Audit[slot];
                if ((state != SlotState::Live && state != SlotState::Pending) || !audit.bBounds)
                {
                    continue;
                }
                const Range &range = m_Ranges[m_SlotRange[slot]];
                const bool bContained = range.bBounds && range.Min.x <= audit.Min.x && range.Min.y <= audit.Min.y &&
                                        range.Min.z <= audit.Min.z && range.Max.x >= audit.Max.x &&
                                        range.Max.y >= audit.Max.y && range.Max.z >= audit.Max.z;
                if (!bContained)
                {
                    ++misses;
                    char text[120];
                    std::snprintf(text, sizeof(text), "範囲 %u の AABB が席 %u の境界を包まない", m_SlotRange[slot], slot);
                    report.Note(text);
                }
            }
            return misses;
        }

        /// 席が使用中か
        [[nodiscard]] bool IsLive(uint32_t slot) const
        {
            return slot < m_SlotCount && m_State[slot] == SlotState::Live;
        }

        /// 席が属する範囲（席の番号が表の外なら kInvalidRange）
        [[nodiscard]] uint32_t GetRangeOf(uint32_t slot) const
        {
            return slot < m_SlotCount ? m_SlotRange[slot] : kInvalidRange;
        }

        [[nodiscard]] uint32_t GetSlotCount() const
        {
            return m_SlotCount;
        }

        [[nodiscard]] uint32_t GetRangeCount() const
        {
            return static_cast<uint32_t>(m_Ranges.size());
        }

        /// 使用中の席の数
        [[nodiscard]] uint32_t GetLiveCount() const
        {
            return m_LiveCount;
        }

        /// 再利用の条件を待っている席の数
        [[nodiscard]] size_t GetPendingCount() const
        {
            return m_Pending.size() - m_PendingHead;
        }

        /// 条件がそろい、再利用を待っている空きの数
        [[nodiscard]] size_t GetReadyCount() const
        {
            size_t total = 0;
            for (const Range &range : m_Ranges)
            {
                total += range.Ready.size();
            }
            return total;
        }

        [[nodiscard]] uint64_t GetAllocatedCount() const
        {
            return m_Allocated;
        }

        [[nodiscard]] uint64_t GetReleasedCount() const
        {
            return m_Released;
        }

        /// 解放した席を再割り当てした回数
        [[nodiscard]] uint64_t GetReusedCount() const
        {
            return m_Reused;
        }

        /// 条件がそろう前に再利用した回数（履歴が要る）
        [[nodiscard]] uint64_t GetPrematureReuseCount() const
        {
            return m_PrematureReuse;
        }

        [[nodiscard]] uint64_t GetInvalidReleaseCount() const
        {
            return m_InvalidReleases;
        }

        [[nodiscard]] uint64_t GetRuleViolationCount() const
        {
            return m_RuleViolations;
        }

        [[nodiscard]] const EcsSlotGate &GetGate() const
        {
            return m_Gate;
        }

        /// Advance を呼んだ回数（検査の間引きに使う）
        [[nodiscard]] uint64_t GetAdvanceCount() const
        {
            return m_AdvanceCount;
        }

    private:
        enum class SlotState : uint8_t
        {
            Unused = 0,
            Live,
            Pending,
            Ready,
        };

        /// 席の解放の記録（再利用の条件に使う）
        struct ReleaseRecord
        {
            /// 解放したときのシーンの世代
            uint64_t Generation = 0;
            /// 解放を運ぶパケットの番号（解放の後の最初の Advance で、送り終えていた番号 + 1）
            uint64_t ReleaseSerial = 0;
            /// その席を最後に使いうる、解放を運ぶパケットの直前に送ったパケットの番号
            uint64_t LastUsedSerial = 0;
        };

        struct PendingEntry
        {
            uint32_t Slot = 0;
            uint32_t Range = 0;
            ReleaseRecord Release;
        };

        struct Range
        {
            uint32_t First = 0;
            uint32_t Count = 0;
            /// 未使用の末尾の先頭（First からの位置）
            uint32_t NextUnused = 0;
            uint32_t Live = 0;
            uint32_t Pending = 0;
            /// 使える空きの一覧に入っているか
            bool bQueued = false;
            /// 散布の塊の範囲（解放した席を個別の実体の割り当てへ回さない）
            bool bDedicated = false;
            bool bBounds = false;
            Math::Vector3 Min;
            Math::Vector3 Max;
            /// 再利用の条件がそろった席
            Container::VariableArray<uint32_t> Ready;
        };

        /// 散布の塊: 連続した範囲の組
        struct Block
        {
            uint32_t FirstRange = 0;
            uint32_t RangeCount = 0;
            uint32_t RangeSlots = 0;
            /// 次に席を取る範囲（FirstRange からの位置）
            uint32_t Cursor = 0;
            bool bInUse = false;
        };

        struct AuditSlot
        {
            Math::Vector3 Min;
            Math::Vector3 Max;
            bool bBounds = false;
            ReleaseRecord Release;
        };

        /// 削除の適用の確認と、最後に使った提出の完了の両方がそろったか
        static bool IsReleasable(const ReleaseRecord &release, const EcsSlotGate &gate)
        {
            const bool bApplied = gate.ConfirmedGeneration > release.Generation ||
                                  (gate.ConfirmedGeneration == release.Generation &&
                                   gate.ConfirmedSerial >= release.ReleaseSerial);
            const bool bCompleted = gate.CompletedSerial >= release.LastUsedSerial;
            return bApplied && bCompleted;
        }

        /// 塊の全範囲に、使用中の席も解放待ちの席も無いか
        bool IsBlockIdle(const Block &block) const
        {
            for (uint32_t range = 0; range < block.RangeCount; ++range)
            {
                const Range &target = m_Ranges[block.FirstRange + range];
                if (target.Live != 0 || target.Pending != 0)
                {
                    return false;
                }
            }
            return true;
        }

        /// 塊の席を先頭から順に 1 つ割り当てる。使い切っていれば未割り当てを返す
        EcsGpuSceneSlot AllocateFromBlock(Block &block)
        {
            while (block.Cursor < block.RangeCount)
            {
                const uint32_t rangeIndex = block.FirstRange + block.Cursor;
                Range &range = m_Ranges[rangeIndex];
                if (range.NextUnused >= range.Count)
                {
                    ++block.Cursor;
                    continue;
                }
                const uint32_t slot = range.First + range.NextUnused;
                ++range.NextUnused;
                // 前に塊を使った散布が解放した席なら再利用（塊は全席の条件がそろってから予約し直すので、早すぎることはない）
                if (m_State[slot] == SlotState::Ready)
                {
                    if (m_bAudit && m_Gate.bValid && !IsReleasable(m_Audit[slot].Release, m_Gate))
                    {
                        ++m_PrematureReuse;
                    }
                    ++m_Reused;
                    if (m_bAudit)
                    {
                        m_Audit[slot].bBounds = false;
                    }
                }
                m_State[slot] = SlotState::Live;
                ++range.Live;
                ++m_LiveCount;
                ++m_Allocated;
                return EcsGpuSceneSlot{slot + 1, rangeIndex};
            }
            return EcsGpuSceneSlot{0, 0};
        }

        uint32_t CreateRange(uint32_t slotCount)
        {
            Range range;
            range.First = m_SlotCount;
            range.Count = slotCount;
            const uint32_t index = static_cast<uint32_t>(m_Ranges.size());
            m_SlotCount += slotCount;
            m_State.resize(m_SlotCount, SlotState::Unused);
            m_SlotRange.resize(m_SlotCount, index);
            if (m_bAudit)
            {
                m_Audit.resize(m_SlotCount);
            }
            m_Ranges.push_back(std::move(range));
            return index;
        }

        /// 席を使える空きにする。範囲に使用中も解放待ちも無くなったら、範囲の AABB を捨てる
        void MakeReady(uint32_t slot, uint32_t rangeIndex)
        {
            Range &range = m_Ranges[rangeIndex];
            m_State[slot] = SlotState::Ready;
            if (range.bDedicated)
            {
                // 散布の塊の席は、塊を予約し直すまで使わない
                if (range.Live == 0 && range.Pending == 0)
                {
                    range.bBounds = false;
                }
                return;
            }
            range.Ready.push_back(slot);
            if (!range.bQueued)
            {
                range.bQueued = true;
                m_ReusableRanges.push_back(rangeIndex);
            }
            if (range.Live == 0 && range.Pending == 0)
            {
                range.bBounds = false;
            }
        }

        Container::VariableArray<Range> m_Ranges;
        Container::VariableArray<SlotState> m_State;
        Container::VariableArray<uint32_t> m_SlotRange;
        Container::VariableArray<AuditSlot> m_Audit;
        /// 使える空きを持つ範囲（末尾から使う）
        Container::VariableArray<uint32_t> m_ReusableRanges;
        /// 解放の順（番号は増える一方なので、条件も先頭から順にそろう）
        Container::VariableArray<PendingEntry> m_Pending;
        size_t m_PendingHead = 0;
        /// m_Pending のうち、解放を運ぶパケットの番号を記録済みの末尾（ここから後ろは次の Advance で記録する）
        size_t m_StampedEnd = 0;
        EcsSlotGate m_Gate;
        uint32_t m_ActiveRange = kInvalidRange;
        Container::VariableArray<Block> m_Blocks;
        /// 割り当て中の塊（BeginBlockAllocation）
        uint32_t m_AllocBlock = kInvalidBlock;
        uint32_t m_SlotCount = 0;
        uint32_t m_LiveCount = 0;
        uint64_t m_Allocated = 0;
        uint64_t m_Released = 0;
        uint64_t m_Reused = 0;
        uint64_t m_PrematureReuse = 0;
        uint64_t m_InvalidReleases = 0;
        uint64_t m_RuleViolations = 0;
        uint64_t m_AdvanceCount = 0;
        bool m_bAudit = false;
    };
}
