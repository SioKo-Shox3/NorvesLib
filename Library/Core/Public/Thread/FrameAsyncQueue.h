// 複数フレームにまたがる計算を制限つきで実行し、ゲームスレッドで結果を受け渡す。
#pragma once

#include "Container/Containers.h"
#include "Thread/Atomic.h"
#include <cstdint>

namespace NorvesLib::Thread
{
    class JobSystem;
    struct FrameAsyncQueueState;

    enum class FrameAsyncOutcome : uint8_t
    {
        Delivered, ///< Deliver を呼んで結果を渡した。
        Cancelled, ///< 要求または持ち主の取り消しにより配送を取り消した。
        Stale,     ///< 登録済みの持ち主の世代と一致しないため配送を取り消した。
        Rejected,  ///< 受付拒否・提出失敗、または未着手タスクの取り消しにより要求を破棄した。
        Failed,    ///< Execute が例外を投げたため結果を破棄した。
        Shutdown   ///< 列の終了、または配送時に検出した JobSystem の受付停止により結果を破棄した。
    };

    struct FrameAsyncOwner
    {
        uint64_t Key = 0;
        uint32_t Generation = 0;
    };

    struct FrameAsyncRequestId
    {
        uint64_t Value = 0;
        /** @brief 受付で割り当てられた有効な識別子かを返す。 */
        bool IsValid() const
        {
            return Value != 0;
        }
    };

    struct FrameAsyncQueueSettings
    {
        uint32_t MaxConcurrent = 4;
    };

    struct FrameAsyncQueueStats
    {
        uint32_t Pending = 0;
        uint32_t Running = 0;
        uint32_t Completed = 0; // 完了したが配送していない要求。
        uint32_t PeakRunning = 0;
        uint64_t Submitted = 0;
        uint64_t Delivered = 0;
        uint64_t Discarded = 0;
    };

    /** @brief 実行中の取り消し・世代変更・終了を協調的に検出する。Execute の間だけ有効。 */
    class FrameAsyncExecuteContext
    {
    public:
        /** @brief 取り消し・世代変更・終了により、計算の打ち切りが要求されたかを返す。 */
        bool IsCancelRequested() const;

    private:
        friend struct FrameAsyncQueueState;
        explicit FrameAsyncExecuteContext(const Atomic<bool>& cancelRequested);
        const Atomic<bool>& m_cancelRequested;
    };

    class IFrameAsyncJob
    {
    public:
        virtual ~IFrameAsyncJob() = default;
        /** @brief ワーカーで計算する。入力は値で保持し、World や Component に触れない。 */
        virtual void Execute(const FrameAsyncExecuteContext& context) = 0;
        /** @brief DeliverCompleted 内のゲームスレッドでだけ結果を渡す。 */
        virtual void Deliver() = 0;
        /** @brief ゲームスレッドで破棄を通知する。持ち主は破棄済みの場合があるため触れない。 */
        virtual void Discard(FrameAsyncOutcome outcome) = 0;
    };

    /** @brief 全公開操作と配送・破棄は、同一のゲームスレッドから呼ぶ。 */
    class FrameAsyncQueue
    {
    public:
        /** @brief jobs が空なら投入時に既存の JobSystem::Get() を使う。 */
        explicit FrameAsyncQueue(JobSystem* jobs = nullptr, FrameAsyncQueueSettings settings = {});
        /** @brief GameThread から Close と同じ終了処理を行う。Execute が終わらなければ返らない。 */
        ~FrameAsyncQueue();
        FrameAsyncQueue(const FrameAsyncQueue&) = delete;
        FrameAsyncQueue& operator=(const FrameAsyncQueue&) = delete;

        /** @brief 優先度の大きい順、同順位は投入順に実行する要求を受け付け、識別子を返す。
         * 閉鎖・終了処理中・JobSystem の受付停止では無効の id を返し、仕事があればその場で Discard(Rejected) を呼ぶ。
         * 空の仕事も無効。受理後の提出失敗は完了一覧へ移し、配送時に Rejected で破棄する。 */
        FrameAsyncRequestId Submit(Core::Container::TUniquePtr<IFrameAsyncJob> job, int32_t priority, FrameAsyncOwner owner);
        /** @brief 待機中は完了一覧へ移し、実行中は取り消し印を立て、完了済み未配送も Cancelled で破棄する。
         * 対象があれば true、配送・破棄の通知済みまたは未知の id なら false。通知は配送の点で行う。 */
        bool Cancel(FrameAsyncRequestId id);
        /** @brief 待機中の要求だけ優先度を変えて true を返す。
         * 実行中・完了済み・通知済み・未知の id は変えず false を返す。 */
        bool SetPriority(FrameAsyncRequestId id, int32_t priority);
        /** @brief 持ち主の世代を登録する。世代は登録済みの鍵だけで比較し、不一致の要求は Stale で捨てる。
         * 待機中は開始せず、実行中には取り消し印を立てる。既に取り消し・拒否済みの理由は保つ。 */
        void SetOwnerGeneration(uint64_t ownerKey, uint32_t generation);
        /** @brief 鍵の全要求を取り消し、世代の登録も消す。終了は待たない。
         * 実行中の分は終わってから、次の DeliverCompleted で Discard(Cancelled) を呼ぶ。 */
        void CancelOwner(uint64_t ownerKey);
        /** @brief GameMode の Leave の IGameModeAsyncWork::CancelAndJoin 向けに、持ち主の実行と破棄を待つ。
         * GameThread から呼び、世代登録を消して対象の全要求に Discard(Cancelled) を呼んでから返る。
         * Execute が終わらなければ返らない。 */
        void CancelOwnerAndJoin(uint64_t ownerKey);
        /** @brief 最上位ビットを立てた通し番号を、この列の持ち主の鍵として割り当てる。 */
        uint64_t AllocateOwnerKey();
        /** @brief GameThread で完了順に、取り消し・世代を再確認して配送し、配送した数を返す。
         * JobSystem の受付停止時は待機分を Rejected、実行の終了を待って残りを Shutdown で捨てるが、列は閉じない。
         * Execute が終わらなければ返らない。 */
        uint32_t DeliverCompleted();
        /** @brief GameThread から受付を閉じ、待機分を Shutdown で捨て、実行の終了を待って残りも Shutdown で捨てる。
         * 2 回目は何もしない。Execute が終わらなければ返らない。 */
        void Close();
        /** @brief Close により列の受付を閉じたかを返す。JobSystem の受付停止だけでは閉じない。 */
        bool IsClosed() const;
        /** @brief 上限が 0 なら 1 として扱う。 */
        void SetMaxConcurrent(uint32_t maxConcurrent);
        /** @brief Submitted は空でない投入の総数で、即時拒否も含む。 */
        FrameAsyncQueueStats GetStats() const;

    private:
        Core::Container::TSharedPtr<FrameAsyncQueueState> m_state;
    };
} // namespace NorvesLib::Thread
