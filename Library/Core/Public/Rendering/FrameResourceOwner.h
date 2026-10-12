#pragma once

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Thread/Atomic.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief フレームの持ち主が区別できる飛行中のフレームの数の上限（スワップチェーンの飛行中のフレーム数はこれ以下でなければならない） */
    inline constexpr uint32_t FrameResourceMaxInFlightSlots = 4;

    /**
     * @brief フレームの持ち主から借りた物に付ける世代
     *
     * どの飛行中のスロットの、どの開始で借りたかを表す。世代は開始のたびに全部のスロットを通して 1 ずつ増えるので、
     * 同じスロットでも前のフレームで借りた物とは必ず違う値になる。0 は未設定。
     */
    struct FrameResourceGeneration
    {
        static constexpr uint32_t InvalidSlot = ~0u;

        uint32_t SlotIndex = InvalidSlot;
        uint64_t Generation = 0;

        bool IsSet() const { return SlotIndex != InvalidSlot && Generation != 0; }
    };

    /** @brief フレームの持ち主の寿命の段 */
    enum class FrameResourceOwnerState : uint8_t
    {
        /** 借りた物は無い（回収済み、またはまだ使っていない） */
        Idle,
        /** 開始から提出・中止まで。借りられるのはこの間だけ */
        Recording,
        /** 提出した。結び付けた提出の通し番号の完了で回収する */
        Submitted,
        /** 提出しなかった。GPU は使っていないので次の開始で回収する */
        Aborted,
    };

    inline const char* GetFrameResourceOwnerStateName(FrameResourceOwnerState state)
    {
        switch (state)
        {
        case FrameResourceOwnerState::Idle:
            return "Idle";
        case FrameResourceOwnerState::Recording:
            return "Recording";
        case FrameResourceOwnerState::Submitted:
            return "Submitted";
        case FrameResourceOwnerState::Aborted:
            return "Aborted";
        }
        return "Unknown";
    }

    /**
     * @brief フレームの持ち主から借りる口（リング・記述子の pool など）が実装する回収の受け口
     *
     * 持ち主はフレームの分を回収するとき（結び付けた提出の完了、または提出しなかったフレームの次の開始）に呼ぶ。
     * 呼ばれた時点で、そのフレームに借りた物を GPU は使っていないので、全部を戻して次のフレームに貸してよい。
     * RenderThread から呼ぶ。
     */
    class IFrameResourceAllocator
    {
    public:
        virtual ~IFrameResourceAllocator() = default;

        /** @brief generation の世代で借りた物を全部戻す */
        virtual void OnFrameResourcesReclaimed(const FrameResourceGeneration& generation) = 0;
    };

    /**
     * @brief 飛行中のスロット 1 つ分の「フレームの持ち主」
     *
     * パスを書く人は、フレームごとに書くデータをこの持ち主から借りる（ViewRenderContext::FrameResources で引く）。
     * 飛行中のフレームの番号・提出の通し番号・FrameUseRing を扱わなくてよい。寿命は FrameResourceOwners が決める:
     * 開始（スワップチェーンの BeginFrame の成功の後）→ 提出（提出の通し番号を結び付ける）または中止 → 回収。
     * 1 フレームの間（開始から提出・中止まで）は、全ビュー・ビューポート・overlay が同じ持ち主を使い、
     * パスが何回 Execute しても位置は戻らない（戻すのは回収だけ）。
     *
     * 借りた物には GetGeneration() の世代を付け、使う前に ValidateBorrowed で確かめる。開発ビルドでは、
     * 借りたフレームより後で使うと FRAME_RESOURCE_STALE を出して拒む。
     * 状態を変えるのは RenderThread だけ。ValidateBorrowed は記録中に並列の記録のワーカーから呼んでもよい。
     */
    class FrameResourceOwner
    {
    public:
        uint32_t GetSlotIndex() const { return m_SlotIndex; }
        FrameResourceOwnerState GetState() const { return m_State; }
        bool IsRecording() const { return m_State == FrameResourceOwnerState::Recording; }

        /** @brief 結び付けた提出の通し番号（提出する前は 0） */
        uint64_t GetSubmissionSerial() const { return m_SubmissionSerial; }

        /** @brief 今のフレームの世代。借りた物に付ける（記録中でなければ未設定を返す） */
        FrameResourceGeneration GetGeneration() const
        {
            if (!IsRecording())
            {
                return {};
            }
            return {m_SlotIndex, m_Generation};
        }

        /** @brief generation がこの持ち主の記録中のフレームで借りた物か */
        bool IsCurrent(const FrameResourceGeneration& generation) const
        {
            return IsRecording() && generation.SlotIndex == m_SlotIndex && generation.Generation == m_Generation;
        }

        /**
         * @brief 借りた物を使ってよいか確かめる
         *
         * 開発ビルドでは、記録中のフレームで借りた物でなければ FRAME_RESOURCE_STALE をログに出し、false を返す
         * （呼び出し側は使わずに捨てる）。開発ビルドでなければ確かめずに true を返す。
         */
        bool ValidateBorrowed(const FrameResourceGeneration& generation, const char* resourceName) const
        {
#if NORVES_BUILD_DEVELOPMENT
            if (IsCurrent(generation))
            {
                return true;
            }
            const uint64_t rejectCount = m_StaleRejectCount.FetchAdd(1) + 1;
            // 同じ誤りが毎フレーム続くとログが埋まるので、最初の 8 回と、その後は 2 の冪の回だけ出す
            if (rejectCount <= 8 || (rejectCount & (rejectCount - 1)) == 0)
            {
                NORVES_LOG_ERROR("FrameResource",
                                 "FRAME_RESOURCE_STALE resource=%s slot=%u borrowed_slot=%u borrowed_generation=%llu "
                                 "current_generation=%llu state=%s rejects=%llu",
                                 resourceName ? resourceName : "(unnamed)",
                                 m_SlotIndex,
                                 generation.SlotIndex,
                                 static_cast<unsigned long long>(generation.Generation),
                                 static_cast<unsigned long long>(m_Generation),
                                 GetFrameResourceOwnerStateName(m_State),
                                 static_cast<unsigned long long>(rejectCount));
            }
            return false;
#else
            (void)generation;
            (void)resourceName;
            return true;
#endif
        }

        /** @brief ValidateBorrowed が拒んだ回数（開発ビルドだけ数える） */
        uint64_t GetStaleRejectCount() const { return m_StaleRejectCount.Load(); }

        /** @brief 回収した回数（回収の段を確かめる検査と統計のため） */
        uint64_t GetReclaimCount() const { return m_ReclaimCount; }

        /** @brief 借りる口を登録する。回収のたびに OnFrameResourcesReclaimed が呼ばれる（RenderThread） */
        void AttachAllocator(IFrameResourceAllocator* allocator)
        {
            if (allocator && std::find(m_Allocators.begin(), m_Allocators.end(), allocator) == m_Allocators.end())
            {
                m_Allocators.push_back(allocator);
            }
        }

        /** @brief 登録を外す（RenderThread） */
        void DetachAllocator(IFrameResourceAllocator* allocator)
        {
            const auto it = std::find(m_Allocators.begin(), m_Allocators.end(), allocator);
            if (it != m_Allocators.end())
            {
                m_Allocators.erase(it);
            }
        }

    private:
        friend class FrameResourceOwners;

        void Begin(uint32_t slotIndex, uint64_t generation)
        {
            assert(m_State == FrameResourceOwnerState::Idle && "回収していない持ち主を開始した");
            m_SlotIndex = slotIndex;
            m_Generation = generation;
            m_SubmissionSerial = 0;
            m_State = FrameResourceOwnerState::Recording;
        }

        void MarkSubmitted(uint64_t submissionSerial)
        {
            assert(IsRecording());
            m_SubmissionSerial = submissionSerial;
            m_State = FrameResourceOwnerState::Submitted;
        }

        void MarkAborted()
        {
            assert(IsRecording());
            m_SubmissionSerial = 0;
            m_State = FrameResourceOwnerState::Aborted;
        }

        void Reclaim()
        {
            if (m_State == FrameResourceOwnerState::Idle)
            {
                return;
            }
            const FrameResourceGeneration generation{m_SlotIndex, m_Generation};
            for (IFrameResourceAllocator* allocator : m_Allocators)
            {
                allocator->OnFrameResourcesReclaimed(generation);
            }
            m_SubmissionSerial = 0;
            m_State = FrameResourceOwnerState::Idle;
            ++m_ReclaimCount;
        }

        uint32_t m_SlotIndex = FrameResourceGeneration::InvalidSlot;
        uint64_t m_Generation = 0;
        uint64_t m_SubmissionSerial = 0;
        FrameResourceOwnerState m_State = FrameResourceOwnerState::Idle;
        uint64_t m_ReclaimCount = 0;
        mutable NorvesLib::Thread::Atomic<uint64_t> m_StaleRejectCount{0};
        Container::VariableArray<IFrameResourceAllocator*> m_Allocators;
    };

    /**
     * @brief 飛行中のスロットごとのフレームの持ち主と、その寿命の 4 つの段
     *
     * - 開始（BeginFrame）: スワップチェーンの BeginFrame が成功した後に 1 回呼ぶ。先に完了した提出の分を回収し、
     *   提出も中止もしないまま残ったフレーム（例外などで抜けた）を中止として扱い、中止した分を回収してから、
     *   このスロットの持ち主を新しい世代で記録中にする。
     * - 提出（CommitSubmitted）: 提出が成功したら、その提出の通し番号を記録中の持ち主に結び付ける。
     * - 中止（AbortFrame）: 提出しなかったら呼ぶ。GPU は使っていないので次の開始で回収する。
     * - 回収（Retire）: 結び付けた提出の通し番号が完了したら戻す。スワップチェーンの作り直しで完了の通し番号が
     *   前より小さくなった（振り直した）ときは、作り直す前に GPU を待ち終えているので、提出済みの分を全部戻す。
     *
     * RenderThread だけから使う（排他は持たない）。
     */
    class FrameResourceOwners
    {
    public:
        FrameResourceOwners()
        {
            for (uint32_t index = 0; index < FrameResourceMaxInFlightSlots; ++index)
            {
                m_Owners[index].m_SlotIndex = index;
            }
        }

        /**
         * @brief 開始。inFlightIndex のスロットの持ち主を記録中にして返す
         * @param inFlightIndex 飛行中のフレームの番号（スワップチェーンの現在のフレームの番号）
         * @param completedSubmissionSerial スワップチェーンの完了済みの提出の通し番号
         * @return 記録中の持ち主。番号が上限を超えていれば nullptr
         */
        FrameResourceOwner* BeginFrame(uint32_t inFlightIndex, uint64_t completedSubmissionSerial)
        {
            Retire(completedSubmissionSerial);
            if (m_Recording)
            {
                // 提出も中止もしないまま次のフレームが始まった。提出していないので GPU は使っていない
                ++m_AbandonedFrameCount;
                m_Recording->MarkAborted();
                m_Recording = nullptr;
            }
            for (FrameResourceOwner& owner : m_Owners)
            {
                if (owner.m_State == FrameResourceOwnerState::Aborted)
                {
                    owner.Reclaim();
                }
            }

            if (inFlightIndex >= FrameResourceMaxInFlightSlots)
            {
                NORVES_LOG_ERROR("FrameResource",
                                 "飛行中のフレームの番号 %u がフレームの持ち主の上限 %u を超えています",
                                 inFlightIndex, FrameResourceMaxInFlightSlots);
                return nullptr;
            }

            FrameResourceOwner& owner = m_Owners[inFlightIndex];
            if (owner.m_State == FrameResourceOwnerState::Submitted)
            {
                // スワップチェーンはこのスロットの前の提出のフェンスを待ってから BeginFrame を返すので、ここで
                // 完了していないのは完了の通し番号の渡し方の誤り。GPU は待ち終えているので回収して進める
                ++m_UnretiredBeginCount;
                NORVES_LOG_WARNING("FrameResource",
                                   "FRAME_RESOURCE_OWNER_UNRETIRED slot=%u submission=%llu completed=%llu",
                                   inFlightIndex,
                                   static_cast<unsigned long long>(owner.m_SubmissionSerial),
                                   static_cast<unsigned long long>(completedSubmissionSerial));
                owner.Reclaim();
            }
            owner.Begin(inFlightIndex, m_NextGeneration++);
            m_Recording = &owner;
            return &owner;
        }

        /** @brief 提出。記録中の持ち主に提出の通し番号（0 以外）を結び付ける。記録中の持ち主が無ければ false */
        bool CommitSubmitted(uint64_t submissionSerial)
        {
            if (!m_Recording)
            {
                return false;
            }
            if (submissionSerial == 0)
            {
                AbortFrame();
                return false;
            }
            m_Recording->MarkSubmitted(submissionSerial);
            m_Recording = nullptr;
            return true;
        }

        /** @brief 中止。記録中の持ち主を、次の開始で回収する印にする */
        void AbortFrame()
        {
            if (!m_Recording)
            {
                return;
            }
            m_Recording->MarkAborted();
            m_Recording = nullptr;
        }

        /** @brief 回収。completedSubmissionSerial までに完了した提出の持ち主を戻す（振り直しは全部戻す） */
        void Retire(uint64_t completedSubmissionSerial)
        {
            const bool bRestarted = completedSubmissionSerial < m_LastCompletedSubmissionSerial;
            m_LastCompletedSubmissionSerial = completedSubmissionSerial;
            if (bRestarted)
            {
                ++m_SerialRestartCount;
            }
            for (FrameResourceOwner& owner : m_Owners)
            {
                if (owner.m_State == FrameResourceOwnerState::Submitted &&
                    (bRestarted || owner.m_SubmissionSerial <= completedSubmissionSerial))
                {
                    owner.Reclaim();
                }
            }
        }

        /** @brief 全部の持ち主を回収して初期状態へ戻す（GPU を待ち終えた後の終了処理で呼ぶ） */
        void Reset()
        {
            m_Recording = nullptr;
            for (FrameResourceOwner& owner : m_Owners)
            {
                owner.Reclaim();
            }
            m_LastCompletedSubmissionSerial = 0;
        }

        /** @brief 記録中の持ち主（開始から提出・中止まで。それ以外は nullptr） */
        FrameResourceOwner* GetRecordingOwner() { return m_Recording; }
        const FrameResourceOwner* GetRecordingOwner() const { return m_Recording; }

        FrameResourceOwner& GetOwner(uint32_t slotIndex)
        {
            assert(slotIndex < FrameResourceMaxInFlightSlots);
            return m_Owners[slotIndex];
        }

        /** @brief 提出も中止もしないまま次の開始で中止として扱ったフレームの数 */
        uint64_t GetAbandonedFrameCount() const { return m_AbandonedFrameCount; }
        /** @brief 開始の時点で回収されていなかった提出の数（完了の通し番号の渡し方の誤り） */
        uint64_t GetUnretiredBeginCount() const { return m_UnretiredBeginCount; }
        /** @brief 完了の通し番号の振り直しを見つけた回数 */
        uint64_t GetSerialRestartCount() const { return m_SerialRestartCount; }

    private:
        FrameResourceOwner m_Owners[FrameResourceMaxInFlightSlots];
        FrameResourceOwner* m_Recording = nullptr;
        uint64_t m_NextGeneration = 1;
        uint64_t m_LastCompletedSubmissionSerial = 0;
        uint64_t m_AbandonedFrameCount = 0;
        uint64_t m_UnretiredBeginCount = 0;
        uint64_t m_SerialRestartCount = 0;
    };
} // namespace NorvesLib::Core::Rendering
