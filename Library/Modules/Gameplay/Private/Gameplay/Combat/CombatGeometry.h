#pragma once

#include "Gameplay/Combat/CombatShape.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Combat
{
    struct CombatPose
    {
        Math::Vector3 Position;
        Math::Quaternion Rotation;
    };

    struct CombatSweepVolume
    {
        CombatShape Shape;
        CombatPose Previous;
        CombatPose Current;
    };

    struct CombatSweepSettings
    {
        uint32_t MaxSubsteps = 16;
        float MinRadius = 0.01f;
        uint32_t RefineIterations = 12;
    };

    struct CombatSweepContact
    {
        Math::Vector3 Point;
        // 被弾側から攻撃側へ向く単位法線。
        Math::Vector3 Normal;
        float Depth = 0.0f;
        float S = 0.0f;
        uint32_t SubstepCount = 0;
    };

    enum class ECombatSweepResult : uint8_t
    {
        Miss,
        Hit,
        InvalidInput
    };

    CombatPose InterpolateCombatPose(const CombatPose& t0, const CombatPose& t1, float s);
    bool IsValidCombatVolume(const CombatSweepVolume& volume);
    bool IsValidCombatSettings(const CombatSweepSettings& settings);

    // 不正な入力の場合は 0 を返す。
    uint32_t ComputeCombatSubstepCount(const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox,
                                       const CombatSweepSettings& settings);

    ECombatSweepResult SweepCombatVolumes(const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox,
                                         const CombatSweepSettings& settings, CombatSweepContact& outContact);

    // 不正な入力や範囲外の s、非接触時は出力を変えず false を返す。
    bool ComputeCombatContactAt(const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox, float s,
                                CombatSweepContact& outContact);
} // namespace NorvesLib::Modules::Gameplay::Combat
