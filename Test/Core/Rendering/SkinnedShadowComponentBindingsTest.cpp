// 実描画でも使う共有表の容量・失敗・face分離を純C++で反証する。
#include "Rendering/SkinnedShadowComponentBindings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Core::Rendering;
int main()
{
    int palettes[17]{};
    int vertex = 0;
    uint32_t allocations = 0;
    for (uint32_t face = 0; face < 6; ++face)
    {
        SkinnedShadowComponentBindings<uint32_t> sets;
        uint32_t draws = 0;
        for (uint32_t part = 0; part < 8; ++part)
        {
            for (uint32_t component = 0; component < 17; ++component)
            {
                const SkinnedShadowBindingKey key{component+1,7,42,3,&palettes[component],&vertex};
                uint32_t binding = 999;
                const bool accepted = sets.TryGet(key,[&]() { return ++allocations; },binding);
                assert(accepted == (component < 16));
                if (accepted)
                {
                    ++draws;
                    assert(binding == face*16+component+1);
                }
                else
                {
                    assert(binding == 0);
                }
            }
        }
        assert(draws == 128 && allocations == (face+1)*16 && sets.Count() == 16);
        auto conflict = SkinnedShadowBindingKey{1,8,42,3,&palettes[0],&vertex};
        uint32_t out = 0;
        assert(!sets.TryGet(conflict,[&]() { return ++allocations; },out));
        conflict.Epoch=7; conflict.Generation=4;
        assert(!sets.TryGet(conflict,[&]() { return ++allocations; },out));
        conflict.Generation=3; conflict.Palette=&palettes[1];
        assert(!sets.TryGet(conflict,[&]() { return ++allocations; },out));
        assert(allocations == (face+1)*16);
    }
    SkinnedShadowComponentBindings<uint32_t> failed;
    uint32_t failures = 0, out = 99;
    const SkinnedShadowBindingKey key{1,1,42,1,&palettes[0],&vertex};
    for (uint32_t part=0;part<8;++part)
    {
        assert(!failed.TryGet(key,[&]() { ++failures; return 0u; },out) && out==0);
    }
    assert(failures==1 && failed.Count()==1);
    auto anonymous = key; anonymous.ComponentId=0; anonymous.Epoch=0;
    assert(failed.TryGet(anonymous,[]() { return 2u; },out) && out==2);
    anonymous.Palette=&palettes[1];
    assert(failed.TryGet(anonymous,[]() { return 3u; },out) && out==3);
    anonymous.Vertex=&palettes[2];
    assert(!failed.TryGet(anonymous,[]() { return 4u; },out));
    auto invalid = key; invalid.Palette=nullptr;
    assert(!failed.TryGet(invalid,[]() { assert(false); return 1u; },out));
    invalid=key; invalid.Epoch=0;
    assert(!failed.TryGet(invalid,[]() { assert(false); return 1u; },out));
    assert(failed.Count()==3);
    std::cout << "SkinnedShadowComponentBindingsTest PASS: 16_components_8_parts_6_faces_failure_identity_legacy\n";
    return 0;
}
