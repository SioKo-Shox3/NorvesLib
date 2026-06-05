#include "Animal.h"
#include "Object/ObjectHeap.h"
#include "Object/ObjectResolver.h"
#include "Object/World.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

int main()
{
    std::cout << "ObjectHeapTest start\n";

    ObjectHeap heap;

    ObjectHandle dogHandle = heap.Create<Dog>();
    assert(dogHandle.IsValid());
    Dog *dog = heap.Resolve<Dog>(dogHandle);
    assert(dog != nullptr);
    assert(dog->HasFlag(OF_Initialized));
    assert(dog->HasFlag(OF_HeapOwned));
    assert(heap.GetHandle(dog) == dogHandle);
    assert(heap.GetLiveObjectCount() == 1);

    dog->Destroy();
    assert(dog->HasFlag(OF_PendingDestroy));
    assert(heap.Resolve<Dog>(dogHandle) == dog);

    dog->SetFlag(OF_PendingDestroy, false);
    assert(heap.EnqueueDestroy(dogHandle));
    assert(heap.EnqueueDestroy(dogHandle));
    assert(heap.ProcessDestroyQueue() == 1);
    assert(heap.Resolve<Dog>(dogHandle) == nullptr);
    assert(heap.GetLiveObjectCount() == 0);

    ObjectHandle reusedHandle = heap.Create<Dog>();
    assert(reusedHandle.IsValid());
    assert(reusedHandle.Id == dogHandle.Id);
    assert(reusedHandle.Generation != dogHandle.Generation);
    assert(heap.Resolve<Dog>(dogHandle) == nullptr);
    assert(heap.Resolve<Dog>(reusedHandle) != nullptr);

    ObjectHandle classHandle = heap.Create(Dog::StaticClass());
    assert(classHandle.IsValid());
    assert(heap.Resolve<Dog>(classHandle) != nullptr);

    ObjectResolveContext context;
    context.Heap = &heap;
    context.bAllowPathFallback = true;
    context.RegisterStableObject(1001, reusedHandle);
    context.RegisterSceneObject(2001, classHandle);
    context.RegisterPathObject("Scene/DogA", reusedHandle);

    StableObjectRef sceneFirst;
    sceneFirst.Id = 1001;
    sceneFirst.SceneId = 2001;
    sceneFirst.Path = "Scene/DogA";

    ObjectResolveResult sceneResult = ObjectResolver::Resolve(sceneFirst, context, Dog::StaticClass());
    assert(sceneResult.IsResolved());
    assert(sceneResult.Handle == classHandle);

    StableObjectRef stableOnly;
    stableOnly.Id = 1001;
    ObjectResolveResult stableResult = ObjectResolver::Resolve(stableOnly, context, Animal::StaticClass());
    assert(stableResult.IsResolved());
    assert(stableResult.Handle == reusedHandle);

    StableObjectRef pathOnly;
    pathOnly.Path = "Scene/DogA";
    ObjectResolveResult pathResult = ObjectResolver::Resolve(pathOnly, context, Dog::StaticClass());
    assert(pathResult.IsResolved());
    assert(pathResult.Handle == reusedHandle);

    ObjectResolveResult mismatch = ObjectResolver::Resolve(stableOnly, context, World::StaticClass());
    assert(mismatch.Status == ObjectResolveStatus::ClassMismatch);

    ObjectHandle anotherDog = heap.Create<Dog>();
    context.RegisterPathObject("Scene/DogA", anotherDog);
    ObjectResolveResult ambiguous = ObjectResolver::Resolve(pathOnly, context, Dog::StaticClass());
    assert(ambiguous.Status == ObjectResolveStatus::Ambiguous);

    assert(heap.EnqueueDestroy(reusedHandle));
    assert(heap.EnqueueDestroy(classHandle));
    assert(heap.EnqueueDestroy(anotherDog));
    assert(heap.ProcessDestroyQueue() == 3);
    assert(heap.GetLiveObjectCount() == 0);

    std::cout << "ObjectHeapTest passed\n";
    return 0;
}
