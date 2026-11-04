#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Input/InputTypes.h"
#include "Locomotion/QuadrupedLocomotionParams.h"
#include "Math/Vector3.h"

namespace Game::GameModes::DogMovementAutopilot
{
    enum class AutopilotStage : uint8_t
    {
        StepClimb, StepBlock, SlopeClimb, SlopeBlock, Platform, Gallop,
        Turn, Jump, CameraLow, CameraSprint, CameraWall, Hitstop, Count
    };

    const char* StageName(AutopilotStage stage);
    bool ParseStageName(const NorvesLib::Core::Container::String& name, AutopilotStage& out);

    enum class AutopilotInputKind : uint8_t
    {
        Key, MouseButton, Look
    };

    struct AutopilotInput
    {
        float Time = 0;
        AutopilotInputKind Kind = AutopilotInputKind::Key;
        NorvesLib::Core::Input::KeyCode Key = NorvesLib::Core::Input::KeyCode::None;
        NorvesLib::Core::Input::MouseButton Button = NorvesLib::Core::Input::MouseButton::Left;
        NorvesLib::Core::Input::InputAction Action = NorvesLib::Core::Input::InputAction::Pressed;
        float LookX = 0, LookY = 0;
    };

    struct AutopilotSegment
    {
        AutopilotStage Stage = AutopilotStage::StepClimb;
        NorvesLib::Math::Vector3 Start;
        float YawDegrees = 0, Duration = 0;
        NorvesLib::Core::Container::VariableArray<AutopilotInput> Inputs;
    };

    struct AutopilotScript
    {
        NorvesLib::Core::Container::VariableArray<AutopilotSegment> Segments;
    };

    /// 全12段階を一度ずつ含む台本を解析する。失敗時はoutを保持し、errorに理由を返す。
    bool ParseAutopilotScript(NorvesLib::Core::Container::Span<const uint8_t> bytes, AutopilotScript& out,
                             NorvesLib::Core::Container::String& error);

    // Vector3のコンストラクタはconstexprではないため、寸法は定数評価できる座標で保持する。
    struct CourseVector
    {
        float x, y, z;
        NorvesLib::Math::Vector3 ToVector3() const
        {
            return {x, y, z};
        }
    };
    struct CourseBox
    {
        CourseVector Center, HalfExtents;
        constexpr float TopY() const
        {
            return Center.y + HalfExtents.y;
        }
    };
    struct CourseSlope
    {
        CourseVector Start;
        float Width, Length, Thickness, AngleDegrees;
        float SinAngle, CosAngle;
        // 上面の低い辺をStartとし、+Zへ登る。箱のX回転は-AngleDegrees。
        constexpr CourseBox Box() const
        {
            return {{Start.x, Start.y + Length * SinAngle * .5f - Thickness * CosAngle * .5f,
                     Start.z + Length * CosAngle * .5f + Thickness * SinAngle * .5f},
                    {Width * .5f, Thickness * .5f, Length * .5f}};
        }
    };
    struct CoursePlatform
    {
        CourseBox Box;
        CourseVector TranslationAmplitude;
        float PeriodSeconds, YawDegreesPerSecond;
    };

    inline constexpr CourseBox Floor{{0, -.5f, 0}, {15, .5f, 15}};
    // 30m四方の床の+X辺へ接続する直線路。既定の最高速度12m/sで6秒以上走れる。
    inline constexpr CourseBox Runway{{50, -.5f, 12}, {35, .5f, 2}};
    inline constexpr CourseBox LowStep{{-10, .1f, 3}, {1.5f, .1f, 3}};
    inline constexpr CourseBox HighStep{{-5, .2f, 3}, {1.5f, .2f, 3}};
    inline constexpr CourseBox Wall{{0, 1.5f, -5}, {4, 1.5f, .1f}};
    inline constexpr CourseSlope LowSlope{{0, 0, 0}, 3, 6, .2f, 30, .5f, .866025404f};
    inline constexpr CourseSlope HighSlope{{5, 0, 0}, 3, 6, .2f, 60, .866025404f, .5f};
    // 中心はt=0の位置。平行移動はAmplitude*sin(2*pi*t/Period)、回転はY軸まわり。
    inline constexpr CoursePlatform Platform{{{-9, .5f, -10}, {2, .5f, 2}}, {1, 0, 0}, 4, 30};

    /// Startは足元（Entity原点）。カプセル中心はここからY方向に0.4m上。
    constexpr CourseVector StageStart(AutopilotStage stage)
    {
        switch (stage)
        {
        case AutopilotStage::StepClimb: return {-10, 0, -1};
        case AutopilotStage::StepBlock: return {-5, 0, -1};
        case AutopilotStage::SlopeClimb: return {0, 0, -1};
        case AutopilotStage::SlopeBlock: return {5, 0, -1};
        case AutopilotStage::Platform: return {-9, 1, -10};
        case AutopilotStage::Gallop:
        case AutopilotStage::CameraSprint: return {-14, 0, 12};
        case AutopilotStage::Turn: return {9, 0, -9};
        case AutopilotStage::Jump: return {9, 0, -3};
        case AutopilotStage::CameraLow:
        case AutopilotStage::Hitstop: return {0, 0, -10};
        case AutopilotStage::CameraWall: return {0, 0, -3.8f};
        default: return {0, 0, 0};
        }
    }

    constexpr float StageYawDegrees(AutopilotStage stage)
    {
        return stage == AutopilotStage::Gallop || stage == AutopilotStage::CameraSprint ? 90.f : 0.f;
    }

    /// 単位はm/s、rad/s、m/s²。いずれかが非有限または0以下、結果がfloat範囲外なら0。
    float ExpectedTurnRadius(float speed, float turnRateCap, float lateralAcceleration);

    /**
     * 平地・直進・停止から全量の前進+Sprintを続ける場合の連続時間近似。
     * 各歩様i=0..2でΔt=max(MinDwell[i], max(0,UpThreshold[i]-v)/Acceleration[i])、
     * v=min(V,v+Acceleration[i]*Δt)、V=Gaits[3].SpeedMaxとしてΔtを合計する。
     * モデルの遷移は観測速度と最短滞在で決まり、SpeedMax自体では加速を制限しない。
     * 目標の臨界減衰応答V*(1-(1+ωt)*exp(-ωt))、ω=1.67834699/TargetHalfLifeの
     * 正規化した遅れの積分2/ωを加える。平滑と加速が重なるため厳密な到達時刻ではない。
     * 接地の観測遅延・離散刻み・衝突は含めない。不正設定・V以上の遷移閾値・桁あふれは0。
     */
    float ExpectedGallopTime(const NorvesLib::Core::Locomotion::QuadrupedLocomotionParams& params);

    /**
     * モデルと同じh=lerp(JumpHeightMin,JumpHeightMax,clamp(v/Gaits[3].SpeedMax,0,1))を使う。
     * 同じ高さへ戻る弾道の飛行時間2*sqrt(2*h/gravity)に離陸時水平速度vを掛ける。
     * 水平速度一定を仮定し、空中入力による加減速・旋回は含めない。着地減速は接地後なので除外。
     * 不正設定・負または非有限の速度・0以下または非有限の重力・桁あふれは0。
     */
    float ExpectedJumpDistance(const NorvesLib::Core::Locomotion::QuadrupedLocomotionParams& params,
                               float takeoffHorizontalSpeed, float gravity);
} // namespace Game::GameModes::DogMovementAutopilot
