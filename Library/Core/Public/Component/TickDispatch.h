#pragma once

#include "Component/TickGroup.h"
#include "Container/Span.h"
#include <cstddef>

namespace NorvesLib::Core
{
    class Entity;
    namespace Component
    {
        class Component;
    }

    // フレーム内だけ保持する非所有参照。実破棄前にWorldが対応entryを無効化する。
    struct TickDispatchEntry
    {
        Entity* Owner = nullptr;
        Component::Component* Target = nullptr;
        Component::ETickGroup Group = Component::ETickGroup::Default;
        Component::ETickGroup PrimaryGroup = Component::ETickGroup::Default;
        int16_t Priority = 0;
        size_t Ordinal = 0;
    };

    inline bool TickDispatchLess(const TickDispatchEntry& a, const TickDispatchEntry& b)
    {
        if (a.Group != b.Group)
        {
            return a.Group < b.Group;
        }
        if (a.Priority != b.Priority)
        {
            return a.Priority < b.Priority;
        }
        return a.Ordinal < b.Ordinal;
    }

    inline void InvalidateTickOwner(Container::Span<TickDispatchEntry> entries, const Entity* owner)
    {
        if (!owner)
        {
            return;
        }
        for (auto& entry : entries)
        {
            if (entry.Owner == owner)
            {
                entry.Owner = nullptr;
                entry.Target = nullptr;
            }
        }
    }

    inline void InvalidateTickTarget(Container::Span<TickDispatchEntry> entries, const Component::Component* target)
    {
        if (!target)
        {
            return;
        }
        for (auto& entry : entries)
        {
            if (entry.Target == target)
            {
                entry.Owner = nullptr;
                entry.Target = nullptr;
            }
        }
    }
} // namespace NorvesLib::Core
