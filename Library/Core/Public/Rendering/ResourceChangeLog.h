#pragma once

/**
 * @file ResourceChangeLog.h
 * @brief 描画の資源（メッシュ・材質など）の登録・更新・解放を、World へ知らせるための記録
 *
 * 資源の側は handle を積むだけで、使っている行を知らない。World が同期の前に取り出し、
 * 「資源の handle → 使っている行」の逆引きで、proxy が古くなる行を見つける。
 */

#include "Container/Containers.h"
#include "Thread/Mutex.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 変化を知らせる資源の種類
    enum class RenderResourceKind : uint8_t
    {
        Mesh,
        Material,
        Texture,
        SkinnedMesh,
        Count
    };

    /// 資源の種類と handle の番号の組。逆引きの鍵に使う
    struct RenderResourceRef
    {
        RenderResourceKind Kind = RenderResourceKind::Mesh;
        uint64_t Id = 0;

        /// 逆引きの表の鍵（上位 8 bit に種類、下位 56 bit に handle の番号）
        [[nodiscard]] constexpr uint64_t Key() const
        {
            return (static_cast<uint64_t>(Kind) << 56) | (Id & 0x00FFFFFFFFFFFFFFull);
        }
    };

    /**
     * @brief 資源の変化の記録（スレッドをまたいで積める）
     *
     * 取り出されないまま溜まり続けないよう、上限を超えたら個別の記録を捨てて「全部変わった」とだけ覚える。
     */
    class ResourceChangeLog
    {
    public:
        /// 個別の記録の上限。超えたら取り出しの結果が overflow になる
        static constexpr size_t kMaxEntries = 1u << 16;

        /// 変化を 1 件積む
        void Notify(RenderResourceKind kind, uint64_t id)
        {
            if (id == 0)
            {
                return;
            }
            Thread::ScopedLock lock(m_Mutex);
            if (m_Entries.size() >= kMaxEntries)
            {
                m_bOverflow = true;
                return;
            }
            m_Entries.push_back(RenderResourceRef{kind, id});
        }

        /// 溜まった記録を取り出して空にする。overflow のときは true を返し、記録は空
        bool Drain(Container::VariableArray<RenderResourceRef>& outEntries)
        {
            outEntries.clear();
            Thread::ScopedLock lock(m_Mutex);
            const bool bOverflow = m_bOverflow;
            m_bOverflow = false;
            if (bOverflow)
            {
                m_Entries.clear();
                return true;
            }
            outEntries.swap(m_Entries);
            return false;
        }

        /// 取り出していない記録があるか
        [[nodiscard]] bool HasPending() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_bOverflow || !m_Entries.empty();
        }

    private:
        mutable Thread::Mutex m_Mutex;
        Container::VariableArray<RenderResourceRef> m_Entries;
        bool m_bOverflow = false;
    };
}
