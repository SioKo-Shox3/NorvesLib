#include "Animal.h"
#include "Object/ObjectHeap.h"
#include "Object/ObjectUtility.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

int main()
{
    std::cout << "LegacyCleanupTest start\n";

    Dog source;
    source.Initialize();
    *static_cast<int *>(source.GetPropertyValue(Identity("Age"))) = 9;

    Dog destination;
    destination.Initialize();
    *static_cast<int *>(destination.GetPropertyValue(Identity("Age"))) = 1;

    const int copied = ObjectUtility::CopyEditableProperties(destination, source);
    assert(copied > 0);
    assert(*static_cast<int *>(destination.GetPropertyValue(Identity("Age"))) == 9);

    ObjectHeap heap;
    ObjectHandle handle = heap.Create<Dog>();
    Dog *dog = heap.Resolve<Dog>(handle);
    assert(dog != nullptr);
    assert(!dog->Object::IsPendingDestroy());

    dog->Destroy();
    assert(dog->Object::IsPendingDestroy());

    ObjectHeap::GCStats stats = heap.CollectGarbage();
    assert(stats.SweptObjects == 1);
    assert(heap.Resolve<Dog>(handle) == nullptr);

    std::cout << "LegacyCleanupTest passed\n";
    return 0;
}
