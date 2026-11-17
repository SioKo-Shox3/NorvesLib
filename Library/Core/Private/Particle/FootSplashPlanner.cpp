// 足の接地１件から GPU 粒子の単発放出を決める純関数。
#include "Particle/FootSplashPlanner.h"

#include "Particle/VfxSimulationMath.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        constexpr float PuddleLevelWetnessStart = 0.3f;
        constexpr float PuddleMaxLevel = 0.8f;
        constexpr float PuddleCoverageBand = 0.1f;
        constexpr float PuddleUpStart = 0.90f;
        constexpr float PuddleUpEnd = 0.98f;

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        float SmoothStep(float edge0, float edge1, float value)
        {
            const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float SaturateVelocity(double value)
        {
            // 有限の入力でも初速が float の範囲を超える場合は有限の端へ収める。
            return static_cast<float>(std::clamp(value, -static_cast<double>(FLT_MAX), static_cast<double>(FLT_MAX)));
        }
    }

    float ComputeFootPuddleCoverage(float cavity, float wetness, const Math::Vector3& groundNormal)
    {
        // Puddle.glsl の水位、上向き度、被覆と同じ順序で計算する。
        float level = (wetness - PuddleLevelWetnessStart) / (1.0f - PuddleLevelWetnessStart);
        const float waterLevel = (level >= 0.0f) ? std::min(level, 1.0f) : 0.0f;
        const float c = (cavity >= 0.0f) ? std::min(cavity, 1.0f) : 0.0f;
        level = (waterLevel >= 0.0f) ? std::min(waterLevel, 1.0f) : 0.0f;
        const float exposure = 1.0f;
        const float threshold = 1.0f - PuddleMaxLevel * level;
        const float coverage = SmoothStep(threshold, threshold + PuddleCoverageBand, c);
        const float upFacing = (groundNormal.y >= 0.0f) ? SmoothStep(PuddleUpStart, PuddleUpEnd, groundNormal.y) : 0.0f;
        return coverage * upFacing * exposure;
    }

    FootSplashBurst PlanFootSplash(const FootSplashTuning& tuning, const FootSplashContact& contact, uint32_t seed)
    {
        FootSplashBurst burst;
        burst.EmitterIndex = tuning.EmitterIndex;
        if (!IsFinite(contact.GroundPoint) || !IsFinite(contact.GroundNormal) || !IsFinite(contact.FootVelocity) ||
            !std::isfinite(contact.Wetness) || !std::isfinite(contact.PuddleCavity) ||
            !std::isfinite(tuning.ReferenceSpeed) || !std::isfinite(tuning.MinSpeedScale) ||
            !std::isfinite(tuning.MaxSpeedScale) || !std::isfinite(tuning.WetnessExponent) ||
            !std::isfinite(tuning.PuddleWeight) || !std::isfinite(tuning.CountJitter) ||
            !std::isfinite(tuning.FootVelocityScale) || !std::isfinite(tuning.NormalSpeed) ||
            (tuning.ReferenceSpeed > 0.0f && tuning.MaxSpeedScale < tuning.MinSpeedScale))
        {
            return burst;
        }

        // 法線の二乗は double で求め、極端に大きい・小さい有限値でも正規化する。
        double normalX = contact.GroundNormal.x;
        double normalY = contact.GroundNormal.y;
        double normalZ = contact.GroundNormal.z;
        double normalLengthSquared = normalX * normalX + normalY * normalY + normalZ * normalZ;
        if (normalLengthSquared == 0.0)
        {
            normalY = 1.0;
            normalLengthSquared = 1.0;
        }
        const double inverseNormalLength = 1.0 / std::sqrt(normalLengthSquared);
        const double nx = normalX * inverseNormalLength;
        const double ny = normalY * inverseNormalLength;
        const double nz = normalZ * inverseNormalLength;
        burst.Position = contact.GroundPoint;
        burst.Normal = Math::Vector3(static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz));

        // 法線に直交する成分（足の速度の水平の成分）を n × (v × n) / |n|² で求める。
        const double crossX = contact.FootVelocity.y * normalZ - contact.FootVelocity.z * normalY;
        const double crossY = contact.FootVelocity.z * normalX - contact.FootVelocity.x * normalZ;
        const double crossZ = contact.FootVelocity.x * normalY - contact.FootVelocity.y * normalX;
        const double horizontalX = (normalY * crossZ - normalZ * crossY) / normalLengthSquared;
        const double horizontalY = (normalZ * crossX - normalX * crossZ) / normalLengthSquared;
        const double horizontalZ = (normalX * crossY - normalY * crossX) / normalLengthSquared;
        burst.Velocity = Math::Vector3(
            SaturateVelocity(tuning.FootVelocityScale * horizontalX + tuning.NormalSpeed * nx),
            SaturateVelocity(tuning.FootVelocityScale * horizontalY + tuning.NormalSpeed * ny),
            SaturateVelocity(tuning.FootVelocityScale * horizontalZ + tuning.NormalSpeed * nz));

        if (contact.Wetness <= 0.0f || tuning.BaseCount == 0u || tuning.MaxCount == 0u)
        {
            return burst;
        }
        const float wetness = std::clamp(contact.Wetness, 0.0f, 1.0f);
        const float cavity = std::clamp(contact.PuddleCavity, 0.0f, 1.0f);
        const float coverage = ComputeFootPuddleCoverage(cavity, wetness, burst.Normal);
        const double effectiveWetness = std::clamp(static_cast<double>(wetness) +
            static_cast<double>(tuning.PuddleWeight) * coverage, 0.0, 1.0);
        if (effectiveWetness <= 0.0)
        {
            return burst;
        }

        const double speed = std::sqrt(horizontalX * horizontalX + horizontalY * horizontalY + horizontalZ * horizontalZ);
        double speedScale = 1.0;
        if (tuning.ReferenceSpeed > 0.0f)
        {
            const double minScale = tuning.MinSpeedScale;
            speedScale = std::clamp(minScale + (1.0 - minScale) * speed / tuning.ReferenceSpeed,
                minScale, static_cast<double>(tuning.MaxSpeedScale));
        }

        // 種と脚番号に通し番号の上位・下位の両方を混ぜ、速さや濡れ度には依存させない。
        uint32_t hash = VfxPcgHash(seed);
        hash = VfxPcgHash(hash ^ contact.LegIndex);
        hash = VfxPcgHash(hash ^ static_cast<uint32_t>(contact.Sequence));
        hash = VfxPcgHash(hash ^ static_cast<uint32_t>(contact.Sequence >> 32u));
        const double random = 2.0 * VfxHashToUnitFloat(hash) - 1.0;
        const double jitter = 1.0 + static_cast<double>(tuning.CountJitter) * random;
        if (speedScale <= 0.0 || jitter <= 0.0)
        {
            return burst;
        }

        const double count = tuning.BaseCount * std::pow(effectiveWetness, tuning.WetnessExponent) * speedScale * jitter;
        if (count >= static_cast<double>(tuning.MaxCount))
        {
            burst.Count = tuning.MaxCount;
        }
        else if (count > 0.0)
        {
            burst.Count = static_cast<uint32_t>(std::round(count));
        }
        return burst;
    }
} // namespace NorvesLib::Core::Particle
