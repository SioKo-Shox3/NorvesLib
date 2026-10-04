#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "RHI/RHITypes.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 解放を頼まれた RHI 資源を、GPU が使い終わるまで保持してから破棄するキュー
     *
     * 資源は「解放を頼んだ時点で最後に提出したフレームの serial」が完了するまで生かす。
     * 解放を頼んだときにレンダースレッドがフレームを記録中なら、その資源を記録済みのコマンドが
     * 参照しているかもしれないので、そのフレームの serial（CommitFrame で確定）まで延ばす。
     * 提出の serial は RenderThread が進めるが、解放は GameThread などからも頼まれるので、
     * すべての状態を内部のミューテックスで守る。
     *
     * serial の扱いは SkinnedMeshGpuStore に合わせる（0 は「まだ何も提出していない」で、
     * 完了済みと同じに扱う）。
     */
    class GpuRetireQueue final
    {
    public:
        GpuRetireQueue() = default;
        ~GpuRetireQueue() { Clear(); }

        GpuRetireQueue(const GpuRetireQueue &) = delete;
        GpuRetireQueue &operator=(const GpuRetireQueue &) = delete;

        /** @brief バッファの解放を頼む（null は無視）。GPU が使い終わっていれば即座に破棄する。 */
        void Retire(RHI::BufferPtr buffer)
        {
            if (buffer)
            {
                Entry entry;
                entry.Buffer = std::move(buffer);
                Enqueue(std::move(entry));
            }
        }

        /** @brief テクスチャの解放を頼む（null は無視）。GPU が使い終わっていれば即座に破棄する。 */
        void Retire(RHI::TexturePtr texture)
        {
            if (texture)
            {
                Entry entry;
                entry.Texture = std::move(texture);
                Enqueue(std::move(entry));
            }
        }

        /**
         * @brief フレームの記録を始める（RenderThread。完了済みの serial を渡す）
         *
         * この呼び出しの後 CommitFrame か AbortFrame までに解放を頼まれた資源は、そのフレームの
         * serial まで保持される。完了した分はここで破棄する。
         */
        void BeginFrame(uint64_t completedSerial)
        {
            Container::VariableArray<Entry> released;
            {
                Thread::ScopedLock lock(m_Mutex);
                m_bFrameOpen = true;
                CollectLocked(completedSerial, released);
            }
        }

        /** @brief フレームを serial で提出した。記録中に解放を頼まれた分の期限をその serial に確定する。 */
        void CommitFrame(uint64_t submissionSerial)
        {
            Thread::ScopedLock lock(m_Mutex);
            m_LastSubmittedSerial = std::max(m_LastSubmittedSerial, submissionSerial);
            ResolveAwaitingLocked(m_LastSubmittedSerial);
            m_bFrameOpen = false;
        }

        /** @brief フレームを提出せずに捨てた。記録中に解放を頼まれた分は、直前に提出した serial までの保持にする。 */
        void AbortFrame()
        {
            Thread::ScopedLock lock(m_Mutex);
            ResolveAwaitingLocked(m_LastSubmittedSerial);
            m_bFrameOpen = false;
        }

        /** @brief 完了した serial を渡して、期限が来た資源を破棄する。 */
        void Collect(uint64_t completedSerial)
        {
            Container::VariableArray<Entry> released;
            {
                Thread::ScopedLock lock(m_Mutex);
                CollectLocked(completedSerial, released);
            }
        }

        /** @brief GPU が完全に止まった後（WaitIdle の後）に、期限を問わず全部破棄する。 */
        void Clear()
        {
            Container::VariableArray<Entry> released;
            {
                Thread::ScopedLock lock(m_Mutex);
                released = std::move(m_Entries);
                m_Entries.clear();
            }
        }

        /** @brief 破棄を待っている資源の数（観測用） */
        size_t GetPendingCount() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_Entries.size();
        }

    private:
        struct Entry
        {
            RHI::BufferPtr Buffer;
            RHI::TexturePtr Texture;
            // この serial が完了するまで保持する。bAwaitingCommit のときは未確定。
            uint64_t Serial = 0;
            // 記録中のフレームの serial が決まるのを待っている。
            bool bAwaitingCommit = false;
        };

        void Enqueue(Entry entry)
        {
            Entry discarded;
            {
                Thread::ScopedLock lock(m_Mutex);
                if (m_bFrameOpen)
                {
                    entry.bAwaitingCommit = true;
                    m_Entries.push_back(std::move(entry));
                    return;
                }

                if (IsCompleteLocked(m_LastSubmittedSerial))
                {
                    // 破棄はロックの外で行う（RHI のデストラクタが他のロックを取っても詰まらないように）。
                    discarded = std::move(entry);
                }
                else
                {
                    entry.Serial = m_LastSubmittedSerial;
                    m_Entries.push_back(std::move(entry));
                }
            }
        }

        bool IsCompleteLocked(uint64_t serial) const
        {
            return serial == 0 || serial <= m_CompletedSerial;
        }

        void ResolveAwaitingLocked(uint64_t serial)
        {
            for (Entry &entry : m_Entries)
            {
                if (entry.bAwaitingCommit)
                {
                    entry.Serial = serial;
                    entry.bAwaitingCommit = false;
                }
            }
        }

        void CollectLocked(uint64_t completedSerial, Container::VariableArray<Entry> &outReleased)
        {
            m_CompletedSerial = std::max(m_CompletedSerial, completedSerial);

            Container::VariableArray<Entry> kept;
            kept.reserve(m_Entries.size());
            for (Entry &entry : m_Entries)
            {
                if (!entry.bAwaitingCommit && IsCompleteLocked(entry.Serial))
                {
                    outReleased.push_back(std::move(entry));
                }
                else
                {
                    kept.push_back(std::move(entry));
                }
            }
            m_Entries = std::move(kept);
        }

        mutable Thread::Mutex m_Mutex;
        Container::VariableArray<Entry> m_Entries;
        uint64_t m_LastSubmittedSerial = 0;
        uint64_t m_CompletedSerial = 0;
        bool m_bFrameOpen = false;
    };
}
