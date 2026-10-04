#include "Resource/SkeletalSubmeshLayout.h"
#include "Resource/SkeletalSubmeshBounds.h"
#include <cmath>
#include "Resource/SkeletalLimits.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Skeletal;
namespace
{
    void AssertFailure(const SkeletalSubmeshLayoutResult& result, SkeletalSubmeshLayoutStatus expected)
    {
        assert(result.Status == expected && !result.Succeeded());
        assert(result.SubmeshCount == 0 && result.MaterialSlotCount == 0 && !result.bUsesImplicitSingleSubmesh);
        assert(result.ImplicitSubmesh.IndexStart == 0 && result.ImplicitSubmesh.IndexCount == 0 && result.ImplicitSubmesh.MaterialSlot == 0);
    }
}
int main()
{
    static_assert(std::is_standard_layout_v<SkeletalSubMesh> && std::is_trivially_copyable_v<SkeletalSubMesh>);
    using Status = SkeletalSubmeshLayoutStatus;
    const auto legacy = ResolveSkeletalSubmeshLayout({}, 6, 0);
    assert(legacy.Succeeded() && legacy.SubmeshCount == 1 && legacy.MaterialSlotCount == 1 && legacy.bUsesImplicitSingleSubmesh);
    assert(legacy.ImplicitSubmesh.IndexStart == 0 && legacy.ImplicitSubmesh.IndexCount == 6 && legacy.ImplicitSubmesh.MaterialSlot == 0);
    for (uint64_t count : {uint64_t{0}, uint64_t{1}, uint64_t{2}, uint64_t{4}, uint64_t{UINT32_MAX} + 1, uint64_t{UINT64_MAX}})
    {
        AssertFailure(ResolveSkeletalSubmeshLayout({}, count, 0), Status::InvalidTotalIndexCount);
    }
    const auto legacyMaximum = ResolveSkeletalSubmeshLayout({}, UINT32_MAX, 0);
    assert(legacyMaximum.Succeeded() && legacyMaximum.ImplicitSubmesh.IndexCount == UINT32_MAX);
    SkeletalSubMesh single[] = {{0, 6, 0}};
    auto result = ResolveSkeletalSubmeshLayout(single, 6, 1);
    assert(result.Succeeded() && result.SubmeshCount == 1 && result.MaterialSlotCount == 1 && !result.bUsesImplicitSingleSubmesh);
    assert(result.ImplicitSubmesh.IndexCount == 0);
    SkeletalSubMesh pair[] = {{0, 3, 0}, {3, 6, 1}};
    const auto original = pair[1];
    result = ResolveSkeletalSubmeshLayout(pair, 9, 2);
    assert(result.Succeeded() && result.SubmeshCount == 2 && result.MaterialSlotCount == 2);
    assert(pair[1].IndexStart == original.IndexStart && pair[1].IndexCount == original.IndexCount && pair[1].MaterialSlot == original.MaterialSlot);
    SkeletalSubMesh eight[8];
    for (uint32_t index = 0; index < 8; ++index)
    {
        eight[index] = {index * 3, 3, index};
    }
    result = ResolveSkeletalSubmeshLayout(eight, 24, 8);
    assert(result.Succeeded() && result.SubmeshCount == 8 && result.MaterialSlotCount == 8);
    // 複数submeshが同じslotを共有してよい。未使用slotも範囲検証の責務では拒否しない。
    for (auto& submesh : eight)
    {
        submesh.MaterialSlot = 0;
    }
    assert(ResolveSkeletalSubmeshLayout(eight, 24, 1).Succeeded());
    assert(ResolveSkeletalSubmeshLayout(single, 6, 8).Succeeded());
    SkeletalSubMesh nine[9] = {};
    AssertFailure(ResolveSkeletalSubmeshLayout(nine, 27, 1), Status::SubmeshLimitExceeded);
    // count上限を検査してから走査する。巨大な宣言でポインタ加算しない。
    AssertFailure(ResolveSkeletalSubmeshLayout({single, std::numeric_limits<size_t>::max()}, 6, 1), Status::SubmeshLimitExceeded);

    AssertFailure(ResolveSkeletalSubmeshLayout(single, 6, 9), Status::MaterialSlotLimitExceeded);
    AssertFailure(ResolveSkeletalSubmeshLayout(single, 6, UINT64_MAX), Status::MaterialSlotLimitExceeded);
    AssertFailure(ResolveSkeletalSubmeshLayout({}, 6, 1), Status::IncompleteTables);
    AssertFailure(ResolveSkeletalSubmeshLayout(single, 6, 0), Status::IncompleteTables);
    AssertFailure(ResolveSkeletalSubmeshLayout({nullptr, 1}, 6, 1), Status::InvalidInput);
    const SkeletalSubMesh malformed[][2] = {
        {{3,3,0},{6,3,0}}, // 先頭が0でない。
        {{0,3,0},{6,3,0}}, // 隙間。
        {{0,6,0},{3,3,0}}, // 重複。
        {{3,6,0},{0,3,0}}, // 逆順。
        {{0,0,0},{0,9,0}}, // 空範囲。
        {{0,4,0},{4,5,0}}, // 三角形境界でない。
        {{0,3,0},{3,3,0}}, // 末尾不足。
        {{0,3,0},{3,9,0}}, // 末尾超過。
        {{0,3,0},{UINT32_MAX,6,0}} // u32で加算するとwrapする開始値。
    };
    for (const auto& submeshes : malformed)
    {
        const SkeletalSubMesh before[] = {submeshes[0], submeshes[1]};
        AssertFailure(ResolveSkeletalSubmeshLayout(submeshes, 9, 1), Status::InvalidIndexRange);
        assert(std::memcmp(before, submeshes, sizeof(before)) == 0);
    }
    pair[1].MaterialSlot = 2;
    AssertFailure(ResolveSkeletalSubmeshLayout(pair, 9, 2), Status::InvalidMaterialSlot);
    pair[1].MaterialSlot = UINT32_MAX;
    AssertFailure(ResolveSkeletalSubmeshLayout(pair, 9, 8), Status::InvalidMaterialSlot);
    const SkeletalSubMesh maximum[] = {{0, UINT32_MAX - 3, 0}, {UINT32_MAX - 3, 3, 0}};
    assert(ResolveSkeletalSubmeshLayout(maximum, UINT32_MAX, 1).Succeeded());
    const SkeletalSubMesh overflow[] = {{0,3,0},{3,UINT32_MAX,0}};
    AssertFailure(ResolveSkeletalSubmeshLayout(overflow, UINT32_MAX, 1), Status::InvalidIndexRange);
    struct Point { float X, Y, Z; };
    Point positions[] = {{-1,0,0}, {3,2,0}, {0,1,0}};
    uint32_t ids[] = {0,1,2};
    SkeletalSubMesh bounds{7,3,2};
    bounds.bNoShadow = true;
    const auto read = [&positions](uint32_t index)
    {
        return positions[index];
    };
    assert(ComputeSkeletalSubmeshBounds(ids,3,read,bounds));
    assert(bounds.BoundsCenter[0] == 1 && bounds.BoundsCenter[1] == 1 && bounds.BoundsCenter[2] == 0);
    assert(double(bounds.BoundsRadius) >= std::sqrt(5.0) && bounds.IndexStart == 7 && bounds.MaterialSlot == 2 && bounds.bNoShadow);
    const float retainedRadius = bounds.BoundsRadius;
    ids[2] = 3;
    assert(!ComputeSkeletalSubmeshBounds(ids,3,read,bounds) && bounds.BoundsRadius == retainedRadius);
    ids[2] = 2;
    positions[2].X = std::numeric_limits<float>::quiet_NaN();
    assert(!ComputeSkeletalSubmeshBounds(ids,3,read,bounds) && bounds.BoundsRadius == retainedRadius);
    positions[2] = {0,0,0};
    positions[0] = {-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),0};
    positions[1] = {std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),0};
    assert(!ComputeSkeletalSubmeshBounds(ids,3,read,bounds) && bounds.BoundsRadius == retainedRadius);
    positions[0] = positions[1] = positions[2] = {2,3,4};
    assert(ComputeSkeletalSubmeshBounds(ids,3,read,bounds) && bounds.BoundsRadius == 0 && bounds.BoundsCenter[2] == 4);
    assert(!ComputeSkeletalSubmeshBounds({},3,read,bounds));
    assert(!ComputeSkeletalSubmeshBounds({nullptr,3},3,read,bounds));
    assert(!ComputeSkeletalSubmeshBounds(ids,0,read,bounds));
    std::cout << "SkeletalSubmeshLayoutTest PASS: packed_ranges_slots_legacy_empty_limits_atomic_readonly\n";
    return 0;
}
