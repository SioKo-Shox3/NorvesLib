#include "Resource/ModelAsyncLoadQueue.h"

#include "Text/IdentityPool.h"
#include "Thread/JobSystem.h"

#include <utility>
#include <cassert>

namespace NorvesLib::Core::Resource
{
    namespace
    {
        thread_local uint32_t GModelCallbackDepth = 0;
    }

    struct ModelAsyncLoadQueue::State
    {
        Container::VariableArray<RequestPtr> Pending;
        Container::UnorderedMap<Identity, RequestPtr, Identity::Hasher> ByKey;
        Container::Map<uint32_t, RequestPtr> ById;
        uint32_t ActiveFlushCount = 0;
        bool bAccepting = true;
        mutable Thread::Mutex Mutex;
        Thread::ConditionVariable Condition;
        Delegate<void> WaitHookForTesting;
        Thread::Atomic<uint32_t> NextRequestId{1};
    };

    ModelAsyncLoadQueue::ActiveFlushGuard::ActiveFlushGuard(Container::TSharedPtr<State> state)
        : m_State(std::move(state))
    {
    }

    ModelAsyncLoadQueue::ActiveFlushGuard::~ActiveFlushGuard()
    {
        Reset();
    }

    ModelAsyncLoadQueue::ActiveFlushGuard::ActiveFlushGuard(ActiveFlushGuard&& other) noexcept
        : m_State(std::move(other.m_State))
    {
    }

    ModelAsyncLoadQueue::ActiveFlushGuard& ModelAsyncLoadQueue::ActiveFlushGuard::operator=(
        ActiveFlushGuard&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_State = std::move(other.m_State);
        }
        return *this;
    }

    void ModelAsyncLoadQueue::ActiveFlushGuard::Reset()
    {
        Container::TSharedPtr<State> state = std::move(m_State);
        if (!state)
        {
            return;
        }

        Thread::ScopedLock lock(state->Mutex);
        if (state->ActiveFlushCount > 0)
        {
            --state->ActiveFlushCount;
        }
        state->Condition.NotifyAll();
    }

    ModelAsyncLoadQueue::CallbackContextGuard::CallbackContextGuard()
    {
        ++GModelCallbackDepth;
    }

    ModelAsyncLoadQueue::CallbackContextGuard::~CallbackContextGuard()
    {
        --GModelCallbackDepth;
    }

    ModelAsyncLoadQueue::ModelAsyncLoadQueue()
        : m_State(Container::MakeShared<State>())
    {
    }

    ModelAsyncLoadQueue::~ModelAsyncLoadQueue()
    {
        CloseCancelAllAndWait();
    }

    ModelAsyncLoadQueue::RequestPtr ModelAsyncLoadQueue::CreateRequest(
        const CookedModelLoadPlan& plan,
        Callback callback)
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return nullptr;
        }

        auto request = Container::MakeShared<ModelAsyncLoadRequest>();
        request->RequestId = state->NextRequestId.FetchAdd(1, std::memory_order_relaxed);
        request->Plan = plan;
        if (callback.IsBound())
        {
            request->Callbacks.push_back(std::move(callback));
        }

        Container::TWeakPtr<ModelAsyncLoadRequest> weakRequest = request;
        request->Task = Thread::Task::Create([weakRequest]()
        {
            Container::TSharedPtr<ModelAsyncLoadRequest> locked = weakRequest.lock();
            if (!locked || locked->bCancelled.Load(std::memory_order_acquire))
            {
                return;
            }
            const bool bLoaded = LoadCookedModelForWorker(locked->Plan, locked->RequestId, locked->Result);
            (void)bLoaded;
        });
        return request;
    }

    uint32_t ModelAsyncLoadQueue::TryAppendDuplicate(
        const Container::String& cacheKey,
        Callback& callback)
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return 0;
        }

        Thread::ScopedLock lock(state->Mutex);
        if (!state->bAccepting)
        {
            return 0;
        }
        const Identity cacheIdentity(cacheKey);
        auto it = state->ByKey.find(cacheIdentity);
        if (it == state->ByKey.end() || !it->second || it->second->bCancelled.Load())
        {
            return 0;
        }
        if (callback.IsBound())
        {
            it->second->Callbacks.push_back(std::move(callback));
        }
        return it->second->RequestId;
    }

    ModelAsyncLoadQueue::EnqueueResult ModelAsyncLoadQueue::EnqueueOrAppendDuplicateAndSubmit(
        const RequestPtr& request)
    {
        if (!request || !request->Task)
        {
            return {};
        }

        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return {};
        }

        uint32_t duplicateId = 0;
        {
            Thread::ScopedLock lock(state->Mutex);
            if (!state->bAccepting)
            {
                return {};
            }
            const Identity cacheIdentity(request->Plan.CacheKey);
            auto duplicate = state->ByKey.find(cacheIdentity);
            if (duplicate != state->ByKey.end() && duplicate->second &&
                !duplicate->second->bCancelled.Load())
            {
                for (Callback& callback : request->Callbacks)
                {
                    if (callback.IsBound())
                    {
                        duplicate->second->Callbacks.push_back(std::move(callback));
                    }
                }
                duplicateId = duplicate->second->RequestId;
            }
            else
            {
                state->Pending.push_back(request);
                state->ByKey[cacheIdentity] = request;
                state->ById[request->RequestId] = request;
            }
        }

        if (duplicateId != 0)
        {
            request->Task.reset();
            return {duplicateId, false};
        }

        if (!Thread::JobSystem::Get().SubmitTask(request->Task))
        {
            Thread::ScopedLock lock(state->Mutex);
            request->bCancelled.Store(true, std::memory_order_release);
            request->Callbacks.clear();
            auto pending = std::find(state->Pending.begin(), state->Pending.end(), request);
            if (pending != state->Pending.end())
            {
                state->Pending.erase(pending);
            }
            const Identity cacheIdentity(request->Plan.CacheKey);
            auto keyIt = state->ByKey.find(cacheIdentity);
            if (keyIt != state->ByKey.end() && keyIt->second == request)
            {
                state->ByKey.erase(keyIt);
            }
            auto idIt = state->ById.find(request->RequestId);
            if (idIt != state->ById.end() && idIt->second == request)
            {
                state->ById.erase(idIt);
            }
            request->Task.reset();
            return {};
        }
        return {request->RequestId, true};
    }

    ModelAsyncLoadQueue::TerminalBatch ModelAsyncLoadQueue::DetachTerminal(uint32_t maxLoads)
    {
        TerminalBatch batch;
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return batch;
        }

        Thread::ScopedLock lock(state->Mutex);
        for (auto it = state->Pending.begin(); it != state->Pending.end();)
        {
            RequestPtr request = *it;
            const bool bTerminal = request && request->Task &&
                (request->Task->IsCompleted() || request->Task->IsCanceled());
            if (!bTerminal || (maxLoads != 0 && batch.Requests.size() >= maxLoads))
            {
                ++it;
                continue;
            }

            const Identity cacheIdentity(request->Plan.CacheKey);
            auto keyIt = state->ByKey.find(cacheIdentity);
            if (keyIt != state->ByKey.end() && keyIt->second == request)
            {
                state->ByKey.erase(keyIt);
            }
            auto idIt = state->ById.find(request->RequestId);
            if (idIt != state->ById.end() && idIt->second == request)
            {
                state->ById.erase(idIt);
            }
            request->Task.reset();
            batch.Requests.push_back(request);
            it = state->Pending.erase(it);
        }

        if (!batch.Requests.empty())
        {
            ++state->ActiveFlushCount;
            batch.Guard = ActiveFlushGuard(state);
        }
        return batch;
    }

    ModelAsyncLoadQueue::CallbackList ModelAsyncLoadQueue::TakeCallbacksAndRelease(const RequestPtr& request)
    {
        CallbackList callbacks;
        if (!request)
        {
            return callbacks;
        }
        callbacks = std::move(request->Callbacks);
        request->Callbacks.clear();
        return callbacks;
    }

    void ModelAsyncLoadQueue::Cancel(uint32_t requestId)
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return;
        }

        Thread::TaskPtr task;
        {
            Thread::ScopedLock lock(state->Mutex);
            auto it = state->ById.find(requestId);
            if (it == state->ById.end() || !it->second)
            {
                return;
            }
            RequestPtr request = it->second;
            request->bCancelled.Store(true, std::memory_order_release);
            request->Callbacks.clear();
            const Identity cacheIdentity(request->Plan.CacheKey);
            auto keyIt = state->ByKey.find(cacheIdentity);
            if (keyIt != state->ByKey.end() && keyIt->second == request)
            {
                state->ByKey.erase(keyIt);
            }
            task = request->Task;
        }
        if (task)
        {
            task->Cancel();
        }
    }

    bool ModelAsyncLoadQueue::CloseCancelAllAndWait()
    {
        if (IsInCallbackContext())
        {
            return false;
        }

        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return true;
        }

        Container::VariableArray<Thread::TaskPtr> tasks;
        {
            Thread::ScopedLock lock(state->Mutex);
            state->bAccepting = false;
            state->ByKey.clear();
            for (RequestPtr& request : state->Pending)
            {
                if (!request)
                {
                    continue;
                }
                request->bCancelled.Store(true, std::memory_order_release);
                request->Callbacks.clear();
                if (request->Task)
                {
                    tasks.push_back(request->Task);
                }
            }
        }

        for (Thread::TaskPtr& task : tasks)
        {
            task->Cancel();
            task->Wait();
        }

        Delegate<void> waitHook;
        {
            Thread::ScopedLock lock(state->Mutex);
            if (state->ActiveFlushCount != 0)
            {
                waitHook = std::move(state->WaitHookForTesting);
                state->WaitHookForTesting.Clear();
            }
        }
        waitHook.InvokeIfBound();

        {
            Thread::ScopedLock lock(state->Mutex);
            state->Condition.Wait(state->Mutex, [state]()
            {
                return state->ActiveFlushCount == 0;
            });
            for (RequestPtr& request : state->Pending)
            {
                if (request)
                {
                    request->Task.reset();
                }
            }
            state->Pending.clear();
            state->ById.clear();
        }
        return true;
    }

    void ModelAsyncLoadQueue::Reopen()
    {
        Container::TSharedPtr<State> state = m_State;
        if (state)
        {
            Thread::ScopedLock lock(state->Mutex);
            if (state->Pending.empty() && state->ActiveFlushCount == 0)
            {
                state->bAccepting = true;
            }
        }
    }

    uint32_t ModelAsyncLoadQueue::GetPendingCount() const
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return 0;
        }
        Thread::ScopedLock lock(state->Mutex);
        return static_cast<uint32_t>(state->Pending.size());
    }

    bool ModelAsyncLoadQueue::HasPendingOrActiveFlush() const
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return false;
        }
        Thread::ScopedLock lock(state->Mutex);
        return !state->Pending.empty() || state->ActiveFlushCount != 0;
    }

    bool ModelAsyncLoadQueue::IsAccepting() const
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return false;
        }
        Thread::ScopedLock lock(state->Mutex);
        return state->bAccepting;
    }

    bool ModelAsyncLoadQueue::IsInCallbackContext()
    {
        return GModelCallbackDepth != 0;
    }

    void ModelAsyncLoadQueue::SetWaitHookForTesting(Delegate<void> hook)
    {
        Container::TSharedPtr<State> state = m_State;
        if (!state)
        {
            return;
        }

        Thread::ScopedLock lock(state->Mutex);
        state->WaitHookForTesting = std::move(hook);
    }
} // namespace NorvesLib::Core::Resource
