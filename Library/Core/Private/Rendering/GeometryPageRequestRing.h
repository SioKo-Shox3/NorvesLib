#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "Rendering/MegaGeometry/GeometryPageRequestSet.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ジオメトリのページの要求のバッファを3つ持ち、完了した古いフレームのものを GPU を待たずに読み戻すリング
     *
     * VirtualTextureFeedbackRing と同じ作り。各スロットは host-visible・host-coherent な 1 枚のバッファ
     * （ヘッダ + capacity 語。並びは GeometryPageRequestBuffer）で、カリングのコンピュートシェーダーが
     * storage buffer として直接書く。コピーは要らない。
     *
     * スロットの流れ: Free →（BeginFrame で獲得。CPU がヘッダを 0 にして容量を書く）Recording →（CommitFrame）
     * InFlight(serial) →（BeginFrame に渡された完了済みの serial が追いつく）読み戻して集計 → Free。
     * - 読み戻しは「提出の serial が完了済み」かつ「書いたフレームから MinReadAgeFrames 以上経った」ことの
     *   比較だけで決める。どちらかが満たされないスロットはフェンスも WaitIdle も待たずに残し、次のフレームで見直す。
     *   RenderThread は止まらない。
     * - GPU の書き込みをホストの読み取りへ見せるバリア（RecordHostReadBarrier）を、最後の書き込みの後・提出の前に記録する。
     * - 3つとも使用中のフレームは、バッファを持たない（GetCurrentBuffer が null）。そのフレームの要求は取らずに進む。
     * - 集計の結果は GeometryPageRequestSet に溜まり、TakeRequests でストリーマへ渡す（渡すと空に戻る）。
     *
     * BeginFrame・GetCurrentBuffer・CommitFrame・AbortFrame・RecordHostReadBarrier は RenderThread から、
     * TakeRequests・GetStats はどのスレッドからでも呼べる（内部のミューテックスで守る）。
     * バッファは GPU が使い終わった後（WaitIdle の後）に破棄すること。
     */
    class GeometryPageRequestRing final
    {
    public:
        static constexpr uint32_t SlotCount = 3;
        // 書いたフレームから、これだけ経ったスロットだけを読み戻す（飛行数が 1 のエンジンでも 2 フレーム遅れにする）
        static constexpr uint64_t MinReadAgeFrames = 2;

        struct Config
        {
            /** @brief 1 枚のバッファの要求の件数（ヘッダを除く） */
            uint32_t Capacity = MegaGeometry::GeometryPageRequestBuffer::DefaultCapacity;
        };

        struct Stats
        {
            bool bEnabled = false;
            uint32_t Capacity = 0;
            uint32_t BusySlots = 0;
            uint64_t Frames = 0;
            // 要求を取れたフレームの数と、空きスロットが無くて取れなかったフレームの数
            uint64_t FramesRecorded = 0;
            uint64_t FramesSkipped = 0;
            // 読み戻して集計したバッファの数と、そのうち溢れていたもの
            uint64_t BuffersRead = 0;
            uint64_t BuffersOverflowed = 0;
            uint64_t OverflowTotal = 0;
            // 読み戻した要求の延べ件数
            uint64_t RequestsRead = 0;
        };

        explicit GeometryPageRequestRing(Container::TSharedPtr<RHI::IDevice> device, const Config &config = Config())
            : m_Device(std::move(device)), m_Config(config)
        {
        }

        ~GeometryPageRequestRing() { Clear(); }

        GeometryPageRequestRing(const GeometryPageRequestRing &) = delete;
        GeometryPageRequestRing &operator=(const GeometryPageRequestRing &) = delete;

        /**
         * @brief 要求を取るかどうかを決める。有効にするとバッファを確保する（未確保のとき）
         * @return 有効にできたら true。バッファを作れなかったときは false のまま（無効）
         */
        bool SetEnabled(bool bEnabled)
        {
            Thread::ScopedLock lock(m_Mutex);
            if (bEnabled && !EnsureSlotsLocked())
            {
                m_bEnabled = false;
                return false;
            }
            m_bEnabled = bEnabled;
            return true;
        }

        /** @brief フレームの記録を始める（RenderThread。完了済みの提出 serial を渡す） */
        void BeginFrame(uint64_t completedSerial)
        {
            Container::VariableArray<uint32_t> readable;
            uint64_t acquireFrame = 0;
            {
                Thread::ScopedLock lock(m_Mutex);
                // 前のフレームが提出も中止もされないまま次が始まったときは、中止として扱う
                ReleaseRecordingLocked();
                m_CompletedSerial = std::max(m_CompletedSerial, completedSerial);
                acquireFrame = ++m_FrameCounter;
                m_Stats.Frames = m_FrameCounter;

                for (uint32_t i = 0; i < SlotCount; ++i)
                {
                    Slot &slot = m_Slots[i];
                    if (slot.State == SlotState::InFlight && slot.Serial <= m_CompletedSerial &&
                        acquireFrame - slot.Frame >= MinReadAgeFrames)
                    {
                        slot.State = SlotState::Reading;
                        readable.push_back(i);
                    }
                }
            }

            // 古いフレームの順に復号する（読み戻し中のスロットは他から触られない）。ロックの外で行う
            std::sort(readable.begin(), readable.end(), [this](uint32_t a, uint32_t b) {
                return m_Slots[a].Frame < m_Slots[b].Frame;
            });
            for (uint32_t index : readable)
            {
                ReadSlot(index);
            }

            Thread::ScopedLock lock(m_Mutex);
            m_CurrentSlot = InvalidSlot;
            if (!m_bEnabled)
            {
                return;
            }
            for (uint32_t i = 0; i < SlotCount; ++i)
            {
                Slot &slot = m_Slots[i];
                if (slot.State == SlotState::Free && slot.Buffer && slot.Mapped != nullptr)
                {
                    // 前のフレームの要求を消す（件数と溢れを 0 にして容量を書く）。提出より前の CPU の書き込みなので、提出で GPU から見える
                    uint32_t *words = static_cast<uint32_t *>(slot.Mapped);
                    words[MegaGeometry::GeometryPageRequestBuffer::CountWord] = 0;
                    words[MegaGeometry::GeometryPageRequestBuffer::OverflowWord] = 0;
                    words[MegaGeometry::GeometryPageRequestBuffer::CapacityWord] = m_Config.Capacity;
                    words[3] = 0;
                    slot.State = SlotState::Recording;
                    slot.Frame = acquireFrame;
                    m_CurrentSlot = i;
                    ++m_Stats.FramesRecorded;
                    return;
                }
            }
            ++m_Stats.FramesSkipped;
        }

        /** @brief このフレームのシェーダーが書くバッファ。獲得できなかった（または無効の）ときは null */
        RHI::BufferPtr GetCurrentBuffer() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_CurrentSlot == InvalidSlot ? RHI::BufferPtr{} : m_Slots[m_CurrentSlot].Buffer;
        }

        /** @brief 今のフレームのバッファの要求の容量（バッファが無ければ 0。シェーダーはこの値が 0 なら書かない） */
        uint32_t GetCurrentCapacity() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_CurrentSlot == InvalidSlot ? 0u : m_Config.Capacity;
        }

        /**
         * @brief 今のフレームの番号（BeginFrame ごとに 1 進む。要求のフレームの印に使う）
         */
        uint64_t GetFrameCounter() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_FrameCounter;
        }

        /**
         * @brief このフレームのバッファへのコンピュートシェーダーの書き込みを、ホストの読み取りへ見せるバリアを記録する
         *
         * 最後の書き込みの後・render pass の外に、RenderThread が 1 回呼ぶ。獲得できなかったフレームでは何も記録しない。
         * @return バリアを記録したら true
         */
        bool RecordHostReadBarrier(RHI::ICommandList &commandList)
        {
            RHI::BufferPtr buffer = GetCurrentBuffer();
            if (!buffer)
            {
                return false;
            }
            commandList.BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::HostRead);
            return true;
        }

        /** @brief 要求のバッファ 1 枚の大きさ（バイト） */
        uint64_t GetBufferBytes() const { return MegaGeometry::GeometryPageRequestBuffer::GetBufferBytes(m_Config.Capacity); }

        /** @brief フレームを serial で提出した。獲得していたスロットは、その serial の完了後に読み戻す */
        void CommitFrame(uint64_t submissionSerial)
        {
            Thread::ScopedLock lock(m_Mutex);
            if (m_CurrentSlot == InvalidSlot)
            {
                return;
            }
            Slot &slot = m_Slots[m_CurrentSlot];
            slot.State = SlotState::InFlight;
            slot.Serial = submissionSerial;
            m_CurrentSlot = InvalidSlot;
        }

        /** @brief フレームを提出せずに捨てた。GPU は書いていないので、スロットはそのまま空きへ戻す */
        void AbortFrame()
        {
            Thread::ScopedLock lock(m_Mutex);
            ReleaseRecordingLocked();
        }

        /**
         * @brief 溜まった集計を out へ渡し、こちらは空に戻す（ストリーマが読む）
         * @return 要求か溢れた件数があれば true。無ければ out は変えない
         */
        bool TakeRequests(MegaGeometry::GeometryPageRequestSet &out)
        {
            Thread::ScopedLock lock(m_Mutex);
            if (m_Pending.IsEmpty())
            {
                return false;
            }
            out.Merge(m_Pending);
            m_Pending.Clear();
            return true;
        }

        Stats GetStats() const
        {
            Thread::ScopedLock lock(m_Mutex);
            Stats stats = m_Stats;
            stats.bEnabled = m_bEnabled;
            stats.Capacity = m_Config.Capacity;
            for (const Slot &slot : m_Slots)
            {
                if (slot.State != SlotState::Free)
                {
                    ++stats.BusySlots;
                }
            }
            return stats;
        }

        /** @brief GPU が止まった後（WaitIdle の後）に、バッファを破棄して初期状態へ戻す */
        void Clear()
        {
            Thread::ScopedLock lock(m_Mutex);
            for (Slot &slot : m_Slots)
            {
                slot = Slot();
            }
            m_Pending.Clear();
            m_CurrentSlot = InvalidSlot;
            m_CompletedSerial = 0;
            m_bEnabled = false;
        }

    private:
        enum class SlotState : uint8_t
        {
            Free,
            Recording,
            InFlight,
            Reading
        };

        struct Slot
        {
            RHI::BufferPtr Buffer;
            void *Mapped = nullptr;
            SlotState State = SlotState::Free;
            uint64_t Frame = 0;
            uint64_t Serial = 0;
        };

        static constexpr uint32_t InvalidSlot = 0xFFFFFFFFu;

        // 記録中のスロットを空きへ戻す。ロックを持って呼ぶ
        void ReleaseRecordingLocked()
        {
            if (m_CurrentSlot != InvalidSlot)
            {
                m_Slots[m_CurrentSlot].State = SlotState::Free;
                m_CurrentSlot = InvalidSlot;
            }
        }

        // 完了したスロットの内容を復号して、溜まった集計へ足し、スロットを空きへ戻す
        void ReadSlot(uint32_t index)
        {
            Slot &slot = m_Slots[index];
            MegaGeometry::GeometryPageRequestSet decoded;
            const MegaGeometry::GeometryPageRequestDecodeResult result =
                decoded.AddBuffer(static_cast<const uint32_t *>(slot.Mapped), m_Config.Capacity, slot.Frame);

            Thread::ScopedLock lock(m_Mutex);
            m_Pending.Merge(decoded);
            ++m_Stats.BuffersRead;
            m_Stats.RequestsRead += result.Accepted;
            if (result.Overflow != 0)
            {
                ++m_Stats.BuffersOverflowed;
                m_Stats.OverflowTotal += result.Overflow;
            }
            slot.State = SlotState::Free;
        }

        // 3つのバッファを作って常時写像する。ロックを持って呼ぶ。作れなかったら false（作れた分は残さない）
        bool EnsureSlotsLocked()
        {
            if (m_Slots[0].Buffer)
            {
                return true;
            }
            if (!m_Device || m_Config.Capacity == 0)
            {
                return false;
            }

            const uint64_t bytes = MegaGeometry::GeometryPageRequestBuffer::GetBufferBytes(m_Config.Capacity);
            Slot created[SlotCount];
            for (uint32_t i = 0; i < SlotCount; ++i)
            {
                RHI::BufferDesc desc(bytes, RHI::ResourceUsage::StorageBuffer, true, "GeometryPageRequest");
                try
                {
                    created[i].Buffer = m_Device->CreateBuffer(desc);
                }
                catch (const std::exception &exception)
                {
                    LOG_ERROR("GeometryPageRequestRing: バッファの作成が例外で失敗した: %s", exception.what());
                    created[i].Buffer.reset();
                }
                if (!created[i].Buffer)
                {
                    LOG_ERROR("GeometryPageRequestRing: バッファを作れなかった bytes=%llu",
                              static_cast<unsigned long long>(bytes));
                    return false;
                }
                // 常時写像する（host-coherent なので flush は要らない）
                created[i].Mapped = created[i].Buffer->Map(0, 0);
                if (created[i].Mapped == nullptr)
                {
                    LOG_ERROR("GeometryPageRequestRing: バッファを写像できなかった");
                    return false;
                }
                std::memset(created[i].Mapped, 0, static_cast<size_t>(bytes));
            }
            for (uint32_t i = 0; i < SlotCount; ++i)
            {
                m_Slots[i] = std::move(created[i]);
            }
            return true;
        }

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Config m_Config;
        mutable Thread::Mutex m_Mutex;
        Slot m_Slots[SlotCount];
        uint32_t m_CurrentSlot = InvalidSlot;
        bool m_bEnabled = false;
        uint64_t m_FrameCounter = 0;
        uint64_t m_CompletedSerial = 0;
        MegaGeometry::GeometryPageRequestSet m_Pending;
        Stats m_Stats;
    };
} // namespace NorvesLib::Core::Rendering
