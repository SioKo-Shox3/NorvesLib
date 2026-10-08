#include "Physics/CharacterBodyComponent.h"
#include "Module/ModuleRegistry.h"
#include "Physics/CharacterMover.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include <cmath>
namespace NorvesLib::Modules::Physics
{
    namespace
    {
        PhysicsModule* Module()
        {
            return dynamic_cast<PhysicsModule*>(FindPhysicsModule(Core::Module::GetModuleRegistry()));
        }
        bool Finite(const Math::Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    } // namespace
    IMPLEMENT_CLASS(CharacterBodyComponent, Core::Component::Component)
    EPhysicsResult CharacterBodyComponent::SetSettings(const CharacterBodySettings& settings)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (!CharacterMover::IsValidSettings(settings.Movement) || !std::isfinite(settings.GravityScale) ||
            settings.GravityScale < 0)
            return EPhysicsResult::InvalidArgument;
        m_Settings = settings;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::SetDesiredVelocity(const Math::Vector3& velocity)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (!Finite(velocity))
            return EPhysicsResult::InvalidArgument;
        m_DesiredVelocity = velocity;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::Jump(float speed)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (!std::isfinite(speed) || speed <= 0)
            return EPhysicsResult::InvalidArgument;
        if (m_Mode != CharacterMovementMode::Walking || !m_State.bGrounded)
            return EPhysicsResult::InvalidState;
        m_JumpSpeed = speed;
        m_bJump = true;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::MoveDelta(const Math::Vector3& displacement, float yaw)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (m_Mode != CharacterMovementMode::Walking)
            return EPhysicsResult::InvalidState;
        const auto sum = m_PendingDisplacement + displacement;
        const float sumYaw = m_PendingYaw + yaw;
        if (!Finite(displacement) || !Finite(sum) || !std::isfinite(yaw) || !std::isfinite(sumYaw))
            return EPhysicsResult::InvalidArgument;
        m_PendingDisplacement = sum;
        m_PendingYaw = sumYaw;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::Teleport(const Math::Vector3& position)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (!Finite(position))
            return EPhysicsResult::InvalidArgument;
        m_PendingTeleport = position;
        m_bTeleport = true;
        m_bJump = false;
        m_PendingDisplacement = {};
        m_PendingYaw = 0;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::SetMovementMode(CharacterMovementMode mode)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (mode != CharacterMovementMode::Walking && mode != CharacterMovementMode::External)
            return EPhysicsResult::InvalidArgument;
        if (m_Mode != mode)
        {
            ResetMotion(false);
            m_Mode = mode;
        }
        return EPhysicsResult::Success;
    }
    void CharacterBodyComponent::FixedTick(float dt)
    {
        if (std::isfinite(dt) && dt > 0)
            m_bFixedRequest = true;
    }
    void CharacterBodyComponent::ResetSimulationState()
    {
        const auto serial = m_State.StepSerial;
        m_State = {};
        m_State.StepSerial = serial;
        m_VerticalSpeed = 0;
        m_bAnchor = false;
        m_bLandedEvent = false;
    }
    void CharacterBodyComponent::ResetMotion(bool clearIntent)
    {
        ResetSimulationState();
        m_PendingDisplacement = {};
        m_PendingYaw = 0;
        m_JumpSpeed = 0;
        m_bJump = false;
        m_bTeleport = false;
        if (clearIntent)
        {
            m_DesiredVelocity = {};
            m_bFixedRequest = false;
            m_BoundBody = {};
            m_BoundCollider = {};
        }
    }
    void CharacterBodyComponent::EndPlay()
    {
        ResetMotion(true);
        OnLanded.Clear();
        Core::Component::Component::EndPlay();
    }
    void CharacterBodyComponent::Finalize()
    {
        ResetMotion(true);
        OnLanded.Clear();
        Core::Component::Component::Finalize();
    }
} // namespace NorvesLib::Modules::Physics
