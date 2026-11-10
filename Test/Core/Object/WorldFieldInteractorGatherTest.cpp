// ワールドフィールドの押し形を集める際の位置、選択、寿命を検証する。
#include "Component/WorldFieldInteractorComponent.h"
#include "Engine/Engine.h"
#include "Object/World.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    bool Near(float a, float b)
    {
        return std::abs(a - b) < 0.0001f;
    }

    void TestPositionHistory()
    {
        WorldFieldInteractorGatherer gatherer;
        World world;
        world.Initialize();
        Entity* parent = world.SpawnEntity<Entity>();
        Entity* owner = world.SpawnEntity<Entity>(parent);
        assert(parent && owner);
        parent->SetLocalPosition(10.0f, 0.0f, 0.0f);
        owner->SetLocalPosition(2.0f, 0.0f, 0.0f);
        owner->SetLocalScale(2.0f, 2.0f, 2.0f);
        auto* component = world.CreateComponent<WorldFieldInteractorComponent>(owner);
        assert(component);
        WorldFieldInteractorShape shape;
        shape.Kind = WorldFieldShapeKind::Capsule;
        shape.P0 = Math::Vector3(1.0f, 0.0f, 0.0f);
        shape.P1 = Math::Vector3(1.0f, 2.0f, 0.0f);
        assert(component->AddShape(shape));
        gatherer.Register(*component);
        Container::VariableArray<WorldFieldInteractor> out;
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1);
        assert(Near(out[0].P0[0], 14.0f) && Near(out[0].PrevP0[0], 14.0f));
        assert(Near(out[0].P1[1], 4.0f) && Near(out[0].PrevP1[1], 4.0f));
        assert(Near(out[0].Radius, 1.0f) && Near(out[0].PrevRadius, 1.0f));

        parent->SetLocalPosition(20.0f, 0.0f, 0.0f);
        shape.Radius = 1.0f;
        assert(component->SetShape(0, shape));
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1);
        assert(Near(out[0].P0[0], 24.0f) && Near(out[0].PrevP0[0], 14.0f));
        assert(Near(out[0].P1[0], 24.0f) && Near(out[0].PrevP1[0], 14.0f));
        assert(Near(out[0].Radius, 2.0f) && Near(out[0].PrevRadius, 1.0f));

        // 反射が配列を保存・復元できることも、同じ形の契約として確認する。
        const auto* property = component->GetClass()->GetProperty(Identity("Shapes"));
        PropertyValue value;
        Container::String serialized;
        assert(property && property->GetPropertyValue(component, value) && value.Serialize(serialized));
        const auto* type = TypeRegistry::Get().Find(value.GetType());
        WorldFieldInteractorShapes restored;
        assert(type && type->Ops.Deserialize(serialized, &restored));
        assert(restored.Size() == 1 && restored.Get(0)->Kind == WorldFieldShapeKind::Capsule);
        assert(Near(restored.Get(0)->P1.y, 2.0f) && Near(restored.Get(0)->Radius, 1.0f));

        assert(component->RemoveShape(0));
        shape.P0 = Math::Vector3(5.0f, 0.0f, 0.0f);
        assert(component->AddShape(shape));
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1 && Near(out[0].P0[0], 32.0f) && Near(out[0].PrevP0[0], 32.0f));
        world.Finalize();
    }

    struct SocketState
    {
        Math::Vector3 Position;
        bool bAvailable = true;
        uint32_t Calls = 0;
    };

    bool ResolveTestSocket(const Entity&, Identity name, Math::Vector3& out, void* user)
    {
        auto& state = *static_cast<SocketState*>(user);
        assert(name == Identity("body"));
        ++state.Calls;
        out = state.Position;
        return state.bAvailable;
    }

    void TestSocketAndFallback()
    {
        WorldFieldInteractorGatherer gatherer;
        World world;
        world.Initialize();
        Entity* owner = world.SpawnEntity<Entity>();
        assert(owner);
        owner->SetLocalPosition(3.0f, 4.0f, 5.0f);
        auto* component = world.CreateComponent<WorldFieldInteractorComponent>(owner);
        assert(component);
        WorldFieldInteractorShape shape;
        shape.Kind = WorldFieldShapeKind::Capsule;
        shape.Socket = Identity("body");
        shape.P1 = Math::Vector3(0.0f, 2.0f, 0.0f);
        shape.Strength = 0.4f;
        shape.ChannelMask = WorldFieldChannelBend;
        assert(component->AddShape(shape));
        gatherer.Register(*component);
        SocketState state{Math::Vector3(10.0f, 11.0f, 12.0f)};
        gatherer.SetSocketResolver(&ResolveTestSocket, &state);
        Container::VariableArray<WorldFieldInteractor> out;
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1 && state.Calls == 1);
        assert(Near(out[0].P0[0], 10.0f) && Near(out[0].P1[1], 13.0f));
        assert(Near(out[0].PrevP0[0], 10.0f) && Near(out[0].Strength, 0.4f));
        assert(out[0].ChannelMask == WorldFieldChannelBend);
        state.Position.x = 20.0f;
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(state.Calls == 2 && Near(out[0].P0[0], 20.0f) && Near(out[0].PrevP0[0], 10.0f));
        state.bAvailable = false;
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(state.Calls == 3 && Near(out[0].P0[0], 3.0f) && Near(out[0].P0[1], 4.0f));
        assert(Near(out[0].P1[1], 6.0f) && Near(out[0].PrevP0[0], 20.0f));
        gatherer.SetSocketResolver(nullptr);
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1 && Near(out[0].P0[0], 3.0f));
        world.Finalize();
    }

    void TestNearestAndDroppedHistory()
    {
        WorldFieldInteractorGatherer gatherer;
        World world;
        world.Initialize();
        Entity* owner = world.SpawnEntity<Entity>();
        auto* component = world.CreateComponent<WorldFieldInteractorComponent>(owner);
        assert(component);
        WorldFieldInteractorShape shape;
        for (uint32_t i = 0; i < 33; ++i)
        {
            shape.P0.x = static_cast<float>(i);
            assert(component->AddShape(shape));
        }
        gatherer.Register(*component);
        Container::VariableArray<WorldFieldInteractor> out;
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == WorldFieldMaxInteractors);
        for (size_t i = 0; i < out.size(); ++i)
        {
            assert(Near(out[i].P0[0], static_cast<float>(i)));
        }
        shape.P0.x = 100.0f;
        assert(component->SetShape(32, shape));
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == WorldFieldMaxInteractors && Near(out.back().P0[0], 31.0f));
        shape.P0.x = 101.0f;
        assert(component->SetShape(32, shape));
        gatherer.Gather(Math::Vector3(101.0f, 0.0f, 0.0f), out);
        assert(out.size() == WorldFieldMaxInteractors);
        assert(Near(out[0].P0[0], 101.0f) && Near(out[0].PrevP0[0], 100.0f));
        world.Finalize();
    }

    void TestRegistrationAndRemoval()
    {
        Engine::Engine engine;
        assert(ClassRegistry::Get().FindClass(Identity("WorldFieldInteractorComponent")) != nullptr);
        Engine::Engine* previousEngine = Engine::GEngine;
        Engine::GEngine = &engine;
        World& world = engine.GetWorld();
        world.Initialize();
        Entity* owner = world.SpawnEntity<Entity>();
        auto* component = world.CreateComponent<WorldFieldInteractorComponent>(owner);
        assert(component && component->AddShape(WorldFieldInteractorShape{}));
        Container::VariableArray<WorldFieldInteractor> out;
        auto& gatherer = engine.GetWorldFieldInteractorGatherer();
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1);
        owner->RemoveComponent(component);
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.empty());
        component = world.CreateComponent<WorldFieldInteractorComponent>(owner);
        assert(component && component->AddShape(WorldFieldInteractorShape{}));
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.size() == 1);
        assert(world.RemoveEntity(owner));
        gatherer.Gather(Math::Vector3::Zero, out);
        assert(out.empty());
        world.Finalize();
        Engine::GEngine = previousEngine;
    }
}

int main()
{
    std::cout << "WorldFieldInteractorGatherTest start\n";
    TestRegistrationAndRemoval();
    TestPositionHistory();
    TestSocketAndFallback();
    TestNearestAndDroppedHistory();
    std::cout << "WorldFieldInteractorGatherTest passed\n";
    return 0;
}
