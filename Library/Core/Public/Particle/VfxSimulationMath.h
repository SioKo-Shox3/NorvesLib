// 粒子の乱数・生成数・乱れを CPU と GPU で共有するための計算式。
// GLSL の Assets/Shaders/Particles/ParticleCommon.glsl と同じ式。変えるときは両方を同時に変える。
#pragma once

#include "Math/Vector3.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Particle
{
    /// 粒ごとに乱数を引く順序。
    enum class VfxRandomDraw : uint32_t
    {
        VelocityX = 0,
        VelocityY = 1,
        VelocityZ = 2,
        ShapeA = 3,
        ShapeB = 4,
        ShapeC = 5,
        ChildSpawn = 6,
        Activation = 7
    };

    /// 32 ビット値に PCG ハッシュを適用する。
    inline constexpr uint32_t VfxPcgHash(uint32_t value)
    {
        const uint32_t state = value * 747796405u + 2891336453u;
        const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    /// シード、通し番号、乱数を引く順序からハッシュを得る。
    inline constexpr uint32_t VfxCounterHash(uint32_t seed, uint32_t serial, uint32_t draw)
    {
        return VfxPcgHash(draw ^ VfxPcgHash(serial ^ VfxPcgHash(seed)));
    }

    /// ハッシュの上位 24 ビットを [0, 1) の浮動小数に変換する。
    inline constexpr float VfxHashToUnitFloat(uint32_t hash)
    {
        return static_cast<float>(hash >> 8u) / 16777216.0f;
    }

    /// シード、通し番号、乱数を引く順序から [0, 1) の値を得る。
    inline constexpr float VfxCounterUnitFloat(uint32_t seed, uint32_t serial, VfxRandomDraw draw)
    {
        return VfxHashToUnitFloat(VfxCounterHash(seed, serial, static_cast<uint32_t>(draw)));
    }

    /// 粒の通し番号の有効化のハッシュ [0, 1)。
    inline constexpr float VfxActivationHash(uint32_t seed, uint32_t serial)
    {
        return VfxCounterUnitFloat(seed, serial, VfxRandomDraw::Activation);
    }

    /// hash < fraction の粒の重みを ramp で制限し、無効な粒には 0 を返す。
    inline constexpr float VfxActivationWeight(float fraction, float ramp, float hash)
    {
        if (!(hash < fraction))
        {
            return 0.0f;
        }
        if (!(ramp > 0.0f))
        {
            return 1.0f;
        }

        const float weight = (fraction - hash) / ramp;
        return weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);
    }

    /// rate×deltaTime と持ち越しの端数から、この刻みの整数の生成数を返し、端数を更新する。
    inline uint32_t VfxAdvanceSpawnAccumulator(float& accumulator, float rate, float deltaTime)
    {
        // 通常の端数演算は既存の粒子系と同じ float 精度を保つ。
        const float accumulated = accumulator + rate * deltaTime;
        const double total = std::isfinite(accumulated) ? static_cast<double>(accumulated) :
            static_cast<double>(accumulator) + static_cast<double>(rate) * deltaTime;
        const double whole = std::floor(total);
        accumulator = static_cast<float>(total - whole);
        return whole >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<uint32_t>(whole);
    }

    /// 格子点 (ix, iy, iz) と軸 axis の [0, 1) のハッシュ。
    inline constexpr float VfxLatticeHash(int32_t ix, int32_t iy, int32_t iz, uint32_t axis, uint32_t seed)
    {
        const uint32_t h = VfxPcgHash(static_cast<uint32_t>(ix) ^ VfxPcgHash(static_cast<uint32_t>(iy) ^
            VfxPcgHash(static_cast<uint32_t>(iz) ^ VfxPcgHash(seed ^ axis))));
        return VfxHashToUnitFloat(h);
    }

    /// 位置に対する格子点ハッシュを x、y、z の順に線形補間し、乱れの加速度を返す。
    /// 周波数が正でない、強さが 0、入力が非有限、または格子座標が int32 の範囲外ならゼロ。
    inline Math::Vector3 VfxTurbulenceAcceleration(const Math::Vector3& position, float frequency, float strength, uint32_t seed)
    {
        if (frequency <= 0.0f || strength == 0.0f || !std::isfinite(frequency) || !std::isfinite(strength) ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            return Math::Vector3(0.0f);
        }

        const Math::Vector3 q = position * frequency;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z))
        {
            return Math::Vector3(0.0f);
        }

        const float cellX = std::floor(q.x);
        const float cellY = std::floor(q.y);
        const float cellZ = std::floor(q.z);
        constexpr float gridLimit = 2147483648.0f;
        if (cellX < -gridLimit || cellX >= gridLimit || cellY < -gridLimit || cellY >= gridLimit ||
            cellZ < -gridLimit || cellZ >= gridLimit)
        {
            return Math::Vector3(0.0f);
        }

        const float fractionX = q.x - cellX;
        const float fractionY = q.y - cellY;
        const float fractionZ = q.z - cellZ;
        const int32_t ix = static_cast<int32_t>(cellX);
        const int32_t iy = static_cast<int32_t>(cellY);
        const int32_t iz = static_cast<int32_t>(cellZ);
        // 隣の格子点は符号付き整数のオーバーフローを避けて求める。
        const int32_t nextX = static_cast<int32_t>(static_cast<uint32_t>(ix) + 1u);
        const int32_t nextY = static_cast<int32_t>(static_cast<uint32_t>(iy) + 1u);
        const int32_t nextZ = static_cast<int32_t>(static_cast<uint32_t>(iz) + 1u);

        float acceleration[3] = {};
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            const float h000 = VfxLatticeHash(ix, iy, iz, axis, seed);
            const float h100 = VfxLatticeHash(nextX, iy, iz, axis, seed);
            const float h010 = VfxLatticeHash(ix, nextY, iz, axis, seed);
            const float h110 = VfxLatticeHash(nextX, nextY, iz, axis, seed);
            const float h001 = VfxLatticeHash(ix, iy, nextZ, axis, seed);
            const float h101 = VfxLatticeHash(nextX, iy, nextZ, axis, seed);
            const float h011 = VfxLatticeHash(ix, nextY, nextZ, axis, seed);
            const float h111 = VfxLatticeHash(nextX, nextY, nextZ, axis, seed);

            const float x00 = h000 + (h100 - h000) * fractionX;
            const float x10 = h010 + (h110 - h010) * fractionX;
            const float x01 = h001 + (h101 - h001) * fractionX;
            const float x11 = h011 + (h111 - h011) * fractionX;
            const float y0 = x00 + (x10 - x00) * fractionY;
            const float y1 = x01 + (x11 - x01) * fractionY;
            const float noise = y0 + (y1 - y0) * fractionZ;
            acceleration[axis] = strength * (2.0f * noise - 1.0f);
        }

        return Math::Vector3(acceleration[0], acceleration[1], acceleration[2]);
    }
} // namespace NorvesLib::Core::Particle
