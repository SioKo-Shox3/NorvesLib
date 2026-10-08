#pragma once
#include "Animation/SocketTypes.h"
#include "Component/Component.h"
#include "Delegate/MulticastDelegate.h"
namespace NorvesLib::Core::Component
{
    struct HoldSlotDefinition
    {
        Identity Name;
        uint32_t Capacity = 1;
        Container::VariableArray<Identity> AcceptTags;
    };
    enum class HoldAcquireResult : uint8_t
    {
        Granted,
        AlreadyHeld,
        DeniedOccupied,
        DeniedTags,
        DeniedTransition,
        InvalidRequest
    };
    struct HoldSlotEvent
    {
        Identity Slot;
        uint64_t Item = 0;
        float MassMultiplier = 1;
    };
    class HoldSlotComponent : public Component
    {
        REFLECTION_CLASS(HoldSlotComponent, Component)
      public:
        HoldSlotComponent();
        explicit HoldSlotComponent(const FieldInitializer*);
        explicit HoldSlotComponent(const IUnknown*);
        ~HoldSlotComponent() override;
        void EndPlay() override;
        void Finalize() override;
        void OnTickGroup(ETickGroup, float) override;
        [[nodiscard]] bool ApplySlotsJson(const Container::String&, Animation::SocketReport&);
        [[nodiscard]] bool ApplySlotsFile(const Container::String&, Animation::SocketReport&);
        [[nodiscard]] bool SetSlots(Container::Span<const HoldSlotDefinition>);
        [[nodiscard]] HoldAcquireResult TryAcquire(Identity slot, uint64_t item, Container::Span<const Identity> tags,
                                                   float massMultiplier = 1, bool transitioning = false);
        [[nodiscard]] bool Release(Identity slot, uint64_t item);
        [[nodiscard]] bool SetTransitioning(Identity slot, uint64_t item, bool);
        uint32_t GetOccupancy(Identity slot) const;
        const HoldSlotDefinition* FindSlot(Identity) const;
        MulticastDelegate<const HoldSlotEvent&> OnAcquired, OnReleased;

      private:
        friend class SocketAttachmentComponent;
        bool ReleaseInternal(Identity, uint64_t, bool force);
        struct Hold
        {
            HoldSlotEvent Info;
            bool bTransitioning = false;
        };
        Container::VariableArray<HoldSlotDefinition> m_Slots;
        Container::VariableArray<Hold> m_Holds;
        bool m_bNotifying = false;
        void Notify(const HoldSlotEvent&, bool acquired);
        void Prune();
        void ReleaseAll();
    };
} // namespace NorvesLib::Core::Component
