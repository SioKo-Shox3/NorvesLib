#include "Component/HoldSlotComponent.h"
#include "Object/World.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(HoldSlotComponent, Component)
    HoldSlotComponent::HoldSlotComponent()
    {
        SetTickGroup(ETickGroup::PostPhysics);
    }
    HoldSlotComponent::HoldSlotComponent(const FieldInitializer* f) : Component(f)
    {
        SetTickGroup(ETickGroup::PostPhysics);
    }
    HoldSlotComponent::HoldSlotComponent(const IUnknown* s) : Component(s)
    {
        SetTickGroup(ETickGroup::PostPhysics);
    }
    HoldSlotComponent::~HoldSlotComponent() = default;
    void HoldSlotComponent::Notify(const HoldSlotEvent& event, bool acquired)
    {
        struct Guard
        {
            bool& Value;
            explicit Guard(bool& v) : Value(v)
            {
                Value = true;
            }
            ~Guard()
            {
                Value = false;
            }
        } guard(m_bNotifying);
        const auto listeners = acquired ? OnAcquired : OnReleased;
        listeners.Broadcast(event);
    }
    const HoldSlotDefinition* HoldSlotComponent::FindSlot(Identity name) const
    {
        for (const auto& slot : m_Slots)
            if (slot.Name == name)
                return &slot;
        return nullptr;
    }
    uint32_t HoldSlotComponent::GetOccupancy(Identity name) const
    {
        uint32_t count = 0;
        for (const auto& hold : m_Holds)
            if (hold.Info.Slot == name)
                ++count;
        return count;
    }
    bool HoldSlotComponent::SetSlots(Container::Span<const HoldSlotDefinition> slots)
    {
        if (m_bNotifying || !m_Holds.empty() || slots.size() > 64)
            return false;
        uint32_t capacity = 0;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            const auto& s = slots[i];
            if (!s.Name.IsValid() || s.Capacity == 0 || s.Capacity > 32 || s.AcceptTags.size() > 64)
                return false;
            capacity += s.Capacity;
            for (size_t j = 0; j < i; ++j)
                if (s.Name == slots[j].Name)
                    return false;
            for (const auto& tag : s.AcceptTags)
                if (!tag.IsValid())
                    return false;
        }
        Container::VariableArray<HoldSlotDefinition> candidate(slots.begin(), slots.end());
        m_Slots = std::move(candidate);
        m_Holds.reserve(capacity);
        return true;
    }

    bool HoldSlotComponent::ApplySlotsFile(const Container::String& path, Animation::SocketReport& report)
    {
        Container::String json;
        return Animation::ReadSocketSettingsFile(path, json, report) && ApplySlotsJson(json, report);
    }
    bool HoldSlotComponent::ApplySlotsJson(const Container::String& json, Animation::SocketReport& report)
    {
        JsonDocument doc;
        if (!Animation::ParseSocketSettingsDocument(json, doc, report))
            return false;
        auto fail = [&]() {
            report.Error = Animation::SocketError::InvalidJson;
            report.Detail = _T("hold_slots");
            return false;
        };
        const auto list = doc.GetRoot().FindMember("holdSlots");
        if (!list.IsArray() || list.GetArraySize() > 64)
            return fail();
        Container::VariableArray<HoldSlotDefinition> candidate;
        const auto name = [&](JsonValue value, Identity& out) {
            if (!value.IsString() || value.AsString().empty() || value.AsString().size() > 1024)
                return false;
            out = Identity(value.AsString());
            Animation::SocketDefinition check;
            check.Name = out;
            check.ParentJoint = 0;
            return Animation::ValidateSockets({&check, 1}, 1, report);
        };
        for (size_t i = 0; i < list.GetArraySize(); ++i)
        {
            auto value = list.GetArrayElement(i);
            if (!value.IsObject() || value.GetObjectSize() > 64)
                return fail();
            for (size_t a = 0; a < value.GetObjectSize(); ++a)
                for (size_t b = 0; b < a; ++b)
                    if (value.GetMemberName(a) == value.GetMemberName(b))
                        return fail();
            HoldSlotDefinition slot;
            if (!name(value.FindMember("name"), slot.Name))
                return fail();
            if (value.HasMember("capacity"))
            {
                auto n = value.FindMember("capacity");
                if (!n.IsIntegerLiteral() || n.AsNumber() < 1 || n.AsNumber() > 32)
                    return fail();
                slot.Capacity = n.AsUInt32();
            }
            if (value.HasMember("acceptTags"))
            {
                auto tags = value.FindMember("acceptTags");
                if (!tags.IsArray() || tags.GetArraySize() > 64)
                    return fail();
                for (size_t j = 0; j < tags.GetArraySize(); ++j)
                {
                    Identity tag;
                    if (!name(tags.GetArrayElement(j), tag))
                        return fail();
                    slot.AcceptTags.push_back(tag);
                }
            }
            candidate.push_back(std::move(slot));
        }
        if (!SetSlots(candidate))
            return fail();
        return true;
    }
    void HoldSlotComponent::Prune()
    {
        if (m_bNotifying)
            return;
        auto* world = GetOwner() ? GetOwner()->GetWorld() : nullptr;
        for (size_t i = 0; i < m_Holds.size();)
        {
            const auto hold = m_Holds[i];
            if (world && world->FindEntityByObjectId(hold.Info.Item))
            {
                ++i;
                continue;
            }
            m_Holds.erase(m_Holds.begin() + i);
            Notify(hold.Info, false);
        }
    }
    HoldAcquireResult HoldSlotComponent::TryAcquire(Identity slotName, uint64_t item,
                                                    Container::Span<const Identity> tags, float mass,
                                                    bool transitioning)
    {
        if (m_bNotifying)
            return HoldAcquireResult::DeniedTransition;
        if (!GetOwner() || !GetOwner()->GetWorld() || IsPendingDestroy() || !IsActive() || !item ||
            item == GetOwner()->GetObjectId() || !std::isfinite(mass) || mass <= 0)
            return HoldAcquireResult::InvalidRequest;
        auto* entity = GetOwner()->GetWorld()->FindEntityByObjectId(item);
        if (!entity || !entity->IsActive())
            return HoldAcquireResult::InvalidRequest;
        Prune();
        const auto* slot = FindSlot(slotName);
        if (!slot)
            return HoldAcquireResult::InvalidRequest;
        for (const auto& hold : m_Holds)
            if (hold.Info.Item == item)
            {
                if (hold.bTransitioning)
                    return HoldAcquireResult::DeniedTransition;
                return hold.Info.Slot == slotName ? HoldAcquireResult::AlreadyHeld : HoldAcquireResult::DeniedOccupied;
            }
        bool accepted = slot->AcceptTags.empty();
        for (const auto& tag : tags)
            if (std::find(slot->AcceptTags.begin(), slot->AcceptTags.end(), tag) != slot->AcceptTags.end())
                accepted = true;
        if (!accepted)
            return HoldAcquireResult::DeniedTags;
        if (GetOccupancy(slotName) >= slot->Capacity)
            return HoldAcquireResult::DeniedOccupied;
        Hold hold{{slotName, item, mass}, transitioning};
        m_Holds.push_back(hold);
        Notify(hold.Info, true);
        return HoldAcquireResult::Granted;
    }
    bool HoldSlotComponent::Release(Identity slot, uint64_t item)
    {
        return ReleaseInternal(slot, item, false);
    }
    bool HoldSlotComponent::ReleaseInternal(Identity slot, uint64_t item, bool force)
    {
        if (m_bNotifying)
            return false;
        for (size_t i = 0; i < m_Holds.size(); ++i)
            if (m_Holds[i].Info.Slot == slot && m_Holds[i].Info.Item == item)
            {
                if (m_Holds[i].bTransitioning && !force)
                    return false;
                const auto event = m_Holds[i].Info;
                m_Holds.erase(m_Holds.begin() + i);
                Notify(event, false);
                return true;
            }
        return false;
    }
    bool HoldSlotComponent::SetTransitioning(Identity slot, uint64_t item, bool active)
    {
        if (m_bNotifying)
            return false;
        for (auto& hold : m_Holds)
            if (hold.Info.Slot == slot && hold.Info.Item == item)
            {
                hold.bTransitioning = active;
                return true;
            }
        return false;
    }
    void HoldSlotComponent::ReleaseAll()
    {
        if (m_bNotifying)
            return;
        while (!m_Holds.empty())
        {
            const auto event = m_Holds.back().Info;
            m_Holds.pop_back();
            Notify(event, false);
        }
    }
    void HoldSlotComponent::OnTickGroup(ETickGroup group, float)
    {
        if (group == ETickGroup::PostPhysics)
            Prune();
    }
    void HoldSlotComponent::EndPlay()
    {
        ReleaseAll();
        OnAcquired.Clear();
        OnReleased.Clear();
        Component::EndPlay();
    }
    void HoldSlotComponent::Finalize()
    {
        ReleaseAll();
        OnAcquired.Clear();
        OnReleased.Clear();
        m_Slots.clear();
        Component::Finalize();
    }
} // namespace NorvesLib::Core::Component
