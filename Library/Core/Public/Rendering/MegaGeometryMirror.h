// MegaGeometry の proxy 配列を、GameThread から RenderThread へ差分で運ぶための送り手と写し。
// 送り手（GameThread）は SceneView の配列の変化した位置だけをパケットへ積み、確認されていない送信の分も積み直す。
// 写し（RenderThread）は適用したパケットの差分を当てて、GameThread の配列と同じ並びの配列を持つ。
#pragma once

#include "Rendering/FramePacket.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneProxyHash.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    /** @brief proxy 配列の内容の hash（検査用。並びに依存する。前の変換と席は含めない） */
    inline uint64_t HashMegaGeometryContent(const Container::VariableArray<MegaGeometryProxy>& proxies)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const MegaGeometryProxy& proxy : proxies)
        {
            MegaGeometryProxy normalized = proxy;
            normalized.PreviousWorldTransform = normalized.WorldTransform;
            hash = (hash ^ HashProxy(normalized)) * 1099511628211ull;
        }
        return hash ^ static_cast<uint64_t>(proxies.size());
    }

    /**
     * @brief RenderThread の MegaGeometry の写し
     *
     * 適用したパケットの差分（MegaSceneDelta）を当て、GameThread の SceneView と同じ並びの配列を持つ。
     * 消費者（MegaGeometryPass・ShadowMapPass）はこの配列を読み、変わった席（SceneSlot）の一覧で GPU の表を更新できる。
     * 描画するスレッドだけが触る。
     */
    class MegaGeometryMirror
    {
    public:
        void Reset()
        {
            m_Proxies.clear();
            m_ChangedSlots.clear();
            m_PositionBySlot.clear();
            m_bAllChanged = false;
            m_bChangedConsumed = true;
            m_ChangedBaseRevision = 0;
            m_Rebased.clear();
            m_DrawnWorlds.clear();
            m_PendingPositions.clear();
            m_PendingRemoved.clear();
            m_bPendingAll = false;
            m_Generation = 0;
            m_Revision = 0;
            m_RejectedCount = 0;
            m_LastEntryCount = 0;
            m_ConsumerChecked.Store(0);
            m_VerifyChecked.Store(0);
            m_VerifyMismatched.Store(0);
        }

        /**
         * @brief 差分を当てる
         * @param delta 適用したパケットの差分
         * @param sceneGeneration そのパケットのシーンの世代
         * @return 当てたとき true。世代が合わない差分（基準でない）は当てずに false
         *
         * 前の変換（PreviousWorldTransform）は GameThread が書いた値のまま持つ。描画がゲームのフレームを飛ばしたときの
         * 付け替えは、描くときに NoteDrawn が行う（当てただけで描かなかった差分では、描いた変換の履歴を動かさない）。
         * 前のフレームの付け替えは、ここで GameThread の値へ戻す（付け替えは描いたフレームだけ）。
         *
         * 変わった席は、消費者が取り込む（NoteDrawn の後に読む）まで溜める。描画を見送った差分や空の差分を挟んでも、
         * 消費者が前に見た版から今までの変わった席を取り逃がさない。
         */
        bool Apply(const MegaSceneDelta& delta, uint64_t sceneGeneration)
        {
            BeginChangeAccumulation();
            SettleRebased();
            m_LastEntryCount = 0;
            if (!delta.bActive)
            {
                return false;
            }
            if (delta.bBaseline)
            {
                m_Proxies.clear();
                m_PositionBySlot.clear();
                m_Generation = sceneGeneration;
                m_bAllChanged = true;
                MarkPendingAll();
            }
            else if (m_Generation != sceneGeneration)
            {
                ++m_RejectedCount;
                return false;
            }

            const size_t sizeBefore = delta.bBaseline ? 0 : m_Proxies.size();

            // 末尾を切る（外した proxy の席は変わった席として知らせる）
            if (delta.Size < m_Proxies.size())
            {
                for (size_t index = delta.Size; index < m_Proxies.size(); ++index)
                {
                    const MegaGeometryProxy& removed = m_Proxies[index];
                    NoteChangedSlot(removed.SceneSlot);
                    UnmapSlot(removed.SceneSlot, static_cast<uint32_t>(index));
                    NotePendingRemoved(removed.ObjectId);
                }
                m_Proxies.resize(delta.Size);
            }
            else if (delta.Size > m_Proxies.size())
            {
                m_Proxies.resize(delta.Size);
            }

            // 1 周目: 置き換わる前の中身の席・描いた履歴の印を片付ける（移った proxy の席が、先に別の位置から外れるため 2 周に分ける）
            const size_t count = std::min(delta.Indices.size(), delta.Proxies.size());
            for (size_t entry = 0; entry < count; ++entry)
            {
                const uint32_t index = delta.Indices[entry];
                if (index >= m_Proxies.size())
                {
                    continue;
                }
                const MegaGeometryProxy& incoming = delta.Proxies[entry];
                // 増えた位置の中身は未使用の入れ物で、前の席を持たない
                if (index < sizeBefore)
                {
                    const MegaGeometryProxy& old = m_Proxies[index];
                    // 位置の中身が替わった（席が替わる・外れる）ときは、前の席も変わった席にする
                    NoteChangedSlot(old.SceneSlot);
                    UnmapSlot(old.SceneSlot, index);
                    if (old.ObjectId != incoming.ObjectId)
                    {
                        NotePendingRemoved(old.ObjectId);
                    }
                }
                NoteChangedSlot(incoming.SceneSlot);
                NotePendingPosition(index);
            }
            // 2 周目: 書いて、席の位置の表を更新する
            for (size_t entry = 0; entry < count; ++entry)
            {
                const uint32_t index = delta.Indices[entry];
                if (index >= m_Proxies.size())
                {
                    continue;
                }
                m_Proxies[index] = delta.Proxies[entry];
                MapSlot(m_Proxies[index].SceneSlot, index);
            }
            m_LastEntryCount = static_cast<uint32_t>(count);

            if (!m_ChangedSlots.empty())
            {
                std::sort(m_ChangedSlots.begin(), m_ChangedSlots.end());
                m_ChangedSlots.erase(std::unique(m_ChangedSlots.begin(), m_ChangedSlots.end()), m_ChangedSlots.end());
            }
            // 描かないまま差分が溜まり続けても、覚える量は配列の大きさで頭打ちにする
            if (!m_bPendingAll && m_PendingPositions.size() > m_Proxies.size() + 4096u)
            {
                MarkPendingAll();
            }
            // 版は、配列が変わったときだけ進める（何も運ばなかった差分では進めない。消費者が作り直しを省ける）
            if (delta.bBaseline || sizeBefore != m_Proxies.size() || count > 0)
            {
                ++m_Revision;
            }
            return true;
        }

        /**
         * @brief 写しの内容でフレームを描く（RenderThread が、描画を始める場所で呼ぶ）
         * @param bRebase true のとき、当てただけで描かなかった差分も含め、最後に描いてから変わった proxy の前の変換を
         *        最後に描いたフレームの変換へ付け替える（描画がゲームのフレームを飛ばして TAA の履歴の基準が
         *        最後に描いたフレームのとき。全量の同期の RenderedObjectHistory と同じ規約）
         *
         * 最後に描いたフレームの変換は、物体（ObjectId）ごとに差分だけで保つ。最後に描いたフレームに無かった物体は付け替えない
         * （前の変換を今の変換にする）。描いたあと、変わった proxy の変換を次のために覚える。
         */
        void NoteDrawn(bool bRebase)
        {
            // 前のフレームの付け替えが残っていれば戻す（Apply を通らずに描くときの備え）
            BeginChangeAccumulation();
            SettleRebased();
            if (bRebase)
            {
                if (m_bPendingAll)
                {
                    for (uint32_t index = 0; index < static_cast<uint32_t>(m_Proxies.size()); ++index)
                    {
                        RebaseProxy(index);
                    }
                }
                else
                {
                    for (uint32_t index : m_PendingPositions)
                    {
                        if (index < m_Proxies.size())
                        {
                            RebaseProxy(index);
                        }
                    }
                }
            }
            if (m_bPendingAll)
            {
                m_DrawnWorlds.clear();
                for (const MegaGeometryProxy& proxy : m_Proxies)
                {
                    RecordDrawn(proxy);
                }
            }
            else
            {
                // 外れた物体を先に消し、その位置を埋めた物体（同じ物体が別の位置へ移ったときを含む）を後から書く
                for (uint64_t objectId : m_PendingRemoved)
                {
                    m_DrawnWorlds.erase(objectId);
                }
                for (uint32_t index : m_PendingPositions)
                {
                    if (index < m_Proxies.size())
                    {
                        RecordDrawn(m_Proxies[index]);
                    }
                }
            }
            m_PendingPositions.clear();
            m_PendingRemoved.clear();
            m_bPendingAll = false;
            // これから消費者が読む。次の Apply で、溜めた変わった席を捨てて数え直す
            m_bChangedConsumed = true;
        }

        /** @brief 写しの配列（GameThread の SceneView と同じ並び。席のない位置はない） */
        [[nodiscard]] const Container::VariableArray<MegaGeometryProxy>& GetProxies() const { return m_Proxies; }
        /** @brief 席（SceneSlot）の proxy の位置。席が配列に無いとき false */
        [[nodiscard]] bool TryGetPositionBySlot(uint32_t slot, uint32_t& outPosition) const
        {
            if (slot == 0 || slot >= m_PositionBySlot.size() || m_PositionBySlot[slot] == 0)
            {
                return false;
            }
            outPosition = m_PositionBySlot[slot] - 1u;
            return true;
        }
        /** @brief 席（SceneSlot）の proxy。席が配列に無いとき nullptr */
        [[nodiscard]] const MegaGeometryProxy* FindBySlot(uint32_t slot) const
        {
            uint32_t position = 0;
            return TryGetPositionBySlot(slot, position) ? &m_Proxies[position] : nullptr;
        }
        /**
         * @brief GetChangedBaseRevision の版から今までに変わった席（SceneSlot。昇順・重複なし）。AreAllChanged のときは空で、全部が変わった
         *
         * 席を持たない proxy（SceneSlot が 0）が変わったときは席で数えられないので、全部が変わったものとして扱う。
         * 消費者が取り込んだ（NoteDrawn を通った）あとの最初の Apply で数え直すので、描画を見送った差分の席も残る。
         */
        [[nodiscard]] const Container::VariableArray<uint32_t>& GetChangedSlots() const { return m_ChangedSlots; }
        [[nodiscard]] bool AreAllChanged() const { return m_bAllChanged; }
        /** @brief GetChangedSlots が数え始めた版。消費者が前に見た版がこれと同じときだけ、変わった席で直せる */
        [[nodiscard]] uint64_t GetChangedBaseRevision() const { return m_ChangedBaseRevision; }
        /**
         * @brief 消費者が前に見た版（consumerRevision）から今までの変更が、変わった席だけで直せるか
         *
         * false のときは全部を作り直す（全部が変わった・消費者が見た版から数えていない）。
         */
        [[nodiscard]] bool CanPatchFrom(uint64_t consumerRevision) const
        {
            return !m_bAllChanged && consumerRevision == m_ChangedBaseRevision;
        }
        /** @brief 基準を当てたとき・配列が変わる差分を当てたときに進む（何も変えない差分では進まない） */
        [[nodiscard]] uint64_t GetRevision() const { return m_Revision; }
        [[nodiscard]] uint64_t GetGeneration() const { return m_Generation; }
        /** @brief 世代が合わず当てなかった差分の数 */
        [[nodiscard]] uint64_t GetRejectedCount() const { return m_RejectedCount; }

        /**
         * @brief 検査 MEGA_SCENE_DELTA: 差分を当てた写しの hash が、送った時点の GameThread の配列の hash と一致するか
         *
         * RenderThread が Apply の直後に呼ぶ。結果は atomic で公開し、GameThread が読む。
         */
        bool Verify(const MegaSceneDelta& delta)
        {
            if (!delta.bVerify)
            {
                return true;
            }
            bool bSame = HashMegaGeometryContent(m_Proxies) == delta.VerifyHash && m_Proxies.size() == delta.Size;
            // 席の位置の表が、配列の全部の席を指していること
            for (uint32_t index = 0; bSame && index < static_cast<uint32_t>(m_Proxies.size()); ++index)
            {
                const uint32_t slot = m_Proxies[index].SceneSlot;
                uint32_t position = 0;
                bSame = slot == 0 || (TryGetPositionBySlot(slot, position) && position == index);
            }
            m_VerifyChecked.FetchAdd(1);
            if (!bSame)
            {
                m_VerifyMismatched.FetchAdd(1);
            }
            return bSame;
        }

        /** @brief 消費者が、差分で直した結果と全部を作り直した結果の照合を知らせる（検査用。どのスレッドからでも） */
        void NoteConsumerCheck(bool bSame) const
        {
            m_ConsumerChecked.FetchAdd(1);
            m_VerifyChecked.FetchAdd(1);
            if (!bSame)
            {
                m_VerifyMismatched.FetchAdd(1);
            }
        }

        /** @brief 消費者が差分で直した結果を照合した回数（どのスレッドからでも読める） */
        [[nodiscard]] uint64_t GetConsumerCheckedCount() const { return m_ConsumerChecked.Load(); }
        /** @brief 照合した回数（どのスレッドからでも読める） */
        [[nodiscard]] uint64_t GetVerifyCheckedCount() const { return m_VerifyChecked.Load(); }
        /** @brief 照合して食い違った回数 */
        [[nodiscard]] uint64_t GetVerifyMismatchedCount() const { return m_VerifyMismatched.Load(); }
        /** @brief 最後に当てた差分の proxy の数 */
        [[nodiscard]] uint32_t GetLastEntryCount() const { return m_LastEntryCount; }

    private:
        // 消費者が取り込んだあとなら、変わった席を捨てて、今の版から数え直す
        void BeginChangeAccumulation()
        {
            if (m_bChangedConsumed)
            {
                m_ChangedSlots.clear();
                m_bAllChanged = false;
                m_ChangedBaseRevision = m_Revision;
                m_bChangedConsumed = false;
            }
        }

        // 前のフレームの付け替えを GameThread の値へ戻す。戻した席は、消費者へ変わった席として知らせる
        void SettleRebased()
        {
            if (m_Rebased.empty())
            {
                return;
            }
            for (const RebasedEntry& entry : m_Rebased)
            {
                if (entry.Index < m_Proxies.size() && m_Proxies[entry.Index].ObjectId == entry.ObjectId)
                {
                    m_Proxies[entry.Index].PreviousWorldTransform = entry.Original;
                    NoteChangedSlot(m_Proxies[entry.Index].SceneSlot);
                }
            }
            m_Rebased.clear();
            if (!m_bAllChanged && !m_ChangedSlots.empty())
            {
                std::sort(m_ChangedSlots.begin(), m_ChangedSlots.end());
                m_ChangedSlots.erase(std::unique(m_ChangedSlots.begin(), m_ChangedSlots.end()), m_ChangedSlots.end());
            }
            ++m_Revision;
        }

        // 席を持たない proxy（SceneSlot が 0）が変わったら、席の一覧では数えられないので全部が変わった扱いにする
        void NoteChangedSlot(uint32_t slot)
        {
            if (m_bAllChanged)
            {
                return;
            }
            if (slot == 0)
            {
                m_bAllChanged = true;
                m_ChangedSlots.clear();
                return;
            }
            m_ChangedSlots.push_back(slot);
        }

        void MapSlot(uint32_t slot, uint32_t position)
        {
            if (slot == 0)
            {
                return;
            }
            if (slot >= m_PositionBySlot.size())
            {
                m_PositionBySlot.resize(static_cast<size_t>(slot) + 1u, 0u);
            }
            m_PositionBySlot[slot] = position + 1u;
        }

        // 表がその位置を指しているときだけ外す（先に別の位置へ移った席を消さない）
        void UnmapSlot(uint32_t slot, uint32_t position)
        {
            if (slot != 0 && slot < m_PositionBySlot.size() && m_PositionBySlot[slot] == position + 1u)
            {
                m_PositionBySlot[slot] = 0u;
            }
        }

        void MarkPendingAll()
        {
            m_bPendingAll = true;
            m_PendingPositions.clear();
            m_PendingRemoved.clear();
        }

        void NotePendingPosition(uint32_t index)
        {
            if (!m_bPendingAll)
            {
                m_PendingPositions.push_back(index);
            }
        }

        void NotePendingRemoved(uint64_t objectId)
        {
            if (!m_bPendingAll && objectId != 0)
            {
                m_PendingRemoved.push_back(objectId);
            }
        }

        // 値が変わるときだけ、GameThread の値を覚えて付け替える（次の Apply で戻す）
        void RebaseProxy(uint32_t index)
        {
            MegaGeometryProxy& proxy = m_Proxies[index];
            const auto found = proxy.ObjectId != 0 ? m_DrawnWorlds.find(proxy.ObjectId) : m_DrawnWorlds.end();
            const Math::Matrix4x4 rebased = found != m_DrawnWorlds.end() ? found->second : proxy.WorldTransform;
            if (std::memcmp(&rebased, &proxy.PreviousWorldTransform, sizeof(Math::Matrix4x4)) == 0)
            {
                return;
            }
            RebasedEntry entry;
            entry.Index = index;
            entry.ObjectId = proxy.ObjectId;
            entry.Original = proxy.PreviousWorldTransform;
            m_Rebased.push_back(entry);
            proxy.PreviousWorldTransform = rebased;
        }

        void RecordDrawn(const MegaGeometryProxy& proxy)
        {
            if (proxy.ObjectId != 0)
            {
                m_DrawnWorlds[proxy.ObjectId] = proxy.WorldTransform;
            }
        }

        // 描くときに付け替えた前の変換の、GameThread の値（次の Apply で戻す。付け替えはフレーム限定）
        struct RebasedEntry
        {
            uint32_t Index = 0;
            uint64_t ObjectId = 0;
            Math::Matrix4x4 Original;
        };

        Container::VariableArray<MegaGeometryProxy> m_Proxies;
        Container::VariableArray<uint32_t> m_ChangedSlots;
        Container::VariableArray<RebasedEntry> m_Rebased;
        // 変わった席を消費者が取り込んだか（true のとき、次の Apply で数え直す）と、数え始めた版
        bool m_bChangedConsumed = true;
        uint64_t m_ChangedBaseRevision = 0;
        // 席（SceneSlot）→ 配列の位置 + 1（0 は無し）
        Container::VariableArray<uint32_t> m_PositionBySlot;
        // 最後に描いたフレームの変換（ObjectId ごと）。当てただけで描かなかった差分では動かさない
        Container::UnorderedMap<uint64_t, Math::Matrix4x4> m_DrawnWorlds;
        // 最後に描いてから変わった位置・外れた物体（描いた履歴を差分で直すための印）。m_bPendingAll のときは全部
        Container::VariableArray<uint32_t> m_PendingPositions;
        Container::VariableArray<uint64_t> m_PendingRemoved;
        bool m_bPendingAll = false;
        bool m_bAllChanged = false;
        uint64_t m_Generation = 0;
        uint64_t m_Revision = 0;
        uint64_t m_RejectedCount = 0;
        uint32_t m_LastEntryCount = 0;
        mutable Thread::Atomic<uint64_t> m_ConsumerChecked{0};
        mutable Thread::Atomic<uint64_t> m_VerifyChecked{0};
        mutable Thread::Atomic<uint64_t> m_VerifyMismatched{0};
    };

    /**
     * @brief GameThread の MegaGeometry の差分の送り手
     *
     * パケットを作るたびに Build でパケットの差分を書き、送り終えたら NoteSent で台帳へ記録する。
     * 差分は、RenderThread が確認した番号より後の送信の変化をすべて積み直す累積（PacketDeltaLedger）なので、
     * RenderThread に飛ばされたパケットがあっても、何回当たっても写しは GameThread の配列と一致する。
     * 適用された世代が現在の世代でないあいだは、全量（基準）を送る。GameThread だけが触る。
     */
    class MegaGeometryDeltaSender
    {
    public:
        void Reset()
        {
            m_Ledger.Reset();
            m_Unsent.clear();
            m_Pending.clear();
            m_BaselineCount = 0;
        }

        /**
         * @param proxies GameThread の SceneView の配列（写しの元）
         * @param newlyChanged 前回の Build 以降に SceneView が書いた位置（昇順・重複なし）
         * @param confirmedSerial RenderThread が適用した最後の番号
         * @param confirmedGeneration そのパケットのシーンの世代
         * @param packetGeneration このパケットのシーンの世代
         */
        void Build(const Container::VariableArray<MegaGeometryProxy>& proxies,
                   const Container::VariableArray<uint32_t>& newlyChanged,
                   uint64_t confirmedSerial,
                   uint64_t confirmedGeneration,
                   uint64_t packetGeneration,
                   MegaSceneDelta& out)
        {
            for (uint32_t index : newlyChanged)
            {
                m_Unsent.push_back(index);
            }
            m_Ledger.NoteConfirmed(confirmedSerial, confirmedGeneration);

            out.Clear();
            out.bActive = true;
            out.Size = static_cast<uint32_t>(proxies.size());
            if (confirmedGeneration != packetGeneration)
            {
                // 適用された写しの世代が違う: 全量を送り、写しを捨てさせる
                out.bBaseline = true;
                ++m_BaselineCount;
                out.Indices.reserve(proxies.size());
                out.Proxies.reserve(proxies.size());
                for (uint32_t index = 0; index < out.Size; ++index)
                {
                    out.Indices.push_back(index);
                    out.Proxies.push_back(proxies[index]);
                }
                return;
            }

            m_Ledger.CollectPending(packetGeneration, m_Pending);
            for (uint32_t index : m_Unsent)
            {
                m_Pending.push_back(index);
            }
            std::sort(m_Pending.begin(), m_Pending.end());
            m_Pending.erase(std::unique(m_Pending.begin(), m_Pending.end()), m_Pending.end());
            out.Indices.reserve(m_Pending.size());
            out.Proxies.reserve(m_Pending.size());
            for (uint64_t key : m_Pending)
            {
                if (key < out.Size)
                {
                    out.Indices.push_back(static_cast<uint32_t>(key));
                    out.Proxies.push_back(proxies[static_cast<size_t>(key)]);
                }
            }
        }

        /** @brief Build したパケットを送り終えた（番号が決まった）。新しい変化を台帳へ移す */
        void NoteSent(uint64_t sendSerial, uint64_t sceneGeneration)
        {
            Container::VariableArray<uint64_t> keys;
            keys.reserve(m_Unsent.size());
            for (uint32_t index : m_Unsent)
            {
                keys.push_back(index);
            }
            m_Ledger.NoteSent(sendSerial, sceneGeneration, keys.data(), keys.size());
            m_Unsent.clear();
        }

        /** @brief 全量（基準）を送った回数 */
        [[nodiscard]] uint64_t GetBaselineCount() const { return m_BaselineCount; }
        /** @brief 確認を待っている送信の数 */
        [[nodiscard]] size_t GetUnconfirmedSendCount() const { return m_Ledger.GetUnconfirmedSendCount(); }

    private:
        PacketDeltaLedger m_Ledger;
        // 送っていない新しい変化（SceneView が書いた位置）
        Container::VariableArray<uint32_t> m_Unsent;
        Container::VariableArray<uint64_t> m_Pending;
        uint64_t m_BaselineCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
