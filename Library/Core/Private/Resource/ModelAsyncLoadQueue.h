#pragma once

#include "Resource/ModelAssetLoader.h"

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"
#include "Thread/Atomic.h"
#include "Thread/ConditionVariable.h"
#include "Thread/Mutex.h"
#include "Thread/Task.h"

#include <cstdint>

namespace NorvesLib::Core::Resource
{
    struct ModelAsyncLoadRequest
    {
        uint32_t RequestId = 0;
        CookedModelLoadPlan Plan;
        CookedModelCpuLoadResult Result;
        Container::VariableArray<Delegate<void, Rendering::ModelHandle>> Callbacks;
        Thread::TaskPtr Task;
        Thread::Atomic<bool> bCancelled{false};
    };

    class ModelAsyncLoadQueue final
    {
    private:
        struct State;

    public:
        using RequestPtr = Container::TSharedPtr<ModelAsyncLoadRequest>;
        using Callback = Delegate<void, Rendering::ModelHandle>;
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
            ActiveFlushGuard(const ActiveFlushGuard&) = delete;
            ActiveFlushGuard& operator=(const ActiveFlushGuard&) = delete;
            ActiveFlushGuard(ActiveFlushGuard&& other) noexcept;
            ActiveFlushGuard& operator=(ActiveFlushGuard&& other) noexcept;
            void Reset();

        private:
            friend class ModelAsyncLoadQueue;
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

        struct TerminalBatch
        {
            Container::VariableArray<RequestPtr> Requests;
            ActiveFlushGuard Guard;
        };

        ModelAsyncLoadQueue();
        ~ModelAsyncLoadQueue();
        ModelAsyncLoadQueue(const ModelAsyncLoadQueue&) = delete;
        ModelAsyncLoadQueue& operator=(const ModelAsyncLoadQueue&) = delete;

        RequestPtr CreateRequest(const CookedModelLoadPlan& plan, Callback callback);
        uint32_t TryAppendDuplicate(const Container::String& cacheKey, Callback& callback);
        EnqueueResult EnqueueOrAppendDuplicateAndSubmit(const RequestPtr& request);
        TerminalBatch DetachTerminal(uint32_t maxLoads);
        CallbackList TakeCallbacksAndRelease(const RequestPtr& request);
        void Cancel(uint32_t requestId);
        bool CloseCancelAllAndWait();
        void Reopen();
        [[nodiscard]] uint32_t GetPendingCount() const;
        [[nodiscard]] bool HasPendingOrActiveFlush() const;
        [[nodiscard]] bool IsAccepting() const;
        [[nodiscard]] static bool IsInCallbackContext();

    private:
        Container::TSharedPtr<State> m_State;
    };
} // namespace NorvesLib::Core::Resource
