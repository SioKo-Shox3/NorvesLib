#include "Component/DirectionalLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Component/PointLightComponent.h"
#include "Math/MathTypes.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

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

    bool Near(float lhs, float rhs, float tolerance)
    {
        return std::fabs(lhs - rhs) <= tolerance;
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
        assert(light->GetOuterConeAngle() == 20.0f);
        LightProxy invalidConeProxy;
        assert(!light->BuildLightProxy(invalidConeProxy));

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

    void TestPhysicalLightContract()
    {
        World world;
        world.Initialize();

        Entity* directionalEntity = world.SpawnObject<Entity>();
        Entity* pointEntity = world.SpawnObject<Entity>();
        Entity* spotEntity = world.SpawnObject<Entity>();
        DirectionalLightComponent* directional = world.CreateComponent<DirectionalLightComponent>(directionalEntity);
        PointLightComponent* point = world.CreateComponent<PointLightComponent>(pointEntity);
        SpotLightComponent* spot = world.CreateComponent<SpotLightComponent>(spotEntity);
        assert(directional != nullptr);
        assert(point != nullptr);
        assert(spot != nullptr);
        assert(directional->GetIntensityUnit() == LightIntensityUnit::Lux);
        assert(point->GetIntensityUnit() == LightIntensityUnit::Candela);
        assert(spot->GetIntensityUnit() == LightIntensityUnit::Candela);

        LightProxy directionalProxy;
        assert(directional->BuildLightProxy(directionalProxy));
        assert(Near(directionalProxy.CanonicalIntensity, 1.0f));
        assert(Near(directionalProxy.ColorR, 1.0f));
        assert(Near(directionalProxy.ColorG, 1.0f));
        assert(Near(directionalProxy.ColorB, 1.0f));

        point->SetIntensity(4.0f * NorvesLib::Math::Constants::PI);
        assert(point->SetIntensityUnit(LightIntensityUnit::Lumen));
        LightProxy pointProxy;
        assert(point->BuildLightProxy(pointProxy));
        assert(Near(pointProxy.CanonicalIntensity, 1.0f, 1.0e-4f));
        assert(Near(pointProxy.Intensity, 1.0f, 1.0e-4f));

        LightProxy sharedIntensityProxy;
        sharedIntensityProxy.CanonicalIntensity = 7.0f;
        assert(Near(sharedIntensityProxy.Intensity, 7.0f));
        sharedIntensityProxy.Intensity = 11.0f;
        assert(Near(sharedIntensityProxy.CanonicalIntensity, 11.0f));

        spot->SetInnerConeAngle(20.0f);
        spot->SetOuterConeAngle(40.0f);
        spot->SetIntensity(1.0f);
        assert(spot->SetIntensityUnit(LightIntensityUnit::Lumen));
        LightProxy spotProxy;
        assert(spot->BuildLightProxy(spotProxy));
        const float omega = 2.0f * NorvesLib::Math::Constants::PI *
                            (1.0f - (std::cos(20.0f * NorvesLib::Math::Constants::PI / 180.0f) +
                                     std::cos(40.0f * NorvesLib::Math::Constants::PI / 180.0f)) / 2.0f);
        assert(Near(spotProxy.CanonicalIntensity, 1.0f / omega, 1.0e-4f));

        point->SetLightColor(1.0f, 0.25f, 0.1f);
        point->SetIntensityUnit(LightIntensityUnit::Candela);
        point->SetIntensity(1.0f);
        float rawR = 0.0f;
        float rawG = 0.0f;
        float rawB = 0.0f;
        point->GetLightColor(rawR, rawG, rawB);
        assert(rawR == 1.0f);
        assert(rawG == 0.25f);
        assert(rawB == 0.1f);
        assert(point->BuildLightProxy(pointProxy));
        assert(Near(pointProxy.ColorR, 1.0f / 0.39862f, 1.0e-4f));
        assert(Near(pointProxy.ColorG, 0.25f / 0.39862f, 1.0e-4f));
        assert(Near(pointProxy.ColorB, 0.1f / 0.39862f, 1.0e-4f));

        directional->SetIntensityUnit(LightIntensityUnit::Candela);
        assert(!directional->BuildLightProxy(directionalProxy));
        point->SetIntensity(-1.0f);
        assert(!point->BuildLightProxy(pointProxy));
        point->SetIntensity(1.0f);
        point->SetLightColor(std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f);
        assert(!point->BuildLightProxy(pointProxy));
        point->SetLightColor(-1.0f, 1.0f, 1.0f);
        assert(!point->BuildLightProxy(pointProxy));
        point->SetLightColor(0.0f, 0.0f, 0.0f);
        assert(!point->BuildLightProxy(pointProxy));
        point->SetLightColor(1.0f, 1.0f, 1.0f);
        point->SetIntensity(std::numeric_limits<float>::infinity());
        assert(!point->BuildLightProxy(pointProxy));

        spot->SetInnerConeAngle(0.0f);
        spot->SetOuterConeAngle(1.0f);
        spot->SetIntensity(std::numeric_limits<float>::max());
        assert(spot->SetIntensityUnit(LightIntensityUnit::Lumen));
        LightProxy overflowProxy;
        overflowProxy.LightId = 9182;
        overflowProxy.CanonicalIntensity = 37.0f;
        assert(!spot->BuildLightProxy(overflowProxy));
        assert(overflowProxy.LightId == 9182);
        assert(Near(overflowProxy.CanonicalIntensity, 37.0f));

        world.Finalize();
    }
}

int main()
{
    std::cout << "LightComponentBuildProxyTest start\n";

    TestDirectionalLightBuildProxy();
    TestSpotLightBuildProxy();
    TestPhysicalLightContract();

    std::cout << "LightComponentBuildProxyTest passed\n";
    return 0;
}

