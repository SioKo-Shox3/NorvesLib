#include "Resource/ModelHandleCache.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

int main()
{
    Resource::ModelHandleCache cache;
    const Container::String keyA = "asset:1:default:models/a.glb";
    const Container::String keyB = "asset:1:default:models/b.glb";
    const Rendering::ModelHandle handleA{101};
    const Rendering::ModelHandle handleB{102};
    const Rendering::ModelHandle candidate{103};

    Resource::ModelCachePublishResult published = cache.PublishOrAcquire(keyA, handleA, 2);
    assert(published.bPublished);
    assert(published.ResolvedHandle == handleA);
    assert(published.HandlesToRelease.empty());

    Resource::ModelCacheAcquireResult acquired = cache.Acquire(keyA, true);
    assert(acquired.bFound);
    assert(acquired.Handle == handleA);

    Resource::ModelCachePublishResult lostPublish = cache.PublishOrAcquire(keyA, candidate, 1);
    assert(!lostPublish.bPublished);
    assert(lostPublish.ResolvedHandle == handleA);
    assert(lostPublish.HandlesToRelease.size() == 1);
    assert(lostPublish.HandlesToRelease[0] == candidate);

    Resource::ModelCacheReleaseResult candidateRelease = cache.ReleaseLease(candidate);
    assert(candidateRelease.bManaged);
    assert(!candidateRelease.HandleToRelease.IsValid());

    assert(cache.PublishOrAcquire(keyB, handleB, 0).bPublished);
    Resource::ModelCacheHandleBatch retired = cache.RetireCurrent();
    assert(retired.HandlesToRelease.size() == 1);
    assert(retired.HandlesToRelease[0] == handleB);
    assert(!cache.Acquire(keyA, false).bFound);

    for (uint32_t lease = 0; lease < 4; ++lease)
    {
        Resource::ModelCacheReleaseResult release = cache.ReleaseLease(handleA);
        assert(release.bManaged);
        if (lease < 3)
        {
            assert(!release.HandleToRelease.IsValid());
        }
        else
        {
            assert(release.HandleToRelease == handleA);
        }
    }

    Resource::ModelCacheReleaseResult overRelease = cache.ReleaseLease(handleA);
    assert(overRelease.bManaged);
    assert(!overRelease.HandleToRelease.IsValid());

    Resource::ModelCacheReleaseResult unknown = cache.ReleaseLease(Rendering::ModelHandle{999});
    assert(!unknown.bManaged);
    assert(!unknown.HandleToRelease.IsValid());

    Resource::ModelCacheHandleBatch drained = cache.DrainAll();
    assert(drained.HandlesToRelease.empty());

    const Rendering::ModelHandle handleC{104};
    const Rendering::ModelHandle handleD{105};
    assert(cache.PublishOrAcquire(keyA, handleC, 0).bPublished);
    assert(cache.PublishOrAcquire(keyB, handleD, 1).bPublished);
    drained = cache.DrainAll();
    assert(drained.HandlesToRelease.size() == 2);
    assert(cache.ReleaseLease(handleC).bManaged);
    assert(cache.ReleaseLease(handleD).bManaged);

    std::cout << "ModelHandleCacheTest passed\n";
    return 0;
}
