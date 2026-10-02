#include "Component/TickDispatch.h"
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;

int main()
{
    // この試験は参照同一性と順序のヘルパーだけを検証する。ダミー参照を参照解除しない。
    alignas(64) unsigned char identities[4][64]{};
    auto* ownerA = reinterpret_cast<Entity*>(identities[0]);
    auto* ownerB = reinterpret_cast<Entity*>(identities[1]);
    auto* targetA = reinterpret_cast<Component::Component*>(identities[2]);
    auto* targetB = reinterpret_cast<Component::Component*>(identities[3]);
    TickDispatchEntry entries[] = {
        {ownerB, targetB, ETickGroup::Camera, ETickGroup::Camera, -100, 4},
        {ownerB, targetB, ETickGroup::Default, ETickGroup::Default, 0, 3},
        {ownerA, targetA, ETickGroup::Default, ETickGroup::Default, -3, 1},
        {ownerA, nullptr, ETickGroup::Default, ETickGroup::Default, -3, 0},
        {ownerB, nullptr, ETickGroup::Default, ETickGroup::Default, 0, 2},
        {ownerA, targetA, ETickGroup::Input, ETickGroup::Input, 100, 5}
    };
    std::sort(entries, entries + 6, TickDispatchLess);
    assert(entries[0].Group == ETickGroup::Input);
    assert(entries[1].Owner == ownerA && entries[1].Target == nullptr);
    assert(entries[2].Target == targetA);
    assert(entries[3].Owner == ownerB && entries[3].Target == nullptr);
    assert(entries[4].Target == targetB && entries[4].Group == ETickGroup::Default);
    assert(entries[5].Group == ETickGroup::Camera);
    InvalidateTickTarget(entries, nullptr);
    assert(entries[0].Owner == ownerA);
    InvalidateTickTarget(entries, targetA);
    assert(entries[0].Owner == nullptr && entries[0].Target == nullptr);
    assert(entries[2].Owner == nullptr && entries[2].Target == nullptr);
    assert(entries[1].Owner == ownerA);
    InvalidateTickOwner(entries, ownerB);
    assert(entries[3].Owner == nullptr && entries[4].Owner == nullptr && entries[5].Owner == nullptr);
    InvalidateTickOwner(entries, ownerA);
    for (const auto& entry : entries)
    {
        assert(entry.Owner == nullptr && entry.Target == nullptr);
    }
    InvalidateTickOwner({}, ownerA);
    InvalidateTickTarget({}, targetA);
    std::cout << "tick_order_and_reference_invalidation passed\nTickDispatchTest passed\n";
    return 0;
}
