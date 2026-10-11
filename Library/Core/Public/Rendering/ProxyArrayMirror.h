// Mesh・Skinned・Light の proxy 配列を、GameThread から RenderThread へ差分で運ぶための送り手と写し。
// 送り手（GameThread）は配列の変わった位置だけをパケットへ積み、確認されていない送信の分も積み直す（MegaGeometry と同じ約束）。
// 写し（RenderThread）は適用したパケットの差分を当てて、GameThread の配列と同じ並びの配列を持つ。
// 構成の revision の hash に使う集合の hash を、変わった位置だけで更新する入れ物もここに置く。
#pragma once

#include "Rendering/FramePacket.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneProxyHash.h"
#include "Container/Containers.h"
#include "Thread/Atomic.h"

#include <algorithm>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief proxy 配列の内容の hash（検査用。並びに依存する）
     *
     * GameThread の SceneView が描画の準備で書き換える SortDepth は、RenderThread の消費者が読まないので HashProxy に含まれない。
     */
    template<typename TProxy>
    uint64_t HashProxyArrayContent(const Container::VariableArray<TProxy>& proxies)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const TProxy& proxy : proxies)
        {
            hash = (hash ^ HashProxy(proxy)) * 1099511628211ull;
        }
        return hash ^ static_cast<uint64_t>(proxies.size());
    }

    /**
     * @brief RenderThread の proxy 配列の写し
     *
     * 適用したパケットの差分（ProxyArrayDelta）を当て、GameThread の配列と同じ並びの配列を持つ。
     * 基準の差分では、パケットの Scene の配列（全量）で置き換える。描画するスレッドだけが触る（照合の数は atomic）。
     */
    template<typename TProxy>
    class ProxyArrayMirror
    {
    public:
        void Reset()
        {
            m_Proxies.clear();
            m_Generation = 0;
            m_RejectedCount = 0;
            m_LastEntryCount = 0;
            m_VerifyChecked.Store(0);
            m_VerifyMismatched.Store(0);
        }

        /**
         * @brief 差分を当てる
         * @param delta 適用したパケットの差分
         * @param sceneGeneration そのパケットのシーンの世代
         * @param baseline 基準の差分のときの全量（パケットの Scene の同じ種類の配列）
         * @return 当てたとき true。世代が合わない差分（基準でない）は当てずに false
         */
        bool Apply(const ProxyArrayDelta<TProxy>& delta, uint64_t sceneGeneration,
                   const Container::VariableArray<TProxy>& baseline)
        {
            m_LastEntryCount = 0;
            if (!delta.bActive)
            {
                return false;
            }
            if (delta.bBaseline)
            {
                m_Proxies = baseline;
                m_Generation = sceneGeneration;
                m_LastEntryCount = static_cast<uint32_t>(baseline.size());
                return true;
            }
            if (m_Generation != sceneGeneration)
            {
                ++m_RejectedCount;
                return false;
            }
            // 末尾を切る・伸ばす（伸ばした位置は、同じ差分が中身を運ぶ）
            m_Proxies.resize(delta.Size);
            const size_t count = std::min(delta.Indices.size(), delta.Proxies.size());
            for (size_t entry = 0; entry < count; ++entry)
            {
                const uint32_t index = delta.Indices[entry];
                if (index < m_Proxies.size())
                {
                    m_Proxies[index] = delta.Proxies[entry];
                }
            }
            m_LastEntryCount = static_cast<uint32_t>(count);
            return true;
        }

        /** @brief 写しの配列（GameThread の配列と同じ並び） */
        [[nodiscard]] const Container::VariableArray<TProxy>& GetProxies() const { return m_Proxies; }
        [[nodiscard]] uint64_t GetGeneration() const { return m_Generation; }
        /** @brief 世代が合わず当てなかった差分の数 */
        [[nodiscard]] uint64_t GetRejectedCount() const { return m_RejectedCount; }
        /** @brief 最後に当てた差分の proxy の数（基準のときは全量の数） */
        [[nodiscard]] uint32_t GetLastEntryCount() const { return m_LastEntryCount; }

        /**
         * @brief 検査 PACKET_DELTA: 差分を当てた写しの hash が、送った時点の GameThread の配列の hash と一致するか
         *
         * RenderThread が Apply の直後に呼ぶ。結果は atomic で公開し、GameThread が読む。
         */
        bool Verify(const ProxyArrayDelta<TProxy>& delta)
        {
            if (!delta.bActive || !delta.bVerify)
            {
                return true;
            }
            const bool bSame = m_Proxies.size() == delta.Size && HashProxyArrayContent(m_Proxies) == delta.VerifyHash;
            m_VerifyChecked.FetchAdd(1);
            if (!bSame)
            {
                m_VerifyMismatched.FetchAdd(1);
            }
            return bSame;
        }

        /** @brief 照合した回数（どのスレッドからでも読める） */
        [[nodiscard]] uint64_t GetVerifyCheckedCount() const { return m_VerifyChecked.Load(); }
        /** @brief 照合して食い違った回数 */
        [[nodiscard]] uint64_t GetVerifyMismatchedCount() const { return m_VerifyMismatched.Load(); }

    private:
        Container::VariableArray<TProxy> m_Proxies;
        uint64_t m_Generation = 0;
        uint64_t m_RejectedCount = 0;
        uint32_t m_LastEntryCount = 0;
        Thread::Atomic<uint64_t> m_VerifyChecked{0};
        Thread::Atomic<uint64_t> m_VerifyMismatched{0};
    };

    /**
     * @brief GameThread の proxy 配列の差分の送り手
     *
     * パケットを作るたびに Build でパケットの差分を書き、送り終えたら NoteSent で台帳へ記録する。
     * 差分は、RenderThread が確認した番号より後の送信の変化をすべて積み直す累積（PacketDeltaLedger）なので、
     * 飛ばされたパケットがあっても、何回当たっても写しは GameThread の配列と一致する。
     * 適用された世代が現在の世代でないあいだは、全量（基準）をパケットの Scene の配列へ書く。GameThread だけが触る。
     */
    template<typename TProxy>
    class ProxyArrayDeltaSender
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
         * @param proxies GameThread の配列（写しの元）
         * @param newlyChanged 前回の Build 以降に書いた位置（配列の大きさ以上の位置を含んでもよい）
         * @param confirmedSerial RenderThread が適用した最後の番号
         * @param confirmedGeneration そのパケットのシーンの世代
         * @param packetGeneration このパケットのシーンの世代
         * @param out パケットの差分
         * @param outBaseline 基準のとき全量を書く配列（パケットの Scene の同じ種類の配列）。それ以外のときは空にする
         */
        void Build(const Container::VariableArray<TProxy>& proxies,
                   const Container::VariableArray<uint32_t>& newlyChanged,
                   uint64_t confirmedSerial,
                   uint64_t confirmedGeneration,
                   uint64_t packetGeneration,
                   ProxyArrayDelta<TProxy>& out,
                   Container::VariableArray<TProxy>& outBaseline)
        {
            for (uint32_t index : newlyChanged)
            {
                m_Unsent.push_back(index);
            }
            m_Ledger.NoteConfirmed(confirmedSerial, confirmedGeneration);

            out.Clear();
            outBaseline.clear();
            out.bActive = true;
            out.Size = static_cast<uint32_t>(proxies.size());
            if (confirmedGeneration != packetGeneration)
            {
                // 適用された写しの世代が違う: 全量を送り、写しを置き換えさせる
                out.bBaseline = true;
                ++m_BaselineCount;
                outBaseline = proxies;
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
            m_Keys.clear();
            m_Keys.reserve(m_Unsent.size());
            for (uint32_t index : m_Unsent)
            {
                m_Keys.push_back(index);
            }
            m_Ledger.NoteSent(sendSerial, sceneGeneration, m_Keys.data(), m_Keys.size());
            m_Unsent.clear();
        }

        /** @brief 全量（基準）を送った回数 */
        [[nodiscard]] uint64_t GetBaselineCount() const { return m_BaselineCount; }
        /** @brief 確認を待っている送信の数 */
        [[nodiscard]] size_t GetUnconfirmedSendCount() const { return m_Ledger.GetUnconfirmedSendCount(); }

    private:
        PacketDeltaLedger m_Ledger;
        // 送っていない新しい変化（書いた位置）
        Container::VariableArray<uint32_t> m_Unsent;
        Container::VariableArray<uint64_t> m_Pending;
        Container::VariableArray<uint64_t> m_Keys;
        uint64_t m_BaselineCount = 0;
    };

    /**
     * @brief 順序に依らない集合の hash（要素の hash の数・XOR・和）を、変わった位置だけで更新する
     *
     * 位置ごとに入れた値（混ぜた後の要素の hash）を覚え、変わった位置は古い値を抜いて新しい値を足す。
     * 全部を数え直した値と同じになる（数・XOR・和は要素の順に依らない）。GameThread だけが触る。
     */
    class ProxyRevisionSet
    {
    public:
        void Reset()
        {
            m_Values.clear();
            m_Xor = 0;
            m_Sum = 0;
        }

        /**
         * @param newSize 今の配列の大きさ
         * @param changed 前回の Update 以降に書いた位置（重複・大きさ以上の位置を含んでもよい）
         * @param bRebuild true のとき、覚えた値を捨てて全部を数え直す（配列の持ち主・世代が替わったとき）
         * @param valueAt 位置 index の要素の、混ぜた後の hash を返す
         */
        template<typename TValueAt>
        void Update(size_t newSize, const Container::VariableArray<uint32_t>& changed, bool bRebuild, TValueAt&& valueAt)
        {
            if (bRebuild)
            {
                Reset();
                m_Values.reserve(newSize);
                for (size_t index = 0; index < newSize; ++index)
                {
                    m_Values.push_back(valueAt(index));
                    Add(m_Values.back());
                }
                return;
            }
            const size_t oldSize = m_Values.size();
            const size_t common = std::min(oldSize, newSize);
            for (uint32_t index : changed)
            {
                if (index < common)
                {
                    Remove(m_Values[index]);
                    m_Values[index] = valueAt(index);
                    Add(m_Values[index]);
                }
            }
            for (size_t index = newSize; index < oldSize; ++index)
            {
                Remove(m_Values[index]);
            }
            m_Values.resize(newSize);
            for (size_t index = oldSize; index < newSize; ++index)
            {
                m_Values[index] = valueAt(index);
                Add(m_Values[index]);
            }
        }

        [[nodiscard]] uint64_t GetCount() const { return static_cast<uint64_t>(m_Values.size()); }
        [[nodiscard]] uint64_t GetXor() const { return m_Xor; }
        [[nodiscard]] uint64_t GetSum() const { return m_Sum; }

    private:
        void Add(uint64_t value)
        {
            m_Xor ^= value;
            m_Sum += value;
        }

        void Remove(uint64_t value)
        {
            m_Xor ^= value;
            m_Sum -= value;
        }

        Container::VariableArray<uint64_t> m_Values;
        uint64_t m_Xor = 0;
        uint64_t m_Sum = 0;
    };

} // namespace NorvesLib::Core::Rendering
