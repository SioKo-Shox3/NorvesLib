#pragma once
#include "Locomotion/QuadrupedLocomotionParams.h"
#include "Math/Vector3.h"
namespace NorvesLib::Core::Locomotion
{
    struct LocomotionIntent
    {
        // カメラ基準変換後のワールドXZ方向。長さを入力強度として0..1へ制限する。
        Math::Vector3 Move;
        bool bSprint = false, bJumpPressed = false;
    };
    // 呼出し時点の観測値。Stepの区間内は一定とし、衝突後の値は次回渡す。
    struct LocomotionGroundInfo
    {
        Math::Vector3 Normal = Math::Vector3::UnitY;
        Math::Vector3 Velocity, PlatformVelocity;
        float Gravity = 9.81f;
        bool bReady = false, bGrounded = false;
    };
    struct LocomotionState
    {
        Math::Vector3 PlanarVelocity;
        float Speed = 0, Yaw = 0, SmoothedTarget = 0, TargetDerivative = 0;
        float GaitElapsed = 0, Phase = 0;
        float Pitch = 0, Bank = 0;
        float JumpBufferRemaining = 0, CoyoteRemaining = 0, AirTime = 0, LandingRemaining = 0;
        float PreviousVerticalVelocity = 0;
        uint8_t Gait = 0;
        bool bInitialized = false, bWasGrounded = false, bPivot = false, bJumpConsumed = false;
    };
    struct LocomotionOutput
    {
        // 前者は区間積分、後者は終端速度。bodyへは変位/dtを渡し、終端速度と二重加算しない。
        Math::Vector3 PlanarDisplacement, PlanarVelocity;
        // Speedは足場に対する観測実速度。制御側の終端速度はPlanarVelocity。
        float Yaw = 0, DeltaYaw = 0, TurnRate = 0, Speed = 0;
        float GaitWeights[4] = {1, 0, 0, 0};
        float PhaseDelta = 0, Pitch = 0, Bank = 0, AirTime = 0, VerticalVelocity = 0;
        float JumpSpeed = 0;
        uint8_t Gait = 0;
        bool bGrounded = false, bLanded = false, bJumpRequested = false;
    };
    enum class LocomotionStepStatus : uint8_t
    {
        Success,
        InvalidArgument,
        GroundNotReady
    };
    class QuadrupedLocomotionModel final
    {
      public:
        // dtは0..1秒。内部刻みは最大1/240秒で、超過時間は黙って捨てず拒否する。
        // dt=0は状態と単発イベントを進めない。失敗時state/outをともに保持する。
        static LocomotionStepStatus Step(LocomotionState& state, const QuadrupedLocomotionParams& params,
                                         const LocomotionIntent& intent, const LocomotionGroundInfo& ground, float dt,
                                         LocomotionOutput& out);
        static bool IsValidParams(const QuadrupedLocomotionParams& params);
        // bodyが発射を受理したときだけ呼ぶ。拒否時は呼ばず、先行入力の期限まで再要求できる。
        static void AcknowledgeJump(LocomotionState& state);
        static float SlopeSpeedScale(const QuadrupedLocomotionParams& params, const Math::Vector3& normal,
                                     const Math::Vector3& forward);
    };
} // namespace NorvesLib::Core::Locomotion
