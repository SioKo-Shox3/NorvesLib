#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Animation::Procedural
{
    inline constexpr double SpringSubstepSeconds = 1.0 / 120.0;
    inline constexpr int32_t SpringMaxSubsteps = 4;

    // 周波数は有限の正数、減衰比は有限かつ 0 より大きく 1 以下。
    // 1 との差が 1e-4 以下なら臨界減衰として扱う。
    struct SpringSettings
    {
        float FrequencyHz = 1.0f;
        float DampingRatio = 1.0f;
    };

    // 1/120 秒の積分回数を求め、1 回の Advance で最大 4 回まで進める。
    // 上限到達後も 1 ステップ分に近い余りがあれば全て捨て、秒数を返す。
    // Reset は余りを 0 に戻す。不正な dt では状態も出力も変更しない。
    class SpringStepAccumulator
    {
    public:
        void Reset();
        bool Advance(double dt, int32_t& outSubsteps, double& outDroppedSeconds);
        double GetRemainderSeconds() const { return m_remainderSeconds; }

    private:
        double m_remainderSeconds = 0.0;
    };

    // 静止した目標へ追従するスカラーばね。初期状態は位置・速度とも 0。
    // 固定 1/120 秒刻みで最大 4 回進め、超過して捨てた秒数を返す。
    // Reset は位置・速度を指定値、余りを 0 に戻す。失敗時は状態と出力を保つ。
    class ScalarSpring
    {
    public:
        bool Reset(float position = 0.0f, float velocity = 0.0f);
        bool Advance(float target, float dt, const SpringSettings& settings, float& outDroppedSeconds);
        float GetPosition() const { return m_position; }
        float GetVelocity() const { return m_velocity; }
        double GetRemainderSeconds() const { return m_accumulator.GetRemainderSeconds(); }

    private:
        float m_position = 0.0f;
        float m_velocity = 0.0f;
        SpringStepAccumulator m_accumulator;
    };

    // 静止した目標へ軸ごとに追従するベクトルばね。初期位置・速度はゼロ。
    // 固定 1/120 秒刻みで最大 4 回進め、超過して捨てた秒数を返す。
    // Reset は位置・速度を指定値、余りを 0 に戻す。失敗時は状態と出力を保つ。
    class VectorSpring
    {
    public:
        bool Reset(const Math::Vector3& position = Math::Vector3(), const Math::Vector3& velocity = Math::Vector3());
        bool Advance(const Math::Vector3& target, float dt, const SpringSettings& settings, float& outDroppedSeconds);
        Math::Vector3 GetPosition() const { return m_position; }
        Math::Vector3 GetVelocity() const { return m_velocity; }
        double GetRemainderSeconds() const { return m_accumulator.GetRemainderSeconds(); }

    private:
        Math::Vector3 m_position;
        Math::Vector3 m_velocity;
        SpringStepAccumulator m_accumulator;
    };

    // 回転差を最短の回転ベクトルに変換して追従するばね。角速度の単位は rad/s。
    // 初期回転は単位四元数、初期角速度はゼロ。回転入力は正規化し、ゼロ四元数を拒否する。
    // 固定 1/120 秒刻みで最大 4 回進め、超過して捨てた秒数を返す。
    // Reset は回転・角速度を指定値、余りを 0 に戻す。失敗時は状態と出力を保つ。
    class AngularSpring
    {
    public:
        bool Reset(const Math::Quaternion& rotation = Math::Quaternion(),
                   const Math::Vector3& angularVelocity = Math::Vector3());
        bool Advance(const Math::Quaternion& target, float dt, const SpringSettings& settings, float& outDroppedSeconds);
        Math::Quaternion GetRotation() const { return m_rotation; }
        Math::Vector3 GetAngularVelocity() const { return m_angularVelocity; }
        double GetRemainderSeconds() const { return m_accumulator.GetRemainderSeconds(); }

    private:
        Math::Quaternion m_rotation;
        Math::Vector3 m_angularVelocity;
        SpringStepAccumulator m_accumulator;
    };
} // namespace NorvesLib::Core::Animation::Procedural
