#include "Component/BillboardComponent.h"
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
    void TestBillboardReflectionAndProxyContract()
    {
        const IClass *billboardClass = BillboardComponent::StaticClass();
        assert(billboardClass);
        assert(billboardClass->IsChildOf(BoardComponent::StaticClass()));
        assert(billboardClass->GetProperty(Identity("SizeWorld")) != nullptr);

        World world;
        world.Initialize();

        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        entity->SetLocalPosition(3.0f, 4.0f, -5.0f);

        BillboardComponent *billboard = world.CreateComponent<BillboardComponent>(entity);
        assert(billboard);
        assert(CastTo<BillboardComponent>(billboard) == billboard);
        assert(CastTo<BoardComponent>(billboard) == billboard);
        assert(billboard->GetRenderLayer() == RenderLayer::Default);
        assert(billboard->GetBoardSpace() == BoardSpace::WorldSpace);
        assert(billboard->GetSizeWorld() == Math::Vector2(1.0f, 1.0f));

        billboard->SetBoardSpace(BoardSpace::ScreenSpace);
        billboard->SetSizePx(Math::Vector2(128.0f, 64.0f));
        billboard->SetSizeWorld(Math::Vector2(2.0f, 3.0f));
        billboard->SetPivot(Math::Vector2(0.5f, 0.5f));

        world.UpdateWorldTransforms();
        billboard->RefreshRenderTransformCache();

        BoardProxy proxy;
        assert(billboard->BuildBoardProxy(proxy));
        assert(proxy.ObjectId == entity->GetObjectId());
        assert(proxy.ComponentId == billboard->GetComponentId());
        assert(proxy.Space == BoardSpace::WorldSpace);
        assert(proxy.LayerMask == RenderLayer::Default);
        assert(proxy.SizeWorld == Math::Vector2(2.0f, 3.0f));
        assert(proxy.SizePx == Math::Vector2(128.0f, 64.0f));
        assert(proxy.WorldBounds.CenterX == 3.0f);
        assert(proxy.WorldBounds.CenterY == 4.0f);
        assert(proxy.WorldBounds.CenterZ == -5.0f);
        const float expectedRadius = std::sqrt(1.0f * 1.0f + 1.5f * 1.5f);
        assert(std::fabs(proxy.WorldBounds.Radius - expectedRadius) < 0.0001f);

        billboard->SetVisible(false);
        assert(!billboard->BuildBoardProxy(proxy));

        world.Finalize();
        std::cout << "TestBillboardReflectionAndProxyContract passed\n";
    }
}

int main()
{
    std::cout << "BillboardComponentTest start\n";
    TestBillboardReflectionAndProxyContract();
    std::cout << "BillboardComponentTest passed\n";
    return 0;
}
