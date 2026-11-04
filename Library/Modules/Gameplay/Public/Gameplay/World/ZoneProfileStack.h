#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::World
{
    struct ZoneProfileChange
    {
        bool bChanged = false;
        Core::Container::String OldProfileId;
        Core::Container::String NewProfileId;
        float BlendSeconds = 0.0f;
    };

    class ZoneProfileStack
    {
    public:
        // 最上位の要素が替わっても ProfileId が同じなら変化なしとする。
        // 変化がない場合の BlendSeconds は 0。空の ProfileId は押さない。
        ZoneProfileChange Push(uint64_t ownerKey, const Core::Container::String& profileId,
                               int32_t priority, float blendSeconds);
        ZoneProfileChange Remove(uint64_t ownerKey);
        ZoneProfileChange Clear();
        Core::Container::String GetActiveProfileId() const;

    private:
        struct Entry
        {
            uint64_t OwnerKey = 0;
            Core::Container::String ProfileId;
            int32_t Priority = 0;
            float BlendSeconds = 0.0f;
            uint64_t Sequence = 0;
        };

        const Entry* FindActive() const;
        ZoneProfileChange MakeChange(const Core::Container::String& oldProfileId, float oldBlendSeconds) const;

        Core::Container::VariableArray<Entry> m_Entries;
        uint64_t m_NextSequence = 0;
    };
} // namespace NorvesLib::Modules::Gameplay::World
