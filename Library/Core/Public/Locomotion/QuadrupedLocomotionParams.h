#pragma once
#include <cstdint>
namespace NorvesLib::Core::Locomotion
{
    struct GaitParams
    {
        float SpeedMin, SpeedMax;
        float UpThreshold, DownThreshold;
        float MinDwell;
        float Acceleration, Deceleration, Brake;
        float TurnRateCap, LateralAcceleration, StrideLength;
    };
    // 数値は調整開始用の仮値。体格・歩様クリップ・ゲームの巡航速度を確定するものではない。
    struct QuadrupedLocomotionParams
    {
        GaitParams Gaits[4] = {{0, 2, 2.2f, 0, .2f, 5, 6, 12, 4, 8, .8f},
                               {1.6f, 4, 4.4f, 1.8f, .2f, 6, 7, 14, 3, 9, 1.4f},
                               {3.5f, 7, 7.7f, 3.6f, .25f, 7, 8, 16, 2.5f, 10, 2},
                               {6, 12, 12, 6.3f, .3f, 8, 9, 18, 2, 12, 2.8f}};
        float TargetHalfLife = .1f;
        float PivotSpeed = .5f, PivotEnterAngle = 1.2f, PivotExitAngle = .25f, PivotRate = 4;
        float TurnSlowdown = .15f;
        float SlopeLimit = .872664626f, UphillCoefficient = .7f, DownhillBoost = .1f, MinimumSlopeScale = .2f;
        float LeanGain = 1, MaximumBank = .45f, LeanHalfLife = .1f;
        float JumpHeightMin = .6f, JumpHeightMax = 1.2f, CoyoteTime = .1f, JumpBuffer = .12f;
        float AirControl = .25f, AirTurnRate = 1.5f;
        float LandingSpeedLoss = .15f, LandingRecovery = .12f;
    };
} // namespace NorvesLib::Core::Locomotion
