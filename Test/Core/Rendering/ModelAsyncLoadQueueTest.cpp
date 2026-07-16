#include "Resource/ModelAsyncLoadQueue.h"

#include "Asset/AssetSystem.h"
#include "Thread/JobSystem.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    Resource::CookedModelLoadPlan MakePlan(
        const Container::TSharedPtr<const Asset::AssetSystem>& snapshot,
        const Container::String& key)
    {
        Resource::CookedModelLoadPlan plan;
        plan.AssetSystem = snapshot;
        plan.RequestPath = "models/a.glb";
        plan.NormalizedLogicalPath = "models/a.glb";
        plan.CacheKey = key;
        plan.Generation = 7;
        return plan;
    }
}

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(1, NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);

    Resource::ModelAsyncLoadQueue queue;
    assert(queue.GetPendingCount() == 0);
    auto snapshot = Container::MakeShared<Asset::AssetSystem>();
    Container::TWeakPtr<const Asset::AssetSystem> weakSnapshot = snapshot;
    Resource::CookedModelLoadPlan plan = MakePlan(snapshot, "asset:7:default:models/a.glb");
    snapshot.reset();

    bool bFirstCalled = false;
    Resource::ModelAsyncLoadQueue::Callback firstCallback(
        [&bFirstCalled](Rendering::ModelHandle)
        {
            bFirstCalled = true;
        });
    auto request = queue.CreateRequest(plan, std::move(firstCallback));
    auto enqueued = queue.EnqueueOrAppendDuplicateAndSubmit(request);
    assert(enqueued.bSubmitted);
    assert(enqueued.RequestId != 0);
    assert(!weakSnapshot.expired());

    bool bDuplicateCalled = false;
    Resource::ModelAsyncLoadQueue::Callback duplicateCallback(
        [&bDuplicateCalled](Rendering::ModelHandle)
        {
            bDuplicateCalled = true;
        });
    uint32_t duplicateId = queue.TryAppendDuplicate(plan.CacheKey, duplicateCallback);
    assert(duplicateId == enqueued.RequestId);

    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    auto batch = queue.DetachTerminal(0);
    assert(batch.Requests.size() == 1);
    assert(queue.GetPendingCount() == 0);
    assert(queue.HasPendingOrActiveFlush());
    auto callbacks = queue.TakeCallbacksAndRelease(batch.Requests[0]);
    assert(callbacks.size() == 2);
    for (auto& callback : callbacks)
    {
        callback(Rendering::ModelHandle::Invalid());
    }
    assert(bFirstCalled && bDuplicateCalled);
    assert(queue.DetachTerminal(0).Requests.empty());
    callbacks.clear();
    batch.Requests.clear();
    batch.Guard.Reset();
    request.reset();
    plan.AssetSystem.reset();
    assert(weakSnapshot.expired());
    assert(!queue.HasPendingOrActiveFlush());

    auto secondSnapshot = Container::MakeShared<Asset::AssetSystem>();
    Container::TWeakPtr<const Asset::AssetSystem> weakSecondSnapshot = secondSnapshot;
    Resource::CookedModelLoadPlan secondPlan = MakePlan(secondSnapshot, "asset:7:default:models/cancel.glb");
    bool bCancelledCallbackCalled = false;
    auto cancelledRequest = queue.CreateRequest(
        secondPlan,
        [&bCancelledCallbackCalled](Rendering::ModelHandle)
        {
            bCancelledCallbackCalled = true;
        });
    const uint32_t cancelledId = queue.EnqueueOrAppendDuplicateAndSubmit(cancelledRequest).RequestId;
    queue.Cancel(cancelledId);

    auto freshRequest = queue.CreateRequest(secondPlan, {});
    const uint32_t freshId = queue.EnqueueOrAppendDuplicateAndSubmit(freshRequest).RequestId;
    assert(freshId != 0);
    assert(freshId != cancelledId);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    auto terminal = queue.DetachTerminal(0);
    assert(terminal.Requests.size() == 2);
    assert(cancelledRequest->bCancelled.Load());
    assert(!bCancelledCallbackCalled);
    assert(queue.GetPendingCount() == 0);
    terminal.Requests.clear();
    terminal.Guard.Reset();
    cancelledRequest.reset();
    freshRequest.reset();
    secondPlan.AssetSystem.reset();
    secondSnapshot.reset();
    assert(weakSecondSnapshot.expired());

    {
        Resource::ModelAsyncLoadQueue::CallbackContextGuard callbackContext;
        assert(!queue.CloseCancelAllAndWait());
    }
    assert(queue.CloseCancelAllAndWait());
    assert(!queue.IsAccepting());
    assert(!queue.EnqueueOrAppendDuplicateAndSubmit(queue.CreateRequest(secondPlan, {})).bSubmitted);
    queue.Reopen();
    assert(queue.IsAccepting());

    assert(queue.CloseCancelAllAndWait());
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "ModelAsyncLoadQueueTest passed\n";
    return 0;
}
