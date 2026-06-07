#include "Rendering/TextureAsyncLoadQueue.h"

#include "Rendering/TextureAssetResolver.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"

#include <utility>

namespace NorvesLib::Core::Rendering
{
    struct TextureAsyncLoadQueue::State
    {
        Container::VariableArray<TextureAsyncLoadQueue::RequestPtr> PendingTextureLoads;
        Container::Map<Container::String, TextureAsyncLoadQueue::RequestPtr> PendingTextureLoadsByPath;
        uint32_t ActiveTextureLoadFlushCount = 0;
        mutable Thread::Mutex AsyncLoadMutex;
        Thread::Atomic<uint32_t> NextAsyncRequestId{1};
    };

    TextureAsyncLoadQueue::ActiveFlushGuard::ActiveFlushGuard(Container::TSharedPtr<State> state)
        : m_State(std::move(state))
    {
    }

    TextureAsyncLoadQueue::ActiveFlushGuard::~ActiveFlushGuard()
    {
        Reset();
    }

    TextureAsyncLoadQueue::ActiveFlushGuard::ActiveFlushGuard(ActiveFlushGuard &&other) noexcept
        : m_State(std::move(other.m_State))
    {
    }

    TextureAsyncLoadQueue::ActiveFlushGuard &TextureAsyncLoadQueue::ActiveFlushGuard::operator=(
        ActiveFlushGuard &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_State = std::move(other.m_State);
        }

        return *this;
    }

    void TextureAsyncLoadQueue::ActiveFlushGuard::Reset()
    {
        auto state = std::move(m_State);
        if (!state)
        {
            return;
        }

        {
            Thread::ScopedLock lock(state->AsyncLoadMutex);
            if (state->ActiveTextureLoadFlushCount > 0)
            {
                --state->ActiveTextureLoadFlushCount;
            }
        }
    }

    TextureAsyncLoadQueue::TextureAsyncLoadQueue()
        : m_State(Container::MakeShared<State>())
    {
    }

    TextureAsyncLoadQueue::~TextureAsyncLoadQueue() = default;

    TextureAsyncLoadQueue::RequestPtr TextureAsyncLoadQueue::CreateRequest(
        const TextureAssetLoadPlan &plan,
        TextureAssetFallbackMode fallbackMode,
        Callback callback)
    {
        auto state = m_State;
        if (!state)
        {
            return nullptr;
        }

        auto request = Container::MakeShared<RenderResourceManager::AsyncTextureRequest>();
        request->RequestId = state->NextAsyncRequestId.FetchAdd(1, std::memory_order_relaxed);
        request->Path = plan.RequestPath;
        request->CacheKey = plan.CacheKey;
        request->Result.Path = plan.RequestPath;
        request->Result.ResolvedPath = plan.ResolvedPath;
        request->Result.CacheKey = plan.CacheKey;
        request->Result.LogicalPath = plan.LogicalPath;
        request->Result.AssetGeneration = plan.Generation;
        request->Result.FallbackMode = fallbackMode;

        if (callback.IsBound())
        {
            request->Callbacks.push_back(std::move(callback));
        }

        return request;
    }

    uint32_t TextureAsyncLoadQueue::TryAppendDuplicate(const Container::String &cacheKey, Callback &callback)
    {
        auto state = m_State;
        if (!state)
        {
            return 0;
        }

        Thread::ScopedLock lock(state->AsyncLoadMutex);
        auto pendingIt = state->PendingTextureLoadsByPath.find(cacheKey);
        if (pendingIt == state->PendingTextureLoadsByPath.end() || !pendingIt->second)
        {
            return 0;
        }

        auto &pendingRequest = pendingIt->second;
        if (callback.IsBound())
        {
            pendingRequest->Callbacks.push_back(std::move(callback));
        }

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_async_duplicate_collapsed role=caller request_id=%u cache_key=\"%s\" completed=%d",
                        static_cast<unsigned int>(pendingRequest->RequestId),
                        cacheKey.c_str(),
                        (pendingRequest->Task && pendingRequest->Task->IsCompleted()) ? 1 : 0);
        return pendingRequest->RequestId;
    }

    TextureAsyncLoadQueue::EnqueueResult TextureAsyncLoadQueue::EnqueueOrAppendDuplicateAndSubmit(
        const RequestPtr &request)
    {
        if (!request)
        {
            return {};
        }

        auto state = m_State;
        if (!state)
        {
            return {};
        }

        uint32_t duplicateRequestId = 0;

        {
            Thread::ScopedLock lock(state->AsyncLoadMutex);
            auto pendingIt = state->PendingTextureLoadsByPath.find(request->CacheKey);
            if (pendingIt != state->PendingTextureLoadsByPath.end() && pendingIt->second)
            {
                auto &pendingRequest = pendingIt->second;
                for (auto &pendingCallback : request->Callbacks)
                {
                    if (pendingCallback.IsBound())
                    {
                        pendingRequest->Callbacks.push_back(std::move(pendingCallback));
                    }
                }

                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_async_duplicate_collapsed role=caller request_id=%u cache_key=\"%s\" completed=%d insert_recheck=1",
                                static_cast<unsigned int>(pendingRequest->RequestId),
                                request->CacheKey.c_str(),
                                (pendingRequest->Task && pendingRequest->Task->IsCompleted()) ? 1 : 0);
                duplicateRequestId = pendingRequest->RequestId;
            }
            else
            {
                state->PendingTextureLoads.push_back(request);
                state->PendingTextureLoadsByPath[request->CacheKey] = request;
            }
        }

        if (duplicateRequestId != 0)
        {
            request->Task.reset();
            return {duplicateRequestId, false};
        }

        Thread::JobSystem::Get().SubmitTask(request->Task);
        return {request->RequestId, true};
    }

    TextureAsyncLoadQueue::CompletedBatch TextureAsyncLoadQueue::DetachCompletedRequests()
    {
        CompletedBatch batch;
        auto state = m_State;
        if (!state)
        {
            return batch;
        }

        Thread::ScopedLock lock(state->AsyncLoadMutex);
        for (auto it = state->PendingTextureLoads.begin(); it != state->PendingTextureLoads.end();)
        {
            auto &request = *it;
            if (!request || !request->Task || !request->Task->IsCompleted())
            {
                ++it;
                continue;
            }

            batch.Requests.push_back(request);
            it = state->PendingTextureLoads.erase(it);
        }

        if (!batch.Requests.empty())
        {
            ++state->ActiveTextureLoadFlushCount;
            batch.Guard = ActiveFlushGuard(state);
        }

        return batch;
    }

    TextureAsyncLoadQueue::CallbackList TextureAsyncLoadQueue::TakeCallbacksAndRelease(const RequestPtr &request)
    {
        CallbackList callbacks;
        if (!request)
        {
            return callbacks;
        }

        auto state = m_State;
        if (state)
        {
            Thread::ScopedLock lock(state->AsyncLoadMutex);
            auto pendingIt = state->PendingTextureLoadsByPath.find(request->CacheKey);
            if (pendingIt != state->PendingTextureLoadsByPath.end() && pendingIt->second == request)
            {
                state->PendingTextureLoadsByPath.erase(pendingIt);
            }
        }

        callbacks = std::move(request->Callbacks);
        request->Callbacks.clear();
        return callbacks;
    }

    uint32_t TextureAsyncLoadQueue::GetPendingCount() const
    {
        auto state = m_State;
        if (!state)
        {
            return 0;
        }

        Thread::ScopedLock lock(state->AsyncLoadMutex);
        return static_cast<uint32_t>(state->PendingTextureLoads.size());
    }

    bool TextureAsyncLoadQueue::HasPendingOrActiveFlush() const
    {
        auto state = m_State;
        if (!state)
        {
            return false;
        }

        Thread::ScopedLock lock(state->AsyncLoadMutex);
        return !state->PendingTextureLoads.empty() || state->ActiveTextureLoadFlushCount != 0;
    }

    void TextureAsyncLoadQueue::ClearPending()
    {
        auto state = m_State;
        if (!state)
        {
            return;
        }

        Thread::ScopedLock lock(state->AsyncLoadMutex);
        state->PendingTextureLoads.clear();
        state->PendingTextureLoadsByPath.clear();
    }
}
