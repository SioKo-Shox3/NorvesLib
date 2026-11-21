#pragma once

#include "Delegate/Delegate.h"
#include "ECS/EcsTypes.h"
#include "Locomotion/QuadrupedLocomotionModel.h"
#include "Object/EntityHandle.h"

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Modules::Physics
{
    class CharacterBodyComponent;
}

namespace NorvesLib::Modules::AI
{
    class SteeringAgentComponent;

    class AnimalMotor
    {
    public:
        bool EnsureBinding(Core::Entity& owner, SteeringAgentComponent& agent);
        void Detach(Core::Entity* owner, const Core::World* fallbackWorld = nullptr);
        void SetParams(const Core::Locomotion::QuadrupedLocomotionParams& params);
        void Step(Core::Entity& owner, float dt, const Math::Vector3& desiredVelocity);
        void PublishAnimator(Core::Entity& owner);
        const Core::Locomotion::LocomotionOutput& GetOutput() const { return m_Output; }
        bool IsBound() const { return m_Before.IsBound(); }
        bool IsBoundTo(const Core::Entity& owner) const { return GetBoundBody(owner) != nullptr; }

        /// 平地でモデルの目標速度を操舵速度に合わせるため、速歩か襲歩の上限で入力強度を正規化する。
        static Core::Locomotion::LocomotionIntent MakeIntent(const Math::Vector3& desiredVelocity,
                                                             const Core::Locomotion::QuadrupedLocomotionParams& params);

    private:
        Physics::CharacterBodyComponent* GetBoundBody(const Core::Entity& owner) const;

        Core::Delegate<void, float> m_Before;
        uint64_t m_ConnectionId = 0;
        Core::EntityHandle m_BoundOwnerHandle;
        uint64_t m_BoundOwnerId = 0;
        Core::Ecs::EcsEntity m_BoundBodyRow{};
        uint64_t m_BoundAgentId = 0;
        uint64_t m_AnimatorChildId = 0;
        Core::Locomotion::QuadrupedLocomotionParams m_Params;
        Core::Locomotion::LocomotionState m_State;
        Core::Locomotion::LocomotionOutput m_Output;
    };
}
