#pragma once

#include "Rendering/TextureAsyncTypes.h"

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"
#include "Thread/Atomic.h"
#include "Thread/ConditionVariable.h"
#include "Thread/Mutex.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct TextureAssetLoadPlan;
    struct TextureAsyncLoadQueueShutdownTestAccess;

    class TextureAsyncLoadQueue
    {
    private:
        struct State;

    public:
        using RequestPtr = Container::TSharedPtr<TextureAsyncRequest>;
        using Callback = NorvesLib::Core::Delegate<void, TextureHandle>;
        using CallbackList = Container::VariableArray<Callback>;

        struct EnqueueResult
        {
            uint32_t RequestId = 0;
            bool bSubmitted = false;
        };

        class ActiveFlushGuard
        {
        public:
            ActiveFlushGuard() = default;
            ~ActiveFlushGuard();

            ActiveFlushGuard(const ActiveFlushGuard &) = delete;
            ActiveFlushGuard &operator=(const ActiveFlushGuard &) = delete;

            ActiveFlushGuard(ActiveFlushGuard &&other) noexcept;
            ActiveFlushGuard &operator=(ActiveFlushGuard &&other) noexcept;

            void Reset();

        private:
            friend class TextureAsyncLoadQueue;

            explicit ActiveFlushGuard(Container::TSharedPtr<State> state);

            Container::TSharedPtr<State> m_State;
        };

        class CallbackContextGuard
        {
        public:
            CallbackContextGuard();
            ~CallbackContextGuard();
            CallbackContextGuard(const CallbackContextGuard&) = delete;
            CallbackContextGuard& operator=(const CallbackContextGuard&) = delete;
        };

        struct CompletedBatch
        {
            Container::VariableArray<RequestPtr> Requests;
            ActiveFlushGuard Guard;
        };

        TextureAsyncLoadQueue();
        ~TextureAsyncLoadQueue();

        TextureAsyncLoadQueue(const TextureAsyncLoadQueue &) = delete;
        TextureAsyncLoadQueue &operator=(const TextureAsyncLoadQueue &) = delete;

        RequestPtr CreateRequest(const TextureAssetLoadPlan &plan,
                                 TextureAssetFallbackMode fallbackMode,
                                 Callback callback);

        uint32_t TryAppendDuplicate(const Container::String &cacheKey, Callback &callback);
        EnqueueResult EnqueueOrAppendDuplicateAndSubmit(const RequestPtr &request);

        CompletedBatch DetachCompletedRequests();
        CallbackList TakeCallbacksAndRelease(const RequestPtr &request);

        [[nodiscard]] uint32_t GetPendingCount() const;
        [[nodiscard]] bool HasPendingOrActiveFlush() const;
        [[nodiscard]] bool IsAccepting() const;
        [[nodiscard]] static bool IsInCallbackContext();

        void ClearPending();
        void CloseCancelAllAndWait();
        void Reopen();

    private:
        friend struct TextureAsyncLoadQueueShutdownTestAccess;

        void SetWaitHookForTesting(Delegate<void> hook);
        Container::TSharedPtr<State> m_State;
    };
}
