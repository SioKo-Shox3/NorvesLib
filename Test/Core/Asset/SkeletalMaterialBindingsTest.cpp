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
    using namespace NorvesLib::Core;
    const uint8_t body[] = {'B','o','d','y'}, eyes[] = {'E','y','e','s'};
    MaterialIdentityView slots[] = {{1,{eyes,4}},{0,{body,4}}};
    assert(FindSkeletalMaterialSlot(slots,{eyes,4}) == 1);
    assert(FindSkeletalMaterialSlot(slots,{body,4}) == 0);
    assert(FindSkeletalMaterialSlot({},{body,4}) == -1);
    MaterialIdentityView tooMany[9]{};
    assert(FindSkeletalMaterialSlot(tooMany,{body,4}) == -1);
    slots[0].Name = {body,4};
    assert(FindSkeletalMaterialSlot(slots,{body,4}) == -1);
    slots[0].Name = {};
    assert(FindSkeletalMaterialSlot(slots,{}) == 1);
    const uint8_t nul[] = {'B','o','d','y',0,'X'};
    assert(FindSkeletalMaterialSlot(slots,{nul,6}) == -1);
    const uint8_t bad[] = {0xc0,0x80};
    assert(FindSkeletalMaterialSlot(slots,{bad,2}) == -1);
    const uint8_t defaultName[] = {'D','e','f','a','u','l','t'};
    MaterialIdentityView legacy[] = {{0,{defaultName,7}}};
    assert(FindSkeletalMaterialSlot(legacy,{defaultName,7}) == 0);
    std::cout << "SkeletalMaterialBindingsTest PASS: scope_generation_fallback_clear_failure_unique_name\n";
    return 0;
}
