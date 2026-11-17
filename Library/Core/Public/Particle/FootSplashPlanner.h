// 足の接地１件から GPU 粒子の単発放出を決める純関数。
#pragma once

#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Particle
{
    /// 足のしぶきの個数と初速度を決める係数。
    struct FootSplashTuning
    {
        uint32_t BaseCount = 12u;        // 濡れ度１・基準の速さでの個数。
        uint32_t MaxCount = 32u;         // 放出個数の上限。
        float ReferenceSpeed = 1.5f;    // 基準の速さ（m/s）。
        float MinSpeedScale = 0.25f;    // 速さ０の倍率。
        float MaxSpeedScale = 2.0f;     // 速さの倍率の上限。MinSpeedScale 以上。
        float WetnessExponent = 1.0f;   // 実効の濡れ度に適用する指数。
        float PuddleWeight = 1.0f;      // 水たまりの被覆を濡れ度へ足す重み。
        float CountJitter = 0.2f;       // 決定的な乱数で個数を揺らす割合。
        float FootVelocityScale = 0.3f; // 足の水平速度を粒へ渡す割合。
        float NormalSpeed = 1.2f;        // 法線方向の初速（m/s）。
        uint32_t EmitterIndex = 0u;     // 効果の中のエミッタ番号。
    };

    /// 接地１件の入力。位置・法線・速度はワールド空間。
    struct FootSplashContact
    {
        uint32_t LegIndex = 0u;       // 種に混ぜる脚の番号。
        uint64_t Sequence = 0u;       // 種に混ぜる接地の通し番号。
        Math::Vector3 GroundPoint;    // 接地の点。
        Math::Vector3 GroundNormal;   // 接地の法線。零なら上向き。
        Math::Vector3 FootVelocity;   // 足先の速度（m/s）。
        float Wetness = 0.0f;         // 地面の濡れ度。
        float PuddleCavity = 0.0f;    // 地形の凹み度。無ければ０。
    };

    /// 接地１件に対応する放出。Count が０なら放出しない。
    struct FootSplashBurst
    {
        Math::Vector3 Position;
        Math::Vector3 Normal;
        Math::Vector3 Velocity;
        uint32_t Count = 0u;
        uint32_t EmitterIndex = 0u;
    };

    /// 正規化済みの地面法線に対し、シェーダーと同じ水たまりの被覆を返す。雨の露出は１。
    float ComputeFootPuddleCoverage(float cavity, float wetness, const Math::Vector3& groundNormal);

    /// 同じ入力から同じ放出を返す。入力が非有限、または基準速度が正で倍率の上限が下限未満なら個数は０。
    /// 速さに対する個数の単調性を保つため、速さか乱数の倍率が０以下の場合も個数は０。
    FootSplashBurst PlanFootSplash(const FootSplashTuning& tuning, const FootSplashContact& contact, uint32_t seed);
} // namespace NorvesLib::Core::Particle
