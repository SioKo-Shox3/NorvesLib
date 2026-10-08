#pragma once
#include "Component/Component.h"
#include "Delegate/MulticastDelegate.h"
#include "Physics/CharacterBodyTypes.h"
namespace NorvesLib::Modules::Physics
{
    class PhysicsModule;
    // 親Entityなしのroot上で、縦capsule ColliderとKinematic RigidBodyを使う。依存componentを所有しない。
    // capsuleの中心はEntityの真上に置く。形状・局所姿勢はCollider側で設定する。
    class CharacterBodyComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(CharacterBodyComponent, Core::Component::Component)
      public:
        EPhysicsResult SetSettings(const CharacterBodySettings& settings);
        const CharacterBodySettings& GetSettings() const
        {
            return m_Settings;
        }
        // 指定速度は次の変更まで保持する。鉛直成分にも別途GravityScaleの重力を加える。
        EPhysicsResult SetDesiredVelocity(const Math::Vector3& worldVelocity);
        EPhysicsResult Jump(float upwardSpeed);
        // Game側でコヨーテ等の許可判定を済ませた発射口。空中でも鉛直速度を設定する。
        EPhysicsResult LaunchVertical(float upwardSpeed);
        EPhysicsResult SetDriveMode(CharacterDriveMode mode);
        CharacterDriveMode GetDriveMode() const
        {
            return m_DriveMode;
        }
        // 依存が揃うまで要求を保持し、選択したdriveのstepで消費する。移動は積算、Teleportは最後の要求。
        // mode変更・EndPlay・依存component破棄で保留入力を破棄する。query失敗はStateへ記録する。
        EPhysicsResult MoveDelta(const Math::Vector3& worldDisplacement, float worldYaw = 0);
        // 瞬間移動自体は速度に含めない。同stepの入力移動と重力は通常どおり適用する。
        EPhysicsResult Teleport(const Math::Vector3& worldPosition);
        EPhysicsResult SetMovementMode(CharacterMovementMode mode);
        CharacterMovementMode GetMovementMode() const
        {
            return m_Mode;
        }
        const CharacterBodyState& GetState() const
        {
            return m_State;
        }
        // 各実行stepの直前。固定catch-upでもstepごとにmodelを進める。
        // callback後にcomponent identityを再確認し、削除・mode変更時は古い対象を使わない。
        Core::MulticastDelegate<float> BeforeSimulation;
        Core::MulticastDelegate<const CharacterBodyState&> OnLanded;
        void Initialize() override;
        void Tick(float dt) override;
        void FixedTick(float dt) override;
        void EndPlay() override;
        void Finalize() override;

      private:
        friend class PhysicsModule;
        void RequestSimulation(float dt, CharacterDriveMode mode);
        void ResetMotion(bool clearIntent);
        void ResetSimulationState();
        CharacterBodySettings m_Settings;
        CharacterBodyState m_State;
        CharacterDriveMode m_DriveMode = CharacterDriveMode::Fixed;
        bool m_bBeforeSimulationActive = false;
        bool m_bLaunch = false;
        CharacterMovementMode m_Mode = CharacterMovementMode::Walking;
        Math::Vector3 m_DesiredVelocity;
        Math::Vector3 m_PendingDisplacement;
        Math::Vector3 m_PendingTeleport;
        Math::Vector3 m_AnchorLocal;
        Math::Vector3 m_PreviousAnchorWorld;
        Core::Scene::ColliderHandle m_BoundCollider;
        Core::Scene::BodyHandle m_BoundBody;
        float m_PendingYaw = 0, m_JumpSpeed = 0, m_VerticalSpeed = 0, m_PlatformYaw = 0;
        bool m_bJump = false, m_bTeleport = false, m_bFixedRequest = false;
        bool m_bAnchor = false, m_bLandedEvent = false, m_bTeleportedStep = false;
    };
} // namespace NorvesLib::Modules::Physics
