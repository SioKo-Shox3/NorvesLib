#include "Object/Resource.h"
#include "Object/ResourceRegistry.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

int main()
{
    std::cout << "ResourceRegistryTest start\n";

    ResourceRegistry registry;
    assert(registry.Initialize());

    auto transient = registry.CreateTransient<Resource>("TransientA");
    assert(transient != nullptr);
    assert(transient->GetResourceId() != 0);

    ResourceHandle<Resource> transientHandle = registry.GetHandle<Resource>(transient->GetResourceId());
    assert(transientHandle.IsValid());
    assert(registry.Resolve(transientHandle) == transient);
    assert(registry.Get<Resource>(transient->GetResourceId()) == transient);

    const Container::String path = "Assets/Data/TestResource.resource";
    auto loaded = registry.Load<Resource>(path);
    assert(loaded != nullptr);
    assert(loaded->IsLoaded());

    ResourceHandle<Resource> loadedHandle = registry.FindHandle<Resource>(path);
    assert(loadedHandle.IsValid());
    assert(registry.Resolve(loadedHandle) == loaded);

    auto loadedAgain = registry.Load<Resource>(path);
    assert(loadedAgain == loaded);
    assert(registry.GetResourceCount() == 2);
    assert(registry.GetCachedPathCount() == 1);

    transient.reset();
    loaded.reset();
    loadedAgain.reset();

    const size_t removedCount = registry.CollectGarbage();
    assert(removedCount == 2);
    assert(registry.GetResourceCount() == 0);
    assert(!registry.Resolve(loadedHandle));

    registry.Shutdown();

    std::cout << "ResourceRegistryTest passed\n";
    return 0;
}
