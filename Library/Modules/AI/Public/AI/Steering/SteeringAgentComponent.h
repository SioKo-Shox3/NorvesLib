#pragma once

#include "Component/Component.h"
#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Object/EntityHandle.h"

#include <cstdint>

namespace NorvesLib::Core::Locomotion
{
    struct LocomotionOutput;
}

namespace NorvesLib::Modules::AI
{
    class AnimalMotor;

    enum class ESteeringDimension : uint8_t
    {
        Ground,
        Flight
    };

    enum class ESteeringRequest : uint8_t
    {
        Idle,
        Arrive,
        MoveTo,
        Pursue,
        Flee,
        KeepDistance,
        Wander,
        FollowTrail
    };

    struct SteeringRequest
    {
        ESteeringRequest Kind = ESteeringRequest::Idle;
        Math::Vector3 TargetPosition;      // Arrive・MoveTo の目的地
        Core::EntityHandle TargetEntity;   // 追跡・逃走・距離維持・追従の相手
        float StopRadius = 0.1f;
        float MinDistance = 6.0f;
        float MaxDistance = 10.0f;
        float FollowDistance = 2.0f;
        uint32_t Revision = 0;             // 未要求は 0。受理ごとに更新する。
    };

    /// 動物の操舵設定と現在の要求を保持する。
    class SteeringAgentComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(SteeringAgentComponent, Core::Component::Component)

    public:
        SteeringAgentComponent();
        ~SteeringAgentComponent() override;

        PROPERTY(ESteeringDimension, Dimension)
        PROPERTY(float, MaxSpeed)              // 最大速度（m/s）
        PROPERTY(float, MaxAcceleration)       // 最大加速度（m/s²）
        PROPERTY(Math::Vector4, TurnRateBySpeed) // 速度比 0、1/3、2/3、1 での旋回率上限（rad/s）
        PROPERTY(Core::Container::String, NavProfileName)
        PROPERTY(Core::Container::String, MotorProfilePath)
        PROPERTY(uint32_t, WanderSeed)

    public:
        void Initialize() override;
        void EndPlay() override;
        void Finalize() override;
        void OnPropertyApplied(const Core::ClassProperty& property) override;

        // GameThread で要求する。入力が不正な場合は false を返し、直前の要求を保つ。
        bool RequestIdle();
        bool RequestArrive(const Math::Vector3& target, float stopRadius = 0.1f);
        bool RequestMoveTo(const Math::Vector3& goal, float stopRadius = 0.3f);
        bool RequestPursue(Core::EntityHandle target);
        bool RequestFlee(Core::EntityHandle threat);
        bool RequestKeepDistance(Core::EntityHandle target, float minDistance, float maxDistance);
        bool RequestWander();
        bool RequestFollowTrail(Core::EntityHandle leader, float followDistance = 2.0f);
        const SteeringRequest& GetRequest() const { return m_Request; }

        /// AIModule が固定ステップで決めたワールド座標系の操舵速度（m/s）。
        const Math::Vector3& GetSteeringVelocity() const { return m_SteeringVelocity; }
        bool IsRegisteredToModule() const { return m_bRegistered; }
        bool IsMotorBound() const;
        const Core::Locomotion::LocomotionOutput* GetMotorOutput() const;

    private:
        friend class AIModule;
        friend class SteeringSystem;
        friend class AnimalMotor;
        bool AcceptRequest(SteeringRequest request);
        void DetachMotorAndUnregister();
        void SetSteeringVelocity(const Math::Vector3& velocity);
        AnimalMotor& GetOrCreateMotor();
        bool EnsureMotorBinding();
        void StepMotor(float dt);
        void PublishMotorVisual();
        void ReloadMotorProfile();

        SteeringRequest m_Request;
        Core::Container::TUniquePtr<AnimalMotor> m_Motor;
        Math::Vector3 m_SteeringVelocity;
        bool m_bRegistered = false;
    };
}
