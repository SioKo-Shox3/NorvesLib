#include "Animal.h"
#include "Object/ObjectHeap.h"
#include "Object/ObjectReference.h"
#include "Object/PropertyBag.h"
#include "Object/ResourceRef.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

int main()
{
    std::cout << "PropertyBagTest start\n";

    ObjectHeap heap;
    ObjectHandle dogHandle = heap.Create<Dog>();
    Dog *dog = heap.Resolve<Dog>(dogHandle);
    assert(dog != nullptr);

    ObjectRef<Dog> strongRef(&heap, dogHandle);
    WeakObjectRef<Dog> weakRef(&heap, dogHandle);
    assert(strongRef.IsValid());
    assert(weakRef.IsValid());

    ReferenceCollector collector;
    strongRef.AddReferencedObjects(collector);
    assert(collector.Contains(dogHandle));

    collector.Clear();
    assert(!collector.Contains(dogHandle));

    const StablePropertyId ageProperty = MakeStableSchemaId("NorvesLib", "Property", Container::StringView("Dog"), Container::StringView("Age"));
    const StablePropertyId objectRefProperty = MakeStableSchemaId("NorvesLib", "Property", Container::StringView("Dog"), Container::StringView("Friend"));

    PropertyBag bag;
    bag.Set<int>(ageProperty, 7);
    assert(bag.Has(ageProperty));
    assert(bag.Get<int>(ageProperty) != nullptr);
    assert(*bag.Get<int>(ageProperty) == 7);

    Container::String serializedAge;
    assert(bag.SerializeValue(ageProperty, serializedAge));
    assert(serializedAge == "7");

    bag.SetValue(objectRefProperty, PropertyValue::Create<ObjectRef<Dog>>(strongRef));

    collector.Clear();
    bag.AddReferencedObjects(collector);
    assert(collector.Contains(dogHandle));

    PropertyBag copiedBag = bag;
    assert(copiedBag.Get<int>(ageProperty) != nullptr);
    assert(*copiedBag.Get<int>(ageProperty) == 7);

    const PropertyValue *copiedRefValue = copiedBag.Find(objectRefProperty);
    assert(copiedRefValue != nullptr);
    const ObjectRef<Dog> *copiedRef = copiedRefValue->Get<ObjectRef<Dog>>();
    assert(copiedRef != nullptr);
    assert(copiedRef->Resolve() == dog);

    ResourceRegistry registry;
    assert(registry.Initialize());

    auto resource = registry.CreateTransient<Resource>("StrongResource");
    assert(resource != nullptr);
    ResourceHandle<Resource> resourceHandle = registry.GetHandle<Resource>(resource->GetResourceId());
    assert(resourceHandle.IsValid());

    ResourceRef<Resource> resourceRef(registry, resourceHandle);
    assert(resourceRef.IsValid());
    resource.reset();

    assert(registry.CollectGarbage() == 0);
    assert(registry.Resolve(resourceHandle) != nullptr);

    resourceRef.Reset();
    assert(registry.CollectGarbage() == 1);
    assert(registry.Resolve(resourceHandle) == nullptr);
    registry.Shutdown();

    assert(heap.EnqueueDestroy(dogHandle));
    assert(heap.ProcessDestroyQueue() == 1);

    std::cout << "PropertyBagTest passed\n";
    return 0;
}
