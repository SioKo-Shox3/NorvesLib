#pragma once
#include "Camera/CameraCollisionSolver.h"
#include "Container/Span.h"
#include "Math/Vector3.h"
namespace Game::Gameplay
{
    struct FollowCameraProfile
    {
        NorvesLib::Math::Vector3 PositionHalfLives{.08f, .18f, .08f};
        float YawReturnDelay = 1.5f, YawHalfLife = .3f, MinimumYawSpeed = .25f;
        float SpeedReference = 6, ArmGain = .5f, FovGain = 6, BaseFov = 60, FovHalfLife = .2f;
        float SpeedEffectsStrength = .5f, MaximumArmLength = 12;
        float FocusHalfLife = .2f, MaximumFocusDistance = 50;
        float FadeStart = .4f, FadeEnd = 1;
        float TraumaDecay = 1.5f, ShakeStrength = .5f;
        NorvesLib::Math::Vector3 ShakeDegrees{1.5f, 1, .5f};
        float LockBias = .35f, LockBreakDegrees = 2, LockArmGain = .25f;
        bool bAutoFocus = false;
        NorvesLib::Core::Camera::CameraCollisionSettings Collision;
    };
    // 不正な設定は以前の値を保持する。省略された項目は既定値を使う。
    bool ParseFollowCameraProfile(NorvesLib::Core::Container::Span<const uint8_t> bytes, FollowCameraProfile& out);
} // namespace Game::Gameplay
