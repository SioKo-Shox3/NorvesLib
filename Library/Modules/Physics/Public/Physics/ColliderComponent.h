#pragma once

#include "Component/Component.h"
#include "Delegate/Delegate.h"
#include "Physics/PhysicsTypes.h"

namespace NorvesLib::Modules::Physics
{
    class PhysicsModule;

    class ColliderComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(ColliderComponent, Core::Component::Component)

    public:
        EPhysicsResult SetSphere(float radius);
        EPhysicsResult SetBox(const Math::Vector3& halfExtents);
        EPhysicsResult SetCapsule(float radius, float halfHeight);
        EPhysicsResult SetTrigger(bool bTrigger);
        Core::Scene::ColliderHandle GetColliderHandle() const;
        EPhysicsResult AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&> callback, PhysicsCallbackHandle& outHandle);
        EPhysicsResult RemoveOnOverlapBegin(PhysicsCallbackHandle handle);
        EPhysicsResult AddOnOverlapEnd(Core::Delegate<void, const PhysicsContactEvent&> callback, PhysicsCallbackHandle& outHandle);
        EPhysicsResult RemoveOnOverlapEnd(PhysicsCallbackHandle handle);
        EPhysicsResult AddOnHit(Core::Delegate<void, const PhysicsContactEvent&> callback, PhysicsCallbackHandle& outHandle);
        EPhysicsResult RemoveOnHit(PhysicsCallbackHandle handle);

        void Initialize() override;
        void Finalize() override;

    private:
        enum class EColliderShape : uint8_t
        {
            None,
            Sphere,
            Box,
            Capsule,
        };

        struct CallbackSlot
        {
            Core::Delegate<void, const PhysicsContactEvent&> Callback;
            uint32_t Generation = 1;
            bool bOccupied = false;
        };

        friend class PhysicsModule;

        Core::Scene::ColliderHandle m_ColliderHandle;
        float m_Radius = 0.0f;
        Math::Vector3 m_HalfExtents;
        float m_CapsuleHalfHeight = 0.0f;
        EColliderShape m_Shape = EColliderShape::None;
        bool m_bHasShape = false;
        bool m_bTrigger = false;
        EPhysicsResult m_LastRegistrationResult = EPhysicsResult::NotRegistered;
        Core::Container::VariableArray<CallbackSlot> m_OverlapBeginCallbacks;
        Core::Container::VariableArray<CallbackSlot> m_OverlapEndCallbacks;
        Core::Container::VariableArray<CallbackSlot> m_HitCallbacks;
        Core::Container::VariableArray<uint32_t> m_FreeOverlapBeginCallbackIndices;
        Core::Container::VariableArray<uint32_t> m_FreeOverlapEndCallbackIndices;
        Core::Container::VariableArray<uint32_t> m_FreeHitCallbackIndices;
    };
} // namespace NorvesLib::Modules::Physics
