#include "Physics/CharacterBodyComponent.h"
#include "Module/ModuleRegistry.h"
#include "Object/Entity.h"
#include "Object/World.h"
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
        m_bLaunch = false;
        m_bJump = true;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::LaunchVertical(float speed)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (!std::isfinite(speed) || speed <= 0)
            return EPhysicsResult::InvalidArgument;
        if (m_Mode != CharacterMovementMode::Walking || !m_State.bReady)
            return EPhysicsResult::InvalidState;
        m_JumpSpeed = speed;
        m_bJump = true;
        m_bLaunch = true;
        return EPhysicsResult::Success;
    }
    EPhysicsResult CharacterBodyComponent::SetDriveMode(CharacterDriveMode mode)
    {
        auto* module = Module();
        const auto result = module ? module->ValidateCharacterAccess(*this) : EPhysicsResult::NotRegistered;
        if (result != EPhysicsResult::Success)
            return result;
        if (mode != CharacterDriveMode::Fixed && mode != CharacterDriveMode::Variable)
            return EPhysicsResult::InvalidArgument;
        if (m_DriveMode != mode)
        {
            ResetMotion(false);
            m_bFixedRequest = false;
            m_DriveMode = mode;
            if (auto* owner = GetOwner())
                owner->ResetRenderInterpolation();
        }
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
        m_bLaunch = false;
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
    void CharacterBodyComponent::Initialize()
    {
        Core::Component::Component::Initialize();
        SetTickGroup(Core::Component::ETickGroup::Movement);
    }
    void CharacterBodyComponent::Tick(float dt)
    {
        RequestSimulation(dt, CharacterDriveMode::Variable);
    }
    void CharacterBodyComponent::FixedTick(float dt)
    {
        RequestSimulation(dt, CharacterDriveMode::Fixed);
    }
    void CharacterBodyComponent::RequestSimulation(float dt, CharacterDriveMode mode)
    {
        if (m_DriveMode != mode || !std::isfinite(dt) || dt <= 0 || m_bBeforeSimulationActive)
            return;
        auto* module = Module();
        if (!module || module->ValidateCharacterAccess(*this) != EPhysicsResult::Success ||
            module->m_bFixedTickInProgress || module->m_bVariableCharacterInProgress ||
            module->m_bCharacterInputInProgress)
            return;
        auto* owner = GetOwner();
        auto* world = owner->GetWorld();
        const auto ownerId = owner->GetObjectId(), componentId = GetComponentId();
        const auto listeners = BeforeSimulation;
        m_bFixedRequest = true;
        m_bBeforeSimulationActive = true;
        const auto resolve = [world, ownerId, componentId]() -> CharacterBodyComponent* {
            auto* entity = world->FindEntityByObjectId(ownerId);
            if (!entity || entity->IsPendingDestroy())
                return nullptr;
            auto* character = entity->GetComponent<CharacterBodyComponent>();
            return character && character->GetComponentId() == componentId ? character : nullptr;
        };
        try
        {
            struct InputGuard
            {
                bool& Active;
                explicit InputGuard(bool& active) : Active(active)
                {
                    Active = true;
                }
                ~InputGuard()
                {
                    Active = false;
                }
            } guard(module->m_bCharacterInputInProgress);
            listeners.Broadcast(dt);
        }
        catch (...)
        {
            if (auto* live = resolve())
            {
                live->m_bBeforeSimulationActive = false;
                live->m_bFixedRequest = false;
            }
            throw;
        }
        auto* live = resolve();
        if (!live)
            return;
        live->m_bBeforeSimulationActive = false;
        if (live->m_DriveMode != mode || !live->IsActive() || live->HasFlag(Core::OF_PendingDestroy))
        {
            live->m_bFixedRequest = false;
            return;
        }
        if (mode == CharacterDriveMode::Variable)
            if (auto* currentModule = Module())
                currentModule->ProcessVariableCharacter(*live, dt);
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
        m_bLaunch = false;
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
        BeforeSimulation.Clear();
        Core::Component::Component::EndPlay();
    }
    void CharacterBodyComponent::Finalize()
    {
        ResetMotion(true);
        OnLanded.Clear();
        BeforeSimulation.Clear();
        Core::Component::Component::Finalize();
    }
} // namespace NorvesLib::Modules::Physics
