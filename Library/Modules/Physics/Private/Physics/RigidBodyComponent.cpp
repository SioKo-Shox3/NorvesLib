#include "Physics/RigidBodyComponent.h"

#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Module/ModuleRegistry.h"

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        PhysicsModule* FindRegisteredPhysicsModule()
        {
            return dynamic_cast<PhysicsModule*>(
                FindPhysicsModule(Core::Module::GetModuleRegistry()));
        }
    } // namespace

    IMPLEMENT_CLASS(RigidBodyComponent, Core::Component::Component)

    EPhysicsResult RigidBodyComponent::SetBodyType(EPhysicsBodyType bodyType)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetBodyType(*this, bodyType) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult RigidBodyComponent::SetMass(float mass)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetBodyMass(*this, mass) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult RigidBodyComponent::SetGravityScale(float gravityScale)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetBodyGravityScale(*this, gravityScale) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult RigidBodyComponent::SetLinearVelocity(const Math::Vector3& velocity)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->SetBodyLinearVelocity(*this, velocity) : EPhysicsResult::NotRegistered;
    }

    EPhysicsResult RigidBodyComponent::AddImpulse(const Math::Vector3& impulse)
    {
        PhysicsModule* module = FindRegisteredPhysicsModule();
        return module ? module->AddBodyImpulse(*this, impulse) : EPhysicsResult::NotRegistered;
    }

    Core::Scene::BodyHandle RigidBodyComponent::GetBodyHandle() const
    {
        return m_BodyHandle;
    }

    EPhysicsBodyType RigidBodyComponent::GetBodyType() const
    {
        return m_BodyType;
    }

    Math::Vector3 RigidBodyComponent::GetLinearVelocity() const
    {
        return m_LinearVelocity;
    }

    void RigidBodyComponent::Initialize()
    {
        Core::Component::Component::Initialize();
        PhysicsModule* module = FindRegisteredPhysicsModule();
        m_LastRegistrationResult = module
            ? module->RegisterRigidBody(*this)
            : EPhysicsResult::NotRegistered;
    }

    void RigidBodyComponent::Finalize()
    {
        if (m_BodyHandle.IsValid())
        {
            PhysicsModule* module = FindRegisteredPhysicsModule();
            if (!module)
            {
                return;
            }

            const EPhysicsResult result = module->UnregisterRigidBody(*this);
            if (result != EPhysicsResult::Success)
            {
                return;
            }

            m_LastRegistrationResult = result;
        }

        m_BodyHandle = Core::Scene::BodyHandle{};
        Core::Component::Component::Finalize();
    }
} // namespace NorvesLib::Modules::Physics
