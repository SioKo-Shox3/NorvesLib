#pragma once

#include "Math/Vector3.h"
#include <cstdint>

/// 動物の操舵を計算する。長さ 0、NaN、無限大、極端な有限値を含む入力でも、
/// すべての関数の数値出力は有限となる。同じ入力と状態からは同じ結果を返す。
namespace NorvesLib::Modules::AI::Steering
{
    enum class SteeringPlane : uint8_t
    {
        Ground, // 高さを除き、XZ 平面で操舵する。
        Flight
    };

    struct SteeringKinematics
    {
        Math::Vector3 Position;
        Math::Vector3 Velocity;
    };

    struct SteeringLimits
    {
        float MaxSpeed = 4.0f;        // 速さの上限（m/s）
        float MaxAcceleration = 8.0f; // 加速度の上限（m/s²）
        float TurnRateBySpeed[4] = {6.0f, 4.0f, 3.0f, 2.0f}; // 速度比 0、1/3、2/3、1 の旋回率（rad/s）
    };

    struct ArriveParams
    {
        float StopRadius = 0.1f;
        float BrakingFactor = 0.8f;
    };

    struct PursueParams
    {
        float MaxPredictionTime = 1.5f;
    };

    enum class KeepDistanceMode : uint8_t
    {
        Hold,
        Approach,
        Retreat
    };

    struct KeepDistanceState
    {
        KeepDistanceMode Mode = KeepDistanceMode::Hold;
        bool bInitialized = false;
    };

    struct KeepDistanceParams
    {
        float MinDistance = 6.0f;
        float MaxDistance = 10.0f;
        float HysteresisFraction = 0.25f;
    };

    struct WanderState
    {
        uint32_t Rng = 0;
        float Angle = 0.0f;
        bool bInitialized = false;
    };

    struct WanderParams
    {
        float CircleDistance = 2.0f;
        float CircleRadius = 1.0f;
        float JitterRate = 3.0f;
        float SpeedFraction = 0.5f;
    };

    /// 全成分が有限かを返す。
    bool IsFiniteVector(const Math::Vector3& v);
    /// 非有限の成分を含むベクトルは零に戻す。
    Math::Vector3 SanitizeVector(const Math::Vector3& v);
    /// 成分を検査し、地上なら高さを除く。
    Math::Vector3 ToPlane(const Math::Vector3& v, SteeringPlane plane);
    /// XZ 平面での長さを返す。float の表現範囲を超えた長さは最大有限値に丸める。
    float PlanarLength(const Math::Vector3& v);
    /// 長さが 1e-6 未満または非有限なら、成分を検査した代替ベクトルを返す。
    Math::Vector3 SafeNormalize(const Math::Vector3& v, const Math::Vector3& fallback);
    /// 向きを保って長さを上限に収める。負または非有限の上限は零とする。
    Math::Vector3 ClampLength(const Math::Vector3& v, float maxLength);
    /// 非有限または負の上限を項目ごとの既定値に戻す。
    SteeringLimits SanitizeLimits(const SteeringLimits& limits);
    /// 速度比に対応する旋回率を隣り合うキーから線形補間する。
    float EvaluateTurnRate(const SteeringLimits& limits, float speed);

    /// 目標へ最大速で向かい、距離が 1e-4 m 未満なら停止する。
    Math::Vector3 Seek(const SteeringKinematics& self, const Math::Vector3& target,
                       const SteeringLimits& limits, SteeringPlane plane);
    /// 脅威から最大速で離れる。重なった場合は現在の進行方向か +Z に逃げる。
    Math::Vector3 Flee(const SteeringKinematics& self, const Math::Vector3& threat,
                       const SteeringLimits& limits, SteeringPlane plane);
    /// 停止半径までの残り距離と加速度から制動できる速さを求める。
    Math::Vector3 Arrive(const SteeringKinematics& self, const Math::Vector3& target,
                         const SteeringLimits& limits, const ArriveParams& params, SteeringPlane plane);
    /// 目標の等速移動を予測し、その位置へ到着するための速度を返す。
    Math::Vector3 Pursue(const SteeringKinematics& self, const SteeringKinematics& target,
                         const SteeringLimits& limits, const PursueParams& pursueParams,
                         const ArriveParams& arriveParams, SteeringPlane plane);
    /// 水平距離が帯を外れたら中央へ戻り、帯を保っている間は目標の速度に合わせる。
    Math::Vector3 KeepDistance(const SteeringKinematics& self, const SteeringKinematics& target,
                               const SteeringLimits& limits, const KeepDistanceParams& params,
                               KeepDistanceState& state, SteeringPlane plane);
    /// 個体ごとの乱数列を初期化する。零の種には固定の非零値を使う。
    void InitializeWander(WanderState& state, uint32_t seed);
    /// 進行方向の前方に置いた円を使って徘徊する。正の有限な時間刻みでのみ乱数と角度を進める。
    Math::Vector3 Wander(const SteeringKinematics& self, const SteeringLimits& limits, const WanderParams& params,
                         WanderState& state, float dt, SteeringPlane plane);
    /// 射影して速さを上限に収めた現在速度から、1 歩の速度変化を MaxAcceleration × dt 以下に制限する。
    /// 現在と希望の水平速度がともに 1e-3 m/s を超えるとき、1 歩の水平方向の変化は旋回率 × dt 以下となる。
    Math::Vector3 ApplySteeringLimits(const Math::Vector3& currentVelocity, const Math::Vector3& desiredVelocity,
                                      const SteeringLimits& limits, float dt, SteeringPlane plane);
}
