#pragma once
#include "Math/Vector3.h"
#include <cmath>
#include <limits>
namespace NorvesLib::Math
{
    // 静止した目標への臨界減衰の厳密解。初速度0の位置誤差が半分になる時間をhalfLifeとする。
    // 同じ目標なら時間の分割に依存しない。dt=0は不変、halfLife=0は即時追従。
    // 出力どうしのalias・非有限値・負の時間・float範囲外の結果ではfalseを返し、状態を変更しない。
    inline bool TryCriticalDamp(float& position, float& velocity, float target, float halfLife, float dt)
    {
        if (&position == &velocity || !std::isfinite(position) || !std::isfinite(velocity) || !std::isfinite(target) ||
            !std::isfinite(halfLife) || !std::isfinite(dt) || halfLife < 0 || dt < 0)
            return false;
        if (dt == 0)
            return true;
        if (halfLife == 0)
        {
            position = target;
            velocity = 0;
            return true;
        }
        constexpr double halfErrorRoot = 1.6783469900166605;
        const double omega = halfErrorRoot / halfLife;
        const double q = omega * dt;
        if (q >= 750)
        {
            position = target;
            velocity = 0;
            return true;
        }
        const double error = static_cast<double>(position) - target;
        const double decay = std::exp(-q);
        // 小さいqでは target + error の相殺も、一次項どうしの相殺も避ける。
        const double response =
            q < .01 ? -q * q * (.5 - q / 3 + q * q / 8 - q * q * q / 30 + q * q * q * q / 144 - q * q * q * q * q / 840)
                    : std::expm1(-q) + q * decay;
        const double nextPosition = q < .5 ? position + error * response + static_cast<double>(velocity) * dt * decay
                                           : target + (error * (1 + q) + static_cast<double>(velocity) * dt) * decay;
        const double nextVelocity = (velocity * (1 - q) - omega * error * q) * decay;
        constexpr double maximum = std::numeric_limits<float>::max();
        if (!std::isfinite(nextPosition) || !std::isfinite(nextVelocity) || std::fabs(nextPosition) > maximum ||
            std::fabs(nextVelocity) > maximum)
            return false;
        position = static_cast<float>(nextPosition);
        velocity = static_cast<float>(nextVelocity);
        return true;
    }
    // 軸ごとの半減期を持ち、いずれかの軸が不正なら全軸の状態を保持する。
    inline bool TryCriticalDamp(Vector3& position, Vector3& velocity, const Vector3& target, const Vector3& halfLife,
                                float dt)
    {
        if (&position == &velocity)
            return false;
        Vector3 p = position, v = velocity;
        if (!TryCriticalDamp(p.x, v.x, target.x, halfLife.x, dt) ||
            !TryCriticalDamp(p.y, v.y, target.y, halfLife.y, dt) ||
            !TryCriticalDamp(p.z, v.z, target.z, halfLife.z, dt))
            return false;
        position = p;
        velocity = v;
        return true;
    }
} // namespace NorvesLib::Math
