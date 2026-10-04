#include "Resource/SkeletalDrawRange.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Skeletal;
int main()
{
    SkeletalDrawRange out{77,88};
    assert(ResolveSkeletalDrawRange({},9,0,0,0,0,0,false,true,out) && out.IndexOffset == 0 && out.IndexCount == 9);
    assert(ResolveSkeletalDrawRange({},9,0,0,0,9,0,true,true,out));
    const auto reject = [&out](auto tables, uint64_t total, uint32_t submesh, uint32_t material, uint32_t start,
        uint32_t count, uint32_t base, bool shadow = false, bool casts = true)
    {
        out = {77,88};
        assert(!ResolveSkeletalDrawRange(tables,total,submesh,material,start,count,base,shadow,casts,out));
        assert(out.IndexOffset == 77 && out.IndexCount == 88);
    };
    using Span = NorvesLib::Core::Container::Span<const SkeletalSubMesh>;
    reject(Span{},0,0,0,0,0,0);
    reject(Span{},uint64_t(UINT32_MAX)+1,0,0,0,0,0);
    reject(Span{},8,0,0,0,0,0);
    reject(Span{},9,1,0,0,0,0);
    reject(Span{},9,0,1,0,0,0);
    reject(Span{},9,0,0,3,0,0);
    reject(Span{},9,0,0,0,3,0);
    reject(Span{},9,0,0,0,9,1);
    reject(Span{},9,0,0,0,0,0,true,false);
    SkeletalSubMesh ranges[] = {{0,3,0},{3,3,1},{6,3,2}};
    for (uint32_t index = 0; index < 3; ++index)
    {
        assert(ResolveSkeletalDrawRange(ranges,9,index,index,index*3,3,0,false,true,out));
        assert(out.IndexOffset == index*3 && out.IndexCount == 3);
    }
    reject(Span{ranges},9,3,0,0,3,0);
    reject(Span{ranges},9,1,0,3,3,0);
    reject(Span{ranges},9,1,1,0,3,0);
    reject(Span{ranges},9,1,1,3,0,0);
    reject(Span{ranges},9,1,1,3,6,0);
    ranges[1].bNoShadow = true;
    reject(Span{ranges},9,1,1,3,3,0,true,true);
    assert(ResolveSkeletalDrawRange(ranges,9,1,1,3,3,0,false,true,out));
    ranges[2].IndexStart = UINT32_MAX;
    reject(Span{ranges},9,2,2,UINT32_MAX,3,0);
    SkeletalSubMesh nine[9]{};
    reject(Span{nine},27,0,0,0,3,0);
    reject(Span{nullptr,1},9,0,0,0,3,0);
    assert(ResolveSkeletalDrawRange({},UINT32_MAX,0,0,0,0,0,false,true,out) && out.IndexCount == UINT32_MAX);
    std::cout << "SkeletalDrawRangeTest PASS: legacy_full_explicit_exact_ranges_slots_shadow_bounds_atomic\n";
    return 0;
}
