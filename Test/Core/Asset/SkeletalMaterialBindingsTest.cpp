#include "Resource/SkeletalMaterialBindings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Skeletal;
int main()
{
    SkeletalMaterialBindings bindings;
    uint64_t out = 999;
    assert(bindings.TryGet(1,1,2,0,10,out) && out == 10);
    assert(bindings.Set(1,1,2,0,20));
    assert(bindings.TryGet(1,1,2,0,10,out) && out == 20);
    assert(bindings.TryGet(1,1,2,1,10,out) && out == 10);
    for (uint32_t count : {0u,9u,UINT32_MAX})
    {
        assert(!bindings.Set(1,2,count,0,30));
        out = 999;
        assert(!bindings.TryGet(1,1,count,0,10,out) && out == 999);
    }
    assert(!bindings.Set(0,1,2,0,30) && !bindings.Set(1,0,2,0,30));
    assert(!bindings.Set(1,2,2,2,30));
    assert(bindings.TryGet(1,1,2,0,10,out) && out == 20); // 失敗Setは旧世代も保持。
    assert(bindings.TryGet(1,2,2,0,10,out) && out == 10); // 別世代はfallback。
    assert(bindings.TryGet(2,1,2,0,10,out) && out == 10); // 別meshはfallback。
    assert(bindings.TryGet(1,1,1,0,10,out) && out == 10); // count変更も別scope。
    assert(bindings.Set(1,2,2,1,30));
    assert(bindings.TryGet(1,2,2,0,10,out) && out == 10);
    assert(bindings.TryGet(1,2,2,1,10,out) && out == 30);
    assert(bindings.Set(1,2,2,1,0));
    assert(bindings.TryGet(1,2,2,1,10,out) && out == 10);
    assert(bindings.Set(2,3,8,7,40));
    assert(bindings.TryGet(2,3,8,7,10,out) && out == 40);
    bindings.Clear();
    assert(bindings.TryGet(2,3,8,7,10,out) && out == 10);
    const auto match = [](uint32_t index)
    {
        return index == 1;
    };
    assert(FindUniqueSkeletalMaterialSlot(2,match) == 1);
    assert(FindUniqueSkeletalMaterialSlot(1,match) == -1);
    assert(FindUniqueSkeletalMaterialSlot(0,match) == -1);
    assert(FindUniqueSkeletalMaterialSlot(9,match) == -1);
    assert(FindUniqueSkeletalMaterialSlot(2,[](uint32_t)
    {
        return true;
    }) == -1);
    std::cout << "SkeletalMaterialBindingsTest PASS: scope_generation_fallback_clear_failure_unique_name\n";
    return 0;
}
