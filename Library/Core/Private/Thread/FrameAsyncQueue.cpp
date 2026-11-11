// 優先度つきの待機列と、ワーカーの完了からゲームスレッドへの受け渡しを管理する。
#include "Thread/FrameAsyncQueue.h"
#include "Thread/JobSystem.h"
#include "Thread/ConditionVariable.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace NorvesLib::Thread
{
    using namespace Core::Container;

    struct FrameAsyncQueueState
    {
        enum class Phase
        {
            Pending,
            Running,
            Completed,
            Finalized
        };

        struct Request
        {
            uint64_t Id = 0;
            FrameAsyncOwner Owner;
            int32_t Priority = 0;
            size_t HeapIndex = 0;
            size_t OwnerIndex = 0;
            size_t RunningIndex = 0;
            Phase CurrentPhase = Phase::Pending;
            FrameAsyncOutcome Outcome = FrameAsyncOutcome::Delivered;
            Atomic<bool> bCancelRequested{false};
            bool bPublishingTask = false;
            TUniquePtr<IFrameAsyncJob> Job;
            TaskPtr Task;
        };
        using RequestPtr = TSharedPtr<Request>;
        using Requests = VariableArray<RequestPtr>;

        explicit FrameAsyncQueueState(JobSystem* jobs, uint32_t maxConcurrent)
            : Jobs(jobs), MaxConcurrent(std::max(1u, maxConcurrent))
        {
        }

        JobSystem& GetJobs() const
        {
            return Jobs ? *Jobs : JobSystem::Get();
        }

        static bool IsHigher(const RequestPtr& left, const RequestPtr& right)
        {
            return left->Priority > right->Priority ||
                (left->Priority == right->Priority && left->Id < right->Id);
        }

        void SwapHeap(size_t left, size_t right)
        {
            std::swap(Pending[left], Pending[right]);
            Pending[left]->HeapIndex = left;
            Pending[right]->HeapIndex = right;
        }

        size_t SiftUp(size_t index)
        {
            while (index > 0 && IsHigher(Pending[index], Pending[(index - 1) / 2]))
            {
                const size_t parent = (index - 1) / 2;
                SwapHeap(index, parent);
                index = parent;
            }
            return index;
        }

        void SiftDown(size_t index)
        {
            while (index * 2 + 1 < Pending.size())
            {
                size_t child = index * 2 + 1;
                if (child + 1 < Pending.size() && IsHigher(Pending[child + 1], Pending[child]))
                {
                    ++child;
                }
                if (!IsHigher(Pending[child], Pending[index]))
                {
                    break;
                }
                SwapHeap(index, child);
                index = child;
            }
        }

        void RemovePending(size_t index)
        {
            SwapHeap(index, Pending.size() - 1);
            Pending.pop_back();
            if (index < Pending.size())
            {
                SiftDown(SiftUp(index));
            }
        }

        // 順序が不要な索引は末尾と交換し、移した要求の位置も更新する。Gate を保持して呼ぶ。
        void RemoveRunning(size_t index)
        {
            std::swap(Running[index], Running.back());
            Running[index]->RunningIndex = index;
            Running.pop_back();
        }

        void RemoveOwnerRequest(const RequestPtr& request)
        {
            const auto owner = ByOwner.find(request->Owner.Key);
            auto& requests = owner->second;
            const size_t index = request->OwnerIndex;
            std::swap(requests[index], requests.back());
            requests[index]->OwnerIndex = index;
            requests.pop_back();
            if (requests.empty())
            {
                ByOwner.erase(owner);
            }
        }

        // 以下の状態操作は Gate を保持して呼ぶ。外部 API とコールバックは保持中に呼ばない。
        void RefreshGeneration(const RequestPtr& request)
        {
            const auto generation = Generations.find(request->Owner.Key);
            if ((request->Outcome == FrameAsyncOutcome::Delivered || request->Outcome == FrameAsyncOutcome::Failed) &&
                generation != Generations.end() && generation->second != request->Owner.Generation)
            {
                request->Outcome = FrameAsyncOutcome::Stale;
                request->bCancelRequested.Store(true);
            }
        }

        void Complete(const RequestPtr& request, FrameAsyncOutcome outcome)
        {
            if (request->CurrentPhase == Phase::Pending)
            {
                RemovePending(request->HeapIndex);
            }
            else if (request->CurrentPhase == Phase::Running)
            {
                RemoveRunning(request->RunningIndex);
                --Stats.Running;
            }
            else
            {
                return;
            }
            if (request->Outcome == FrameAsyncOutcome::Delivered)
            {
                request->Outcome = outcome;
            }
            request->CurrentPhase = Phase::Completed;
            request->bPublishingTask = false;
            // タスクのラムダが状態と要求を保持するので、逆向きの所有をここで解く。
            request->Task.reset();
            Completed.push_back(request);
            ++Stats.Completed;
            Published.NotifyAll();
        }

        void MarkCancelled(const RequestPtr& request)
        {
            request->Outcome = FrameAsyncOutcome::Cancelled;
            request->bCancelRequested.Store(true);
            if (request->CurrentPhase == Phase::Pending)
            {
                Complete(request, FrameAsyncOutcome::Cancelled);
            }
        }

        static void Pump(const TSharedPtr<FrameAsyncQueueState>& state,
                         uint32_t limit = std::numeric_limits<uint32_t>::max())
        {
            uint32_t accepted = 0;
            while (accepted < limit)
            {
                RequestPtr request;
                {
                    ScopedLock lock(state->Gate);
                    if (state->bClosed || state->bDraining || state->Pending.empty() ||
                        state->Stats.Running >= state->MaxConcurrent)
                    {
                        return;
                    }
                    request = state->Pending.front();
                    state->RefreshGeneration(request);
                    if (request->Outcome != FrameAsyncOutcome::Delivered)
                    {
                        state->Complete(request, request->Outcome);
                        continue;
                    }
                    request->RunningIndex = state->Running.size();
                    state->Running.push_back(request);
                    state->RemovePending(0);
                    request->CurrentPhase = Phase::Running;
                    request->bPublishingTask = true;
                    ++state->Stats.Running;
                    state->Stats.PeakRunning = std::max(state->Stats.PeakRunning, state->Stats.Running);
                }

                bool bAccepted = false;
                try
                {
                    auto task = Task::Create([state, request]()
                    {
                        FrameAsyncOutcome outcome = FrameAsyncOutcome::Delivered;
                        try
                        {
                            request->Job->Execute(FrameAsyncExecuteContext(request->bCancelRequested));
                        }
                        catch (...)
                        {
                            outcome = FrameAsyncOutcome::Failed;
                        }
                        {
                            ScopedLock lock(state->Gate);
                            state->Complete(request, outcome);
                        }
                        // 完了公開後に触るのは、共有状態を保持して次の一件を引く処理だけ。
                        Pump(state, 1);
                    });
                    {
                        ScopedLock lock(state->Gate);
                        request->Task = task;
                    }
                    bAccepted = state->GetJobs().IsAcceptingTasks() && state->GetJobs().SubmitTask(task);
                }
                catch (...)
                {
                    // 作成や提出が失敗しても、予約した実行枠と要求を必ず回収する。
                    bAccepted = false;
                }
                {
                    ScopedLock lock(state->Gate);
                    if (!bAccepted)
                    {
                        request->Outcome = FrameAsyncOutcome::Rejected;
                        state->Complete(request, FrameAsyncOutcome::Rejected);
                    }
                    else
                    {
                        // 提出を拒まれた Task は未着手のままなので、受理の確定まで待機側へ渡さない。
                        request->bPublishingTask = false;
                        state->Published.NotifyAll();
                        ++accepted;
                    }
                }
            }
        }

        void ReapCancelledTasks()
        {
            size_t index = 0;
            while (index < Running.size())
            {
                // Complete が末尾の要素をこの位置へ移すため、要求を保持し、回収時は位置を進めない。
                const auto request = Running[index];
                if (request->Task && request->Task->IsCanceled())
                {
                    request->Outcome = FrameAsyncOutcome::Rejected;
                    Complete(request, FrameAsyncOutcome::Rejected);
                }
                else
                {
                    ++index;
                }
            }
        }

        void Wait(const Requests& requests)
        {
            for (const auto& request : requests)
            {
                TaskPtr task;
                {
                    ScopedLock lock(Gate);
                    // 予約から提出結果の確定までだけ条件変数で待つ。実行の終わりは Task::Wait に任せる。
                    Published.Wait(Gate, [&request]() { return !request->bPublishingTask; });
                    if (request->CurrentPhase == Phase::Running)
                    {
                        task = request->Task;
                    }
                }
                if (task)
                {
                    task->Wait();
                    ScopedLock lock(Gate);
                    if (request->CurrentPhase == Phase::Running && task->IsCanceled())
                    {
                        Complete(request, FrameAsyncOutcome::Shutdown);
                    }
                }
            }
        }

        static void Invoke(TUniquePtr<IFrameAsyncJob> job, FrameAsyncOutcome outcome)
        {
            try
            {
                if (outcome == FrameAsyncOutcome::Delivered)
                {
                    job->Deliver();
                }
                else
                {
                    job->Discard(outcome);
                }
            }
            catch (...)
            {
                LOG_ERROR("非同期の結果の受け渡し中に例外が発生しました");
            }
        }

        bool Finalize(const RequestPtr& request, bool bForce = false,
                      FrameAsyncOutcome forced = FrameAsyncOutcome::Shutdown)
        {
            TUniquePtr<IFrameAsyncJob> job;
            FrameAsyncOutcome outcome;
            {
                ScopedLock lock(Gate);
                if (request->CurrentPhase != Phase::Completed)
                {
                    return false;
                }
                RefreshGeneration(request);
                outcome = bForce ? forced : request->Outcome;
                request->CurrentPhase = Phase::Finalized;
                job = std::move(request->Job);
                RemoveOwnerRequest(request);
                All.erase(request->Id);
                --Stats.Completed;
                if (outcome == FrameAsyncOutcome::Delivered)
                {
                    ++Stats.Delivered;
                }
                else
                {
                    ++Stats.Discarded;
                }
            }
            Invoke(std::move(job), outcome);
            return outcome == FrameAsyncOutcome::Delivered;
        }

        void Drain(bool bClose)
        {
            Requests pending;
            Requests remaining;
            {
                ScopedLock lock(Gate);
                if (bClosed || bDraining)
                {
                    return;
                }
                bClosed = bClose;
                bDraining = true;
                for (const auto& entry : All)
                {
                    const auto& request = entry.second;
                    request->bCancelRequested.Store(true);
                    if (request->CurrentPhase == Phase::Pending)
                    {
                        request->Outcome = bClose ? FrameAsyncOutcome::Shutdown : FrameAsyncOutcome::Rejected;
                        Complete(request, request->Outcome);
                        pending.push_back(request);
                    }
                    else
                    {
                        request->Outcome = FrameAsyncOutcome::Shutdown;
                        remaining.push_back(request);
                    }
                }
            }
            for (const auto& request : pending)
            {
                Finalize(request, true, bClose ? FrameAsyncOutcome::Shutdown : FrameAsyncOutcome::Rejected);
            }
            Wait(remaining);
            for (const auto& request : remaining)
            {
                Finalize(request, true);
            }
            {
                ScopedLock lock(Gate);
                Completed.clear();
                bDraining = false;
            }
        }

        JobSystem* Jobs;
        mutable Mutex Gate;
        ConditionVariable Published;
        Requests Pending;
        Requests Running;
        Requests Completed;
        UnorderedMap<uint64_t, RequestPtr> All;
        UnorderedMap<uint64_t, Requests> ByOwner;
        UnorderedMap<uint64_t, uint32_t> Generations;
        FrameAsyncQueueStats Stats;
        uint32_t MaxConcurrent;
        uint64_t NextId = 1;
        uint64_t NextOwnerKey = 1;
        bool bClosed = false;
        bool bDraining = false;
    };

    FrameAsyncExecuteContext::FrameAsyncExecuteContext(const Atomic<bool>& cancelRequested)
        : m_cancelRequested(cancelRequested)
    {
    }

    bool FrameAsyncExecuteContext::IsCancelRequested() const
    {
        return m_cancelRequested.Load();
    }

    FrameAsyncQueue::FrameAsyncQueue(JobSystem* jobs, FrameAsyncQueueSettings settings)
        : m_state(MakeShared<FrameAsyncQueueState>(jobs, settings.MaxConcurrent))
    {
    }

    FrameAsyncQueue::~FrameAsyncQueue()
    {
        Close();
    }

    FrameAsyncRequestId FrameAsyncQueue::Submit(TUniquePtr<IFrameAsyncJob> job, int32_t priority, FrameAsyncOwner owner)
    {
        if (!job)
        {
            return {};
        }
        const auto state = m_state;
        const bool bAccepting = state->GetJobs().IsAcceptingTasks();
        FrameAsyncRequestId id;
        {
            ScopedLock lock(state->Gate);
            ++state->Stats.Submitted;
            if (state->bClosed || state->bDraining || !bAccepting)
            {
                ++state->Stats.Discarded;
            }
            else
            {
                auto request = MakeShared<FrameAsyncQueueState::Request>();
                id.Value = request->Id = state->NextId++;
                request->Priority = priority;
                request->Owner = owner;
                request->Job = std::move(job);
                state->All.emplace(request->Id, request);
                auto& ownerRequests = state->ByOwner[owner.Key];
                request->OwnerIndex = ownerRequests.size();
                ownerRequests.push_back(request);
                request->HeapIndex = state->Pending.size();
                state->Pending.push_back(request);
                state->SiftUp(request->HeapIndex);
                state->RefreshGeneration(request);
                if (request->Outcome == FrameAsyncOutcome::Stale)
                {
                    state->Complete(request, FrameAsyncOutcome::Stale);
                }
            }
        }
        if (!id.IsValid())
        {
            FrameAsyncQueueState::Invoke(std::move(job), FrameAsyncOutcome::Rejected);
        }
        else
        {
            FrameAsyncQueueState::Pump(state);
        }
        return id;
    }

    bool FrameAsyncQueue::Cancel(FrameAsyncRequestId id)
    {
        ScopedLock lock(m_state->Gate);
        const auto found = m_state->All.find(id.Value);
        if (found == m_state->All.end())
        {
            return false;
        }
        m_state->MarkCancelled(found->second);
        return true;
    }

    bool FrameAsyncQueue::SetPriority(FrameAsyncRequestId id, int32_t priority)
    {
        const auto state = m_state;
        {
            ScopedLock lock(state->Gate);
            const auto found = state->All.find(id.Value);
            if (found == state->All.end() || found->second->CurrentPhase != FrameAsyncQueueState::Phase::Pending)
            {
                return false;
            }
            auto& request = found->second;
            request->Priority = priority;
            state->SiftDown(state->SiftUp(request->HeapIndex));
        }
        FrameAsyncQueueState::Pump(state);
        return true;
    }

    void FrameAsyncQueue::SetOwnerGeneration(uint64_t ownerKey, uint32_t generation)
    {
        ScopedLock lock(m_state->Gate);
        m_state->Generations[ownerKey] = generation;
        const auto owner = m_state->ByOwner.find(ownerKey);
        if (owner == m_state->ByOwner.end())
        {
            return;
        }
        // Complete は持ち主の索引を変更せず、Gate 内では Finalize も並行しない。
        for (const auto& request : owner->second)
        {
            m_state->RefreshGeneration(request);
            if (request->CurrentPhase == FrameAsyncQueueState::Phase::Pending &&
                request->Outcome == FrameAsyncOutcome::Stale)
            {
                m_state->Complete(request, FrameAsyncOutcome::Stale);
            }
        }
    }

    void FrameAsyncQueue::CancelOwner(uint64_t ownerKey)
    {
        ScopedLock lock(m_state->Gate);
        m_state->Generations.erase(ownerKey);
        const auto owner = m_state->ByOwner.find(ownerKey);
        if (owner == m_state->ByOwner.end())
        {
            return;
        }
        for (const auto& request : owner->second)
        {
            m_state->MarkCancelled(request);
        }
    }

    void FrameAsyncQueue::CancelOwnerAndJoin(uint64_t ownerKey)
    {
        const auto state = m_state;
        FrameAsyncQueueState::Requests requests;
        {
            ScopedLock lock(state->Gate);
            state->Generations.erase(ownerKey);
            const auto owner = state->ByOwner.find(ownerKey);
            if (owner == state->ByOwner.end())
            {
                return;
            }
            // 待機と Finalize の間は Gate を解放するため、再入でも無効にならない一覧を保持する。
            requests = owner->second;
            for (const auto& request : requests)
            {
                state->MarkCancelled(request);
            }
        }
        state->Wait(requests);
        for (const auto& request : requests)
        {
            state->Finalize(request, true, FrameAsyncOutcome::Cancelled);
        }
    }

    uint64_t FrameAsyncQueue::AllocateOwnerKey()
    {
        ScopedLock lock(m_state->Gate);
        return (uint64_t{1} << 63) | m_state->NextOwnerKey++;
    }

    uint32_t FrameAsyncQueue::DeliverCompleted()
    {
        const auto state = m_state;
        if (!state->GetJobs().IsAcceptingTasks())
        {
            state->Drain(false);
            return 0;
        }
        FrameAsyncQueueState::Requests completed;
        {
            ScopedLock lock(state->Gate);
            state->ReapCancelledTasks();
            completed.swap(state->Completed);
        }
        uint32_t delivered = 0;
        for (const auto& request : completed)
        {
            if (state->Finalize(request))
            {
                ++delivered;
            }
        }
        FrameAsyncQueueState::Pump(state);
        return delivered;
    }

    void FrameAsyncQueue::Close()
    {
        m_state->Drain(true);
    }

    bool FrameAsyncQueue::IsClosed() const
    {
        ScopedLock lock(m_state->Gate);
        return m_state->bClosed;
    }

    void FrameAsyncQueue::SetMaxConcurrent(uint32_t maxConcurrent)
    {
        const auto state = m_state;
        {
            ScopedLock lock(state->Gate);
            state->MaxConcurrent = std::max(1u, maxConcurrent);
        }
        FrameAsyncQueueState::Pump(state);
    }

    FrameAsyncQueueStats FrameAsyncQueue::GetStats() const
    {
        ScopedLock lock(m_state->Gate);
        auto stats = m_state->Stats;
        stats.Pending = static_cast<uint32_t>(m_state->Pending.size());
        return stats;
    }
} // namespace NorvesLib::Thread
