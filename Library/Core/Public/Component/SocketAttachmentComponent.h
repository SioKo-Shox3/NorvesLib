#pragma once
#include "Animation/AnimationEvents.h"
#include "Animation/SocketTypes.h"
#include "Component/Component.h"
namespace NorvesLib::Core::Component
{
    enum class AttachState : uint8_t
    {
        Detached,
        Blending,
        Attached
    };
    struct AttachmentVelocity
    {
        Math::Vector3 Linear = Math::Vector3::Zero, Angular = Math::Vector3::Zero;
    };
    class SocketAttachmentComponent : public Component
    {
        REFLECTION_CLASS(SocketAttachmentComponent, Component)
      public:
        SocketAttachmentComponent();
        explicit SocketAttachmentComponent(const FieldInitializer*);
        explicit SocketAttachmentComponent(const IUnknown*);
        ~SocketAttachmentComponent() override;
        void EndPlay() override;
        void Finalize() override;
        void OnTickGroup(ETickGroup, float) override;
        [[nodiscard]] bool Attach(uint64_t target, Identity socket, Identity slot = {}, float blendSeconds = .1f,
                                  Container::Span<const Identity> tags = {}, float massMultiplier = 1);
        void Detach();
        [[nodiscard]] bool SetProfiles(Container::Span<const Animation::AttachProfile>);
        [[nodiscard]] bool ApplySettingsFile(const Container::String&, Animation::SocketReport&);
        [[nodiscard]] bool ApplySettingsJson(const Container::String&, Animation::SocketReport&);
        Container::Span<const Animation::AttachProfile> GetProfiles() const
        {
            return m_Profiles;
        }
        [[nodiscard]] bool SetProfileOffset(uint32_t, const Math::Transform&, float blendSeconds = .1f);
        [[nodiscard]] bool SetGrip(const Math::Transform&);
        [[nodiscard]] bool SetProfile(Identity, float blendSeconds = .1f);
        [[nodiscard]] bool SetProfileByIndex(uint32_t, float blendSeconds = .1f);
        Identity GetProfileParameter() const
        {
            return m_ProfileParameter;
        }
        void BindProfileParameter(Identity name)
        {
            m_ProfileParameter = name;
        }
        [[nodiscard]] bool SetVelocitySampling(float maximumDt, float smoothingSeconds);
        AttachState GetAttachState() const noexcept
        {
            return m_State;
        }
        uint64_t GetTargetId() const noexcept
        {
            return m_Target;
        }
        Identity GetSocketName() const
        {
            return m_Socket;
        }
        uint32_t GetProfileIndex() const noexcept
        {
            return m_ProfileIndex;
        }
        const AttachmentVelocity& GetReleaseVelocity() const
        {
            return m_ReleaseVelocity;
        }
        const Math::Transform& GetProfileOffset() const
        {
            return m_ProfileCurrent;
        }

      private:
        Container::VariableArray<Animation::AttachProfile> m_Profiles;
        Math::Transform m_Grip, m_ProfileCurrent, m_ProfileFrom, m_ProfileTarget, m_AttachFrom, m_LastWorld;
        // World所有componentのFinalize中もWorld本体は生存する。Outer解除後の清算にだけ使う。
        World* m_BoundWorld = nullptr;
        uint64_t m_BoundOwner = 0;
        uint64_t m_Target = 0, m_BoundAnimator = 0, m_LastFrame = UINT64_MAX;
        Identity m_Socket, m_Slot, m_ProfileParameter;
        uint32_t m_ProfileIndex = UINT32_MAX;
        float m_ProfileElapsed = 0, m_ProfileDuration = 0, m_AttachElapsed = 0, m_AttachDuration = 0;
        float m_MaximumSampleDt = .1f, m_SmoothingSeconds = .05f;
        AttachState m_State = AttachState::Detached;
        AttachmentVelocity m_Velocity, m_ReleaseVelocity;
        bool m_bVisiting = false, m_bHaveSample = false, m_bLastSuccess = false;
        bool UpdateAttachment(float, uint64_t, uint32_t depth);
        Entity* ResolveTarget() const;
        void UnbindEvents();
        void BindEvents();
        void OnAnimationEvent(const Animation::AnimEventInfo&);
        void MarkSlotTransition(bool);
    };
} // namespace NorvesLib::Core::Component
