#pragma once

#include "Component/Component.h"
#include "Physics/PhysicsTypes.h"

namespace NorvesLib::Modules::Physics
{
    class PhysicsModule;

    class RigidBodyComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(RigidBodyComponent, Core::Component::Component)

    public:
        EPhysicsResult SetBodyType(EPhysicsBodyType bodyType);
        EPhysicsResult SetMass(float mass);
        EPhysicsResult SetGravityScale(float gravityScale);
        EPhysicsResult SetLinearVelocity(const Math::Vector3& velocity);
        EPhysicsResult AddImpulse(const Math::Vector3& impulse);
        Core::Scene::BodyHandle GetBodyHandle() const;
        EPhysicsBodyType GetBodyType() const;
        Math::Vector3 GetLinearVelocity() const;

        void Initialize() override;
        void Finalize() override;

    private:
        friend class PhysicsModule;

        Core::Scene::BodyHandle m_BodyHandle;
        EPhysicsBodyType m_BodyType = EPhysicsBodyType::Static;
        float m_Mass = 1.0f;
        float m_GravityScale = 1.0f;
        Math::Vector3 m_LinearVelocity;
        EPhysicsResult m_LastRegistrationResult = EPhysicsResult::NotRegistered;
    };
} // namespace NorvesLib::Modules::Physics
