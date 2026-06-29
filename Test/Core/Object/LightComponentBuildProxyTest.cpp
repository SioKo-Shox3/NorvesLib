#include "Component/DirectionalLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Math/MathTypes.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    constexpr float Epsilon = 0.0001f;

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    float DegreesToCos(float degrees)
    {
        return cosf(degrees * NorvesLib::Math::Constants::PI / 180.0f);
    }

    void TestDirectionalLightBuildProxy()
    {
        World world;
        world.Initialize();

        Entity* entity = world.SpawnObject<Entity>();
        assert(entity != nullptr);

        DirectionalLightComponent* light = world.CreateComponent<DirectionalLightComponent>(entity);
        assert(light != nullptr);
        light->SetLightDirection(0.25f, -0.5f, 0.75f);

        LightProxy proxy;
        assert(light->BuildLightProxy(proxy));
        assert(proxy.Type == LightType::Directional);
        assert(Near(proxy.DirectionX, 0.25f));
        assert(Near(proxy.DirectionY, -0.5f));
        assert(Near(proxy.DirectionZ, 0.75f));

        world.Finalize();
    }

    void TestSpotLightBuildProxy()
    {
        World world;
        world.Initialize();

        Entity* entity = world.SpawnObject<Entity>();
        assert(entity != nullptr);
        entity->SetPosition(3.0f, 4.0f, 5.0f);

        SpotLightComponent* light = world.CreateComponent<SpotLightComponent>(entity);
        assert(light != nullptr);
        light->SetRange(42.0f);
        light->SetInnerConeAngle(30.0f);
        light->SetOuterConeAngle(20.0f);
        assert(light->GetOuterConeAngle() > light->GetInnerConeAngle());

        light->SetInnerConeAngle(20.0f);
        light->SetOuterConeAngle(40.0f);

        LightProxy proxy;
        assert(light->BuildLightProxy(proxy));
        assert(proxy.Type == LightType::Spot);
        assert(Near(proxy.PositionX, 3.0f));
        assert(Near(proxy.PositionY, 4.0f));
        assert(Near(proxy.PositionZ, 5.0f));
        assert(Near(proxy.Range, 42.0f));
        assert(Near(proxy.InnerConeAngle, DegreesToCos(20.0f)));
        assert(Near(proxy.OuterConeAngle, DegreesToCos(40.0f)));
        assert(proxy.InnerConeAngle > 0.0f);
        assert(proxy.InnerConeAngle <= 1.0f);
        assert(proxy.OuterConeAngle < proxy.InnerConeAngle);

        world.Finalize();
    }
}

int main()
{
    std::cout << "LightComponentBuildProxyTest start\n";

    TestDirectionalLightBuildProxy();
    TestSpotLightBuildProxy();

    std::cout << "LightComponentBuildProxyTest passed\n";
    return 0;
}

