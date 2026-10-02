#include "Scene/PhysicsQueryTypes.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <iostream>
#include <type_traits>

using namespace NorvesLib::Core::Scene;

// 移設した既存APIの数値とハンドルレイアウトを固定する。
static_assert(static_cast<int>(EPhysicsSceneQueryResult::Success) == 0);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::NoHit) == 1);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::Unavailable) == 2);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::NotReady) == 3);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::InvalidArgument) == 4);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::WrongThread) == 5);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::AlreadyBound) == 6);
static_assert(static_cast<int>(EPhysicsSceneQueryResult::ProviderMismatch) == 7);
static_assert(sizeof(ColliderHandle) == 8 && sizeof(BodyHandle) == 8);
static_assert(offsetof(ColliderHandle, Generation) == 4 && offsetof(BodyHandle, Generation) == 4);
static_assert(std::is_trivially_copyable_v<PhysicsQueryDesc>);
static_assert(std::is_trivially_copyable_v<PhysicsQueryHit>);
static_assert(!ColliderHandle{}.IsValid() && !BodyHandle{}.IsValid());
static_assert(ColliderHandle{2, 1} < ColliderHandle{2, 2});

int main()
{
    static_assert(std::is_trivially_copyable_v<PhysicsRaycastHit>);
    static_assert(std::is_trivially_copyable_v<PhysicsOverlapHit>);
    assert(PhysicsRaycastHit{}.UserData == 0);
    assert(PhysicsOverlapHit{}.UserData == 0);
    PhysicsRaycastHit rayHit;
    rayHit.UserData = UINT64_MAX;
    assert(PhysicsRaycastHit(rayHit).UserData == UINT64_MAX);
    PhysicsOverlapHit overlapHit;
    overlapHit.UserData = UINT64_MAX;
    assert(PhysicsOverlapHit(overlapHit).UserData == UINT64_MAX);
    PhysicsQueryFilter filter;
    assert(filter.IsValid());
    const ColliderHandle collider{3, 1};
    const BodyHandle body{2, 1};
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        const uint32_t layer = uint32_t{1} << bit;
        assert(filter.Accepts(layer, false, collider, body));
        assert(filter.Accepts(layer, true, collider, body));
        PhysicsQueryFilter one;
        one.LayerMask = layer;
        for (uint32_t other = 0; other < 32; ++other)
        {
            assert(one.Accepts(uint32_t{1} << other, false, collider, body) == (bit == other));
        }
    }
    filter.LayerMask = 0;
    assert(!filter.Accepts(AllPhysicsLayers, false, collider, body));
    filter.LayerMask = AllPhysicsLayers;
    assert(!filter.Accepts(0, false, collider, body));
    std::cout << "layer_masks_32 passed\n";

    filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Exclude;
    assert(filter.Accepts(1, false, collider, body));
    assert(!filter.Accepts(1, true, collider, body));
    filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Only;
    assert(!filter.Accepts(1, false, collider, body));
    assert(filter.Accepts(1, true, collider, body));
    filter.TriggerPolicy = static_cast<EPhysicsQueryTriggerPolicy>(255);
    assert(!filter.IsValid() && !filter.Accepts(1, false, collider, body));
    filter = {};
    std::cout << "trigger_policy passed\n";

    filter.IgnoreColliders[3] = collider;
    assert(!filter.Accepts(1, false, collider, body));
    assert(filter.Accepts(1, false, ColliderHandle{3, 2}, body));
    filter = {};
    filter.IgnoreBodies[3] = body;
    assert(!filter.Accepts(1, true, collider, body));
    assert(filter.Accepts(1, true, collider, BodyHandle{2, 2}));
    assert(filter.Accepts(1, true, ColliderHandle{}, BodyHandle{}));
    filter = {};
    filter.IgnoreBodies[0] = BodyHandle{2, 0};
    filter.IgnoreColliders[0] = ColliderHandle{3, 0};
    assert(filter.Accepts(1, false, ColliderHandle{3, 0}, BodyHandle{2, 0}));
    std::cout << "ignore_generation_and_invalid passed\n";

    for (uint32_t layerA = 0; layerA < 8; ++layerA)
    {
        for (uint32_t maskA = 0; maskA < 8; ++maskA)
        {
            for (uint32_t layerB = 0; layerB < 8; ++layerB)
            {
                for (uint32_t maskB = 0; maskB < 8; ++maskB)
                {
                    const bool expected = (layerA & maskB) && (layerB & maskA);
                    assert(CanPhysicsLayersInteract(layerA, maskA, layerB, maskB) == expected);
                    assert(CanPhysicsLayersInteract(layerB, maskB, layerA, maskA) == expected);
                }
            }
        }
    }
    for (uint32_t firstBit = 0; firstBit < 32; ++firstBit)
    {
        for (uint32_t secondBit = 0; secondBit < 32; ++secondBit)
        {
            const uint32_t a = uint32_t{1} << firstBit, b = uint32_t{1} << secondBit;
            assert(CanPhysicsLayersInteract(a,b,b,a));
            assert(CanPhysicsLayersInteract(b,a,a,b));
            assert(!CanPhysicsLayersInteract(a,0,b,a));
            assert(!CanPhysicsLayersInteract(a,b,b,0));
            assert(!CanPhysicsLayersInteract(0,b,b,a));
            assert(CanPhysicsLayersInteract(a|b,AllPhysicsLayers,b|a,AllPhysicsLayers));
        }
    }
    assert(CanPhysicsLayersInteract(DefaultPhysicsLayer, AllPhysicsLayers, DefaultPhysicsLayer, AllPhysicsLayers));
    PhysicsQueryDesc desc;
    assert(desc.Kind == EPhysicsQueryKind::RaycastClosest && desc.bReportStartOverlap);
    assert(desc.Filter.Accepts(DefaultPhysicsLayer, true, collider, body));
    assert(desc.MaxHits == UINT32_MAX);
    PhysicsQueryBatchResult batch;
    assert(batch.Result == EPhysicsSceneQueryResult::Unavailable && batch.FirstHit == 0 && batch.HitCount == 0);
    PhysicsQueryHit hit;
    assert(!hit.bHasEntity && !hit.bStartPenetrating && hit.UserData == 0 && hit.Distance == 0.0f && hit.Depth == 0.0f);
    std::cout << "pair_truth_table_4096_and_defaults passed\nPhysicsQueryTypesTest passed\n";
    return 0;
}
