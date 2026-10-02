#include "Component/TickGroup.h"
// 検証をReleaseのNDEBUGでも省略しない。
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Component;

static_assert(TickGroupCount == 8);
static_assert(TickGroupBit(ETickGroup::Input) == 1);
static_assert(TickGroupBit(ETickGroup::PreRender) == 128);
static_assert(TickGroupBit(ETickGroup::Count) == 0);
static_assert(!IsPostPhysicsTickGroup(ETickGroup::Count));
static_assert(TickGroupConfiguration{}.GetGroup() == ETickGroup::Default);

int main()
{
    TickGroupConfiguration configuration;
    assert(configuration.GetGroup() == ETickGroup::Default && configuration.GetPriority() == 0);
    assert(configuration.GetMask() == TickGroupBit(ETickGroup::Default));
    const char* names[8] = {"Input", "Movement", "Default", "Animation", "PoseFinalize", "PostPhysics", "Camera", "PreRender"};
    for (uint8_t index = 0; index < TickGroupCount; ++index)
    {
        const auto group = static_cast<ETickGroup>(index);
        assert(IsValidTickGroup(group));
        assert(std::strcmp(GetTickGroupName(group), names[index]) == 0);
        assert(IsPostPhysicsTickGroup(group) == (index >= 5));
        assert(configuration.SetGroup(group));
        assert(configuration.GetMask() == TickGroupBit(group));
        for (uint16_t mask = 0; mask <= AllTickGroups; ++mask)
        {
            const auto previous = configuration.GetMask();
            const bool expected = (mask & TickGroupBit(group)) != 0;
            assert(configuration.SetMask(mask) == expected);
            assert(configuration.GetMask() == (expected ? mask : previous));
        }
    }
    configuration.SetPriority(std::numeric_limits<int16_t>::min());
    assert(configuration.GetPriority() == std::numeric_limits<int16_t>::min());
    configuration.SetPriority(std::numeric_limits<int16_t>::max());
    assert(configuration.GetPriority() == std::numeric_limits<int16_t>::max());
    assert(configuration.SetMask(AllTickGroups));
    assert(configuration.SetGroup(ETickGroup::Animation));
    assert(configuration.GetMask() == TickGroupBit(ETickGroup::Animation));
    assert(configuration.SetMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize)));
    assert(configuration.ParticipatesIn(ETickGroup::Animation));
    assert(configuration.ParticipatesIn(ETickGroup::PoseFinalize));
    assert(!configuration.ParticipatesIn(ETickGroup::Camera));
    const auto priorMask = configuration.GetMask();
    for (uint16_t invalid = TickGroupCount; invalid < 256; ++invalid)
    {
        const auto group = static_cast<ETickGroup>(invalid);
        assert(!IsValidTickGroup(group) && TickGroupBit(group) == 0 && !IsPostPhysicsTickGroup(group));
        assert(!configuration.SetGroup(group));
        assert(configuration.GetGroup() == ETickGroup::Animation && configuration.GetMask() == priorMask);
        assert(!configuration.ParticipatesIn(group));
        assert(std::strcmp(GetTickGroupName(group), "Invalid") == 0);
    }
    assert(!configuration.SetMask(0xffffu) && configuration.GetMask() == priorMask);
    assert(!configuration.SetMask(TickGroupBit(ETickGroup::Camera)) && configuration.GetMask() == priorMask);
    std::cout << "eight_groups_masks_priority_invalid passed\nTickGroupConfigurationTest passed\n";
    return 0;
}
