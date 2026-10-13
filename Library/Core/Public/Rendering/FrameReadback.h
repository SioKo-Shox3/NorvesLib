#pragma once

// フレームの持ち主の読み戻しの口。GPU が書いた小さな結果（統計など）をホストへ戻す共通の仕組み。
//
// パスは記録中に FrameResourceLane::RequestReadback で読み戻し先を 1 つ借りる。読み戻し先はホストが読めるバッファで、
// 次のどちらの形でも 1 つの要求で扱う:
// - GPU が直接書く: 返した Buffer を storage buffer として束縛し、シェーダーが書いた後に HostRead へのバリアを記録する
//   （0 から数えるなら、借りるときに bZeroOnHost でホストが 0 を書くか、パスが GPU で FillBuffer する）。
// - 他の資源からのコピー: CopySource を渡すと、口がコピーとバリアを記録する。
// 持ち主の束（FrameResourceOwners）は、提出が成功したときに要求へ実際の提出の通し番号を付け、その提出の完了の後に
// RenderThread で受け取り手へ結果を渡す（開始の回収と同じ時点）。提出しなかったフレームの要求は取り消しとして渡す。
// 受け取り手が受け取り終えるまで、その読み戻し先をほかの要求へ使い直さない。
//
// 受け取り手は FrameReadbackReceiver（パスがメンバとして持つ）で登録する。要求が覚えるのは受け取り手の共有の状態と
// 要求したときの世代だけで、パスへの生のポインタは残さない。パスが消えた・CancelPending で世代を進めたときは、
// 前の要求の結果を渡さずに捨てる（読み戻し先は GPU の完了を待ってから戻す）。
//
// 上限は要求の数（MaxPendingRequests）と読み戻し先の合計のバイト数（MaxBufferBytes）。満杯のときは借りられず、
// パスはそのフレームの統計を飛ばす（FrameResource.Readback.DroppedFull に数える）。終了時（GPU を待ち終えた後の
// FrameResourceOwners::Reset）に、提出済みの要求を全部渡してから残りを取り消す。

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/FrameResourceWriteTracker.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    /** @brief 受け取り手へ渡す結果の種類 */
    enum class FrameReadbackStatus : uint8_t
    {
        /** GPU が書き終えた。Data から Size バイトを読める（受け取りの呼び出しの間だけ） */
        Delivered,
        /** 提出しなかったフレームの要求、または終了時に提出されていなかった要求。Data は nullptr */
        Canceled,
    };

    /** @brief 受け取り手へ渡す読み戻しの結果 */
    struct FrameReadbackResult
    {
        FrameReadbackStatus Status = FrameReadbackStatus::Canceled;
        /** @brief 読み戻した中身（Delivered のときだけ。受け取りの呼び出しを抜けたら使わない） */
        const void* Data = nullptr;
        uint64_t Size = 0;
        /** @brief RequestReadback が返した要求の番号（パスが要求ごとの情報を引くのに使う） */
        uint64_t RequestId = 0;
        /** @brief 要求を記録したフレームの提出の通し番号（取り消しは 0） */
        uint64_t SubmissionSerial = 0;
        uint32_t ViewId = UINT32_MAX;
        uint32_t ViewportId = UINT32_MAX;

        template <typename T>
        const T* As() const
        {
            return Status == FrameReadbackStatus::Delivered && Size >= sizeof(T) ? static_cast<const T*>(Data) : nullptr;
        }
    };

    /** @brief 読み戻しの結果を受け取る側（パス）が実装する受け口。RenderThread から呼ぶ */
    class IFrameReadbackReceiver
    {
    public:
        virtual ~IFrameReadbackReceiver() = default;
        virtual void OnFrameReadback(const FrameReadbackResult& result) = 0;
    };

    /** @brief 受け取り手の共有の状態（要求が持つ。受け取り手が消えると Receiver が nullptr になる） */
    struct FrameReadbackReceiverState
    {
        IFrameReadbackReceiver* Receiver = nullptr;
        uint64_t Generation = 1;
    };

    /**
     * @brief 読み戻しの受け取り手の登録（パスがメンバとして持つ）
     *
     * 受け取り手（ふつうはパス自身）と同じ寿命で持つ。消えると、まだ渡していない要求の結果は捨てられる。
     * CancelPending は世代を進め、それまでの要求の結果を渡さない（パスの Shutdown・測定のやり直しで呼ぶ）。
     * RenderThread だけから使う。
     */
    class FrameReadbackReceiver
    {
    public:
        explicit FrameReadbackReceiver(IFrameReadbackReceiver* receiver)
            : m_State(Container::MakeShared<FrameReadbackReceiverState>())
        {
            m_State->Receiver = receiver;
        }

        ~FrameReadbackReceiver()
        {
            m_State->Receiver = nullptr;
            ++m_State->Generation;
        }

        FrameReadbackReceiver(const FrameReadbackReceiver&) = delete;
        FrameReadbackReceiver& operator=(const FrameReadbackReceiver&) = delete;

        /** @brief これまでの要求の結果を渡さない（読み戻し先は GPU の完了の後に戻る） */
        void CancelPending() { ++m_State->Generation; }

        const Container::TSharedPtr<FrameReadbackReceiverState>& GetState() const { return m_State; }

    private:
        Container::TSharedPtr<FrameReadbackReceiverState> m_State;
    };

    /** @brief 読み戻しの要求 */
    struct FrameReadbackRequest
    {
        /** @brief 結果を受け取る登録（必須） */
        const FrameReadbackReceiver* Receiver = nullptr;
        /** @brief 読み戻すバイト数 */
        uint64_t Size = 0;
        /** @brief 借りるときにホストが 0 を書く（GPU が直接数える読み戻し先を 0 から始める。GPU の FillBuffer は要らない） */
        bool bZeroOnHost = false;
        /** @brief 結果に付けて返すビュー・ビューポート */
        uint32_t ViewId = UINT32_MAX;
        uint32_t ViewportId = UINT32_MAX;
        /**
         * @brief 他の資源からのコピー（任意）
         *
         * 渡すと、RequestReadback が CopySource の CopySourceOffset から Size バイトを読み戻し先へ写すコマンドとバリアを
         * 記録する（commandList が要る）。CopySourceState は今の状態で、写した後にその状態へ戻す。
         */
        RHI::BufferPtr CopySource;
        uint64_t CopySourceOffset = 0;
        RHI::ResourceState CopySourceState = RHI::ResourceState::Common;
    };

    /**
     * @brief 借りた読み戻し先
     *
     * GPU が直接書くときは Buffer の Offset から Size バイトを storage buffer として束縛し、書いた後に
     * HostRead へのバリアを記録する（借りた時点の状態は HostRead として扱ってよい）。
     */
    struct FrameReadbackTarget
    {
        RHI::BufferPtr Buffer;
        uint64_t Offset = 0;
        uint64_t Size = 0;
        uint64_t RequestId = 0;

        bool IsValid() const { return static_cast<bool>(Buffer) && RequestId != 0; }
    };

    /** @brief 読み戻しの口の累計の統計 */
    struct FrameReadbackStats
    {
        uint64_t Requests = 0;
        uint64_t Delivered = 0;
        uint64_t Canceled = 0;
        /** 受け取り手が消えた・世代が進んだので渡さずに捨てた数 */
        uint64_t Orphaned = 0;
        /** 上限で借りられなかった数（そのフレームの統計を飛ばした数） */
        uint64_t DroppedFull = 0;
        /** 読み戻し先を作れなかった・不正な要求の数 */
        uint64_t Failures = 0;
        /** 終了時の吐き出しで渡した数 */
        uint64_t Flushed = 0;
        uint64_t MaxPending = 0;
    };

    /**
     * @brief 読み戻しの口の本体（FrameResourceOwners が 1 つ持つ）
     *
     * RequestReadback は記録中に並列の記録のワーカーから呼んでもよい（排他を持つ）。それ以外は RenderThread だけ。
     */
    class FrameReadbackQueue
    {
    public:
        /** @brief 待っている要求の数の上限 */
        static constexpr uint32_t MaxPendingRequests = 64;
        /** @brief 読み戻し先の合計のバイト数の上限 */
        static constexpr uint64_t MaxBufferBytes = 4ull * 1024ull * 1024ull;
        /** @brief 読み戻し先の大きさの最小の単位 */
        static constexpr uint64_t BufferGranularity = 256;

        FrameReadbackQueue() = default;
        FrameReadbackQueue(const FrameReadbackQueue&) = delete;
        FrameReadbackQueue& operator=(const FrameReadbackQueue&) = delete;

        /** @brief 読み戻し先を作るデバイスを設定する（RenderThread。借りる前に） */
        void Configure(RHI::IDevice* device)
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            m_Device = device;
        }

        /** @brief 読み戻し先を全部手放す（Flush の後、デバイスを手放す前に呼ぶ） */
        void Release()
        {
            LogStats("release");
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            m_Pending.clear();
            for (Entry& entry : m_Entries)
            {
                if (entry.Buffer && entry.Mapped)
                {
                    entry.Buffer->Unmap();
                }
            }
            m_Entries.clear();
            m_BufferBytes = 0;
            m_Device = nullptr;
        }

        /**
         * @brief 読み戻し先を 1 つ借りる（記録中。generation は記録中のフレームの世代）
         * @return 借りた読み戻し先。上限・作れない・不正な要求のときは IsValid() が false
         */
        FrameReadbackTarget Request(const FrameReadbackRequest& request, uint64_t generation, RHI::ICommandList* commandList)
        {
            FrameReadbackTarget target;
            if (!request.Receiver || request.Size == 0 || generation == 0 ||
                (request.CopySource && (!commandList || request.CopySourceOffset + request.Size > request.CopySource->GetSize())))
            {
                NorvesLib::Thread::ScopedLock lock(m_Mutex);
                ++m_Stats.Requests;
                ++m_Stats.Failures;
                return target;
            }
            {
                NorvesLib::Thread::ScopedLock lock(m_Mutex);
                ++m_Stats.Requests;
                if (!m_Device)
                {
                    ++m_Stats.Failures;
                    return target;
                }
                if (m_Pending.size() >= MaxPendingRequests)
                {
                    ++m_Stats.DroppedFull;
                    ++m_DroppedSinceLog;
                    return target;
                }
                const uint64_t capacity = (request.Size + BufferGranularity - 1) / BufferGranularity * BufferGranularity;
                uint32_t entryIndex = FindFreeEntry(request.Size);
                if (entryIndex == InvalidEntry)
                {
                    if (m_BufferBytes + capacity > MaxBufferBytes)
                    {
                        ++m_Stats.DroppedFull;
                        ++m_DroppedSinceLog;
                        return target;
                    }
                    entryIndex = CreateEntry(capacity);
                    if (entryIndex == InvalidEntry)
                    {
                        ++m_Stats.Failures;
                        return target;
                    }
                }
                Entry& entry = m_Entries[entryIndex];
                entry.bInUse = true;
                if (request.bZeroOnHost)
                {
                    // 使い直す読み戻し先は前の受け取りが終わっていて GPU も使っていない（host-coherent なので提出で見える）
                    std::memset(entry.Mapped, 0, static_cast<size_t>(request.Size));
                }
                Pending pending;
                pending.RequestId = m_NextRequestId++;
                pending.EntryIndex = entryIndex;
                pending.Size = request.Size;
                pending.FrameGeneration = generation;
                pending.Receiver = request.Receiver->GetState();
                pending.ReceiverGeneration = pending.Receiver->Generation;
                pending.ViewId = request.ViewId;
                pending.ViewportId = request.ViewportId;
                m_Pending.push_back(pending);
                m_Stats.MaxPending = std::max<uint64_t>(m_Stats.MaxPending, m_Pending.size());
                target.Buffer = entry.Buffer;
                target.Offset = 0;
                target.Size = request.Size;
                target.RequestId = pending.RequestId;
            }

            if (request.CopySource)
            {
                // 他の資源から写す。写した後はホストの読み取りへ見せ、写し元は元の状態へ戻す
                const bool bTransitionSource = request.CopySourceState != RHI::ResourceState::CopySource;
                if (bTransitionSource)
                {
                    commandList->BufferBarrier(request.CopySource, request.CopySourceState, RHI::ResourceState::CopySource);
                }
                commandList->BufferBarrier(target.Buffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
                commandList->CopyBuffer(request.CopySource, target.Buffer, request.Size, request.CopySourceOffset, 0);
                commandList->BufferBarrier(target.Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
                if (bTransitionSource)
                {
                    commandList->BufferBarrier(request.CopySource, RHI::ResourceState::CopySource, request.CopySourceState);
                }
            }
            return target;
        }

        /** @brief 提出。generation のフレームの要求へ提出の通し番号を付ける（RenderThread） */
        void CommitSubmitted(uint64_t generation, uint64_t submissionSerial)
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            for (Pending& pending : m_Pending)
            {
                if (pending.FrameGeneration == generation && pending.State == PendingState::Recording)
                {
                    pending.State = PendingState::Submitted;
                    pending.SubmissionSerial = submissionSerial;
                }
            }
        }

        /** @brief 中止。generation のフレームの要求を取り消しの印にする（次の Retire で渡す。RenderThread） */
        void Abort(uint64_t generation)
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            for (Pending& pending : m_Pending)
            {
                if (pending.FrameGeneration == generation && pending.State == PendingState::Recording)
                {
                    pending.State = PendingState::Aborted;
                }
            }
        }

        /**
         * @brief 回収。completedSubmissionSerial までに完了した提出の要求の結果と、中止した要求の取り消しを渡す（RenderThread）
         * @param bRestarted 完了の通し番号を振り直した（作り直す前に GPU を待ち終えているので、提出済みを全部渡す）
         */
        void Retire(uint64_t completedSubmissionSerial, bool bRestarted)
        {
            Deliver([completedSubmissionSerial, bRestarted](const Pending& pending) {
                return pending.State == PendingState::Aborted ||
                       (pending.State == PendingState::Submitted &&
                        (bRestarted || pending.SubmissionSerial <= completedSubmissionSerial));
            },
                    false);
        }

        /** @brief 終了時の吐き出し。GPU を待ち終えた後に、提出済みの要求を全部渡し、残りを取り消す（RenderThread） */
        void Flush()
        {
            Deliver([](const Pending&) { return true; }, true);
        }

        /** @brief 待っている要求の数 */
        uint64_t GetPendingCount() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_Pending.size();
        }

        FrameReadbackStats GetStats() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_Stats;
        }

        /** @brief 前のログから満杯で飛ばした要求があったか（あれば次のフレームの終わりにログへ出す） */
        bool HasDropsSinceLog() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_DroppedSinceLog != 0;
        }

        /** @brief 統計を 1 行でログに出す（開発ビルドだけ） */
        void LogStats(const char* reason)
        {
#if NORVES_BUILD_DEVELOPMENT
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            m_DroppedSinceLog = 0;
            NORVES_LOG_INFO("FrameResource",
                            "FrameResource.Readback reason=%s FrameResource.Readback.Requests=%llu "
                            "FrameResource.Readback.Delivered=%llu FrameResource.Readback.Canceled=%llu "
                            "FrameResource.Readback.Orphaned=%llu FrameResource.Readback.DroppedFull=%llu "
                            "FrameResource.Readback.Failures=%llu FrameResource.Readback.Flushed=%llu "
                            "FrameResource.Readback.Pending=%llu FrameResource.Readback.MaxPending=%llu "
                            "FrameResource.Readback.Buffers=%llu FrameResource.Readback.BufferBytes=%llu",
                            reason ? reason : "(none)",
                            static_cast<unsigned long long>(m_Stats.Requests),
                            static_cast<unsigned long long>(m_Stats.Delivered),
                            static_cast<unsigned long long>(m_Stats.Canceled),
                            static_cast<unsigned long long>(m_Stats.Orphaned),
                            static_cast<unsigned long long>(m_Stats.DroppedFull),
                            static_cast<unsigned long long>(m_Stats.Failures),
                            static_cast<unsigned long long>(m_Stats.Flushed),
                            static_cast<unsigned long long>(m_Pending.size()),
                            static_cast<unsigned long long>(m_Stats.MaxPending),
                            static_cast<unsigned long long>(m_Entries.size()),
                            static_cast<unsigned long long>(m_BufferBytes));
#else
            (void)reason;
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            m_DroppedSinceLog = 0;
#endif
        }

    private:
        static constexpr uint32_t InvalidEntry = ~0u;

        enum class PendingState : uint8_t
        {
            Recording,
            Submitted,
            Aborted,
        };

        struct Entry
        {
            RHI::BufferPtr Buffer;
            uint8_t* Mapped = nullptr;
            uint64_t Capacity = 0;
            bool bInUse = false;
        };

        struct Pending
        {
            uint64_t RequestId = 0;
            uint32_t EntryIndex = InvalidEntry;
            uint64_t Size = 0;
            uint64_t FrameGeneration = 0;
            uint64_t SubmissionSerial = 0;
            PendingState State = PendingState::Recording;
            Container::TSharedPtr<FrameReadbackReceiverState> Receiver;
            uint64_t ReceiverGeneration = 0;
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
        };

        // 空いている読み戻し先のうち、size が入るいちばん小さい物（m_Mutex を持って呼ぶ）
        uint32_t FindFreeEntry(uint64_t size) const
        {
            uint32_t best = InvalidEntry;
            for (uint32_t index = 0; index < m_Entries.size(); ++index)
            {
                const Entry& entry = m_Entries[index];
                if (!entry.bInUse && entry.Capacity >= size &&
                    (best == InvalidEntry || entry.Capacity < m_Entries[best].Capacity))
                {
                    best = index;
                }
            }
            return best;
        }

        // 読み戻し先を作って写像したままにする（m_Mutex を持って呼ぶ）
        uint32_t CreateEntry(uint64_t capacity)
        {
            // 口が持ち続け、受け取りが終わるまで使い直さない読み戻し先。守られた資源として登録する
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            Entry entry;
            entry.Buffer = m_Device->CreateBuffer(RHI::BufferDesc(capacity,
                                                                  RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                                                  true,
                                                                  "FrameReadback"));
            entry.Mapped = entry.Buffer ? static_cast<uint8_t*>(entry.Buffer->Map(0, 0)) : nullptr;
            if (!entry.Mapped)
            {
                return InvalidEntry;
            }
            entry.Capacity = capacity;
            m_BufferBytes += capacity;
            m_Entries.push_back(entry);
            return static_cast<uint32_t>(m_Entries.size() - 1);
        }

        // bReady が真の要求を受け取り手へ渡し、読み戻し先を空ける。受け取りの呼び出しは排他の外で行う
        template <typename ReadyFn>
        void Deliver(ReadyFn bReady, bool bFlush)
        {
            Container::VariableArray<Pending> ready;
            {
                NorvesLib::Thread::ScopedLock lock(m_Mutex);
                for (size_t index = 0; index < m_Pending.size();)
                {
                    if (bReady(m_Pending[index]))
                    {
                        ready.push_back(m_Pending[index]);
                        m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                    else
                    {
                        ++index;
                    }
                }
            }
            for (const Pending& pending : ready)
            {
                const bool bDelivered = pending.State == PendingState::Submitted;
                IFrameReadbackReceiver* const receiver =
                    pending.Receiver && pending.Receiver->Generation == pending.ReceiverGeneration ? pending.Receiver->Receiver
                                                                                                    : nullptr;
                if (receiver)
                {
                    FrameReadbackResult result;
                    result.Status = bDelivered ? FrameReadbackStatus::Delivered : FrameReadbackStatus::Canceled;
                    result.Data = bDelivered ? m_Entries[pending.EntryIndex].Mapped : nullptr;
                    result.Size = pending.Size;
                    result.RequestId = pending.RequestId;
                    result.SubmissionSerial = bDelivered ? pending.SubmissionSerial : 0;
                    result.ViewId = pending.ViewId;
                    result.ViewportId = pending.ViewportId;
                    receiver->OnFrameReadback(result);
                }
                NorvesLib::Thread::ScopedLock lock(m_Mutex);
                if (!receiver)
                {
                    ++m_Stats.Orphaned;
                }
                else if (bDelivered)
                {
                    ++m_Stats.Delivered;
                    m_Stats.Flushed += bFlush ? 1u : 0u;
                }
                else
                {
                    ++m_Stats.Canceled;
                }
                // 受け取りが終わったので、この読み戻し先をほかの要求へ使い直してよい
                m_Entries[pending.EntryIndex].bInUse = false;
            }
        }

        mutable NorvesLib::Thread::Mutex m_Mutex;
        RHI::IDevice* m_Device = nullptr;
        Container::VariableArray<Entry> m_Entries;
        Container::VariableArray<Pending> m_Pending;
        uint64_t m_BufferBytes = 0;
        uint64_t m_NextRequestId = 1;
        uint64_t m_DroppedSinceLog = 0;
        FrameReadbackStats m_Stats;
    };
} // namespace NorvesLib::Core::Rendering
