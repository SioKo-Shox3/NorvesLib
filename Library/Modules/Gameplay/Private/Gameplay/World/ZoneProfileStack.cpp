#include "Gameplay/World/ZoneProfileStack.h"

#include <limits>

namespace NorvesLib::Modules::Gameplay::World
{
    const ZoneProfileStack::Entry* ZoneProfileStack::FindActive() const
    {
        const Entry* active = nullptr;
        for (const Entry& entry : m_Entries)
        {
            if (!active || entry.Priority > active->Priority ||
                (entry.Priority == active->Priority && entry.Sequence > active->Sequence))
            {
                active = &entry;
            }
        }
        return active;
    }

    ZoneProfileChange ZoneProfileStack::MakeChange(const Core::Container::String& oldProfileId,
                                                    float oldBlendSeconds) const
    {
        ZoneProfileChange change;
        change.OldProfileId = oldProfileId;
        const Entry* active = FindActive();
        if (active)
        {
            change.NewProfileId = active->ProfileId;
        }
        change.bChanged = change.OldProfileId != change.NewProfileId;
        if (change.bChanged)
        {
            change.BlendSeconds = active ? active->BlendSeconds : oldBlendSeconds;
        }
        return change;
    }

    ZoneProfileChange ZoneProfileStack::Push(uint64_t ownerKey, const Core::Container::String& profileId,
                                              int32_t priority, float blendSeconds)
    {
        Core::Container::String oldProfileId;
        float oldBlendSeconds = 0.0f;
        if (const Entry* active = FindActive())
        {
            oldProfileId = active->ProfileId;
            oldBlendSeconds = active->BlendSeconds;
        }
        if (profileId.empty())
        {
            return MakeChange(oldProfileId, oldBlendSeconds);
        }

        for (auto it = m_Entries.begin(); it != m_Entries.end(); ++it)
        {
            if (it->OwnerKey == ownerKey)
            {
                m_Entries.erase(it);
                break;
            }
        }

        if (m_NextSequence == std::numeric_limits<uint64_t>::max())
        {
            // 配列は追加順を保つため、通し番号が尽きても優先順位を変えずに振り直せる。
            m_NextSequence = 0;
            for (Entry& entry : m_Entries)
            {
                entry.Sequence = m_NextSequence++;
            }
        }
        m_Entries.push_back({ownerKey, profileId, priority, blendSeconds, m_NextSequence++});
        return MakeChange(oldProfileId, oldBlendSeconds);
    }

    ZoneProfileChange ZoneProfileStack::Remove(uint64_t ownerKey)
    {
        Core::Container::String oldProfileId;
        float oldBlendSeconds = 0.0f;
        if (const Entry* active = FindActive())
        {
            oldProfileId = active->ProfileId;
            oldBlendSeconds = active->BlendSeconds;
        }
        for (auto it = m_Entries.begin(); it != m_Entries.end(); ++it)
        {
            if (it->OwnerKey == ownerKey)
            {
                m_Entries.erase(it);
                break;
            }
        }
        return MakeChange(oldProfileId, oldBlendSeconds);
    }

    ZoneProfileChange ZoneProfileStack::Clear()
    {
        Core::Container::String oldProfileId;
        float oldBlendSeconds = 0.0f;
        if (const Entry* active = FindActive())
        {
            oldProfileId = active->ProfileId;
            oldBlendSeconds = active->BlendSeconds;
        }
        m_Entries.clear();
        m_NextSequence = 0;
        return MakeChange(oldProfileId, oldBlendSeconds);
    }

    Core::Container::String ZoneProfileStack::GetActiveProfileId() const
    {
        if (const Entry* active = FindActive())
        {
            return active->ProfileId;
        }
        return {};
    }
} // namespace NorvesLib::Modules::Gameplay::World
