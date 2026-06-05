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
    assert(ClassRegistry::Get().FindClass(animalClass->GetClassId()) == animalClass);
    assert(ClassRegistry::Get().FindClass(dogClass->GetClassId()) == dogClass);
    assert(animalClass->GetClassId() != dogClass->GetClassId());

    const IClass *worldClass = World::StaticClass();
    const IClass *worldObjectClass = WorldObject::StaticClass();
    const IClass *componentClass = NorvesLib::Core::Component::Component::StaticClass();
    assert(worldClass != nullptr);
    assert(worldObjectClass != nullptr);
    assert(componentClass != nullptr);
    assert(ClassRegistry::Get().FindClass(worldClass->GetClassId()) == worldClass);
    assert(ClassRegistry::Get().FindClass(worldObjectClass->GetClassId()) == worldObjectClass);
    assert(ClassRegistry::Get().FindClass(componentClass->GetClassId()) == componentClass);

    auto registeredClasses = ClassRegistry::Get().GetAllClasses();
    for (size_t i = 0; i < registeredClasses.size(); ++i)
    {
        for (size_t j = i + 1; j < registeredClasses.size(); ++j)
        {
            assert(registeredClasses[i]->GetClassId() != registeredClasses[j]->GetClassId());
        }
    }

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

    Dog copiedDog;
    copiedDog.Initialize();
    assert(ObjectUtility::CopyEditableProperties(copiedDog, dog) > 0);
    auto *copiedAge = static_cast<int *>(copiedDog.GetPropertyValue(Identity("Age")));
    assert(copiedAge != nullptr);
    assert(*copiedAge == 5);

    Dog outerDog;
    outerDog.Initialize();
    Dog *ownedDog = ObjectUtility::CreateTypedObject<Dog>(&outerDog);
    assert(ownedDog != nullptr);
    assert(ownedDog->GetOuter() == &outerDog);
    assert(ownedDog->HasFlag(OF_Initialized));
    assert(ObjectUtility::DestroyObject(ownedDog));
    assert(outerDog.GetInners().empty());

    Dog *standaloneDog = ObjectUtility::CreateTypedObject<Dog>();
    assert(standaloneDog != nullptr);
    assert(standaloneDog->GetOuter() == nullptr);
    assert(standaloneDog->HasFlag(OF_Initialized));
    assert(ObjectUtility::DestroyObject(standaloneDog));

    World world;
    world.Initialize();
    WorldObject *object = world.SpawnObject<WorldObject>();
    assert(object != nullptr);
    assert(object->GetWorld() == &world);
    assert(world.GetObjectCount() == 1);

    auto *component = world.CreateComponent<NorvesLib::Core::Component::Component>(object);
    assert(component != nullptr);
    assert(component->GetOwner() == object);
    object->RemoveComponent(component);
    assert(object->GetComponents().empty());

    component = world.CreateComponent<NorvesLib::Core::Component::Component>(object);
    assert(component != nullptr);

    object->MarkForDestroy();
    world.Tick(0.016f);
    assert(world.GetObjectCount() == 0);
    world.Finalize();

    std::cout << "ObjectReflectionTest passed\n";
    return 0;
}
