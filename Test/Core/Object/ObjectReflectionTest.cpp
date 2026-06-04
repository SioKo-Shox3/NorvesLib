#include "Animal.h"
#include "Component/Component.h"
#include "Object/ObjectUtility.h"
#include "Object/World.h"
#include "Object/WorldObject.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test;

int main()
{
    std::cout << "ObjectReflectionTest start\n";

    const IClass *animalClass = Animal::StaticClass();
    const IClass *dogClass = Dog::StaticClass();
    assert(animalClass != nullptr);
    assert(dogClass != nullptr);
    assert(dogClass->IsChildOf(animalClass));

    Dog dog;
    dog.Initialize();

    auto *age = static_cast<int *>(dog.GetPropertyValue(Identity("Age")));
    assert(age != nullptr);
    assert(*age == 3);
    *age = 5;
    assert(*static_cast<const int *>(static_cast<const Dog &>(dog).GetPropertyValue(Identity("Age"))) == 5);

    auto *isGoodBoy = static_cast<bool *>(dog.GetPropertyValue(Identity("IsGoodBoy")));
    assert(isGoodBoy != nullptr);
    assert(*isGoodBoy);

    const ClassFunction *bmiFunction = dogClass->GetFunction(Identity("GetBMI"));
    assert(bmiFunction != nullptr);
    float bmi = 0.0f;
    assert(bmiFunction->Invoke(&dog, nullptr, &bmi));
    assert(std::fabs(bmi - 7.5f) < 0.001f);

    NorvesLib::Core::IUnknown *cloneUnknown = dog.Clone();
    Dog *cloneDog = ObjectUtility::CastTo<Dog>(cloneUnknown);
    assert(cloneDog != nullptr);
    auto *cloneAge = static_cast<int *>(cloneDog->GetPropertyValue(Identity("Age")));
    assert(cloneAge != nullptr);
    assert(*cloneAge == 5);
    cloneUnknown->Release();

    World world;
    world.Initialize();
    WorldObject *object = world.SpawnObject<WorldObject>();
    assert(object != nullptr);
    assert(object->GetWorld() == &world);
    assert(object->GetRefCount() == 1);
    assert(world.GetObjectCount() == 1);

    auto *component = world.CreateComponent<NorvesLib::Core::Component::Component>(object);
    assert(component != nullptr);
    assert(component->GetOwner() == object);
    assert(component->GetRefCount() == 1);

    object->MarkForDestroy();
    world.Tick(0.016f);
    assert(world.GetObjectCount() == 0);
    world.Finalize();

    std::cout << "ObjectReflectionTest passed\n";
    return 0;
}
