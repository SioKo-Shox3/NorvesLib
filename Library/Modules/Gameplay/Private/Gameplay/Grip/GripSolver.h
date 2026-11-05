// しがみつきの拘束、荷重、顎のゲージをフレーム単位で計算する。
#pragma once

#include "Gameplay/Grip/GripTuning.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Grip
{
    inline constexpr float kGripStandardGravity = 9.81f;
    inline constexpr float kGripMaxDeltaSeconds = 1.0f / 30.0f;
    inline constexpr float kGripTargetSubstepSeconds = 1.0f / 120.0f;
    inline constexpr uint32_t kGripHardMaxSubsteps = 8;

    struct GripBodyState
    {
        Math::Vector3 Position;
        Math::Vector3 Velocity;
    };

    struct GripSolverInput
    {
        Math::Vector3 GripPrevious;
        Math::Vector3 GripCurrent;
        float Length = 0.0f;
        float DeltaSeconds = 0.0f;
        Math::Vector3 Gravity = Math::Vector3(0.0f, -9.81f, 0.0f);
        float SwingDampingPerSecond = 0.0f;
        uint32_t MaxSubsteps = kGripHardMaxSubsteps;
    };

    struct GripSolverStep
    {
        uint32_t Substeps = 0;
        float SubstepSeconds = 0.0f;
        float MeanLoadG = 0.0f;
        float PeakLoadG = 0.0f;
        Math::Vector3 MeanTensionPerMass;
        float ResidualMeters = 0.0f;
    };

    enum class EGripSolverResult : uint8_t
    {
        Ok,
        InvalidInput
    };

    /**
     * @brief 握りの点から距離 L を保つ位置積分と、補正から荷重を計算する。
     * dtc = min(dt, 1/30)、N = clamp(ceil(dtc / (1/120) - 1e-4), 1, MaxSubsteps)、h = dtc / N。
     * pred = x + v h + g h²、c = G + normalize(pred - G) L - pred。
     * 握りの点に対する接線速度には exp(-減衰 × h) を掛ける。
     * 各補正の |c| / h² / 9.81 の平均と最大を g 単位で返し、c / h² の平均を m/s² で返す。
     * 位置と L は m、速度は m/s、重力は m/s²、時間は秒、減衰は毎秒。
     * 方向が定まらないときは前の位置、次に重力の逆向き、重力もゼロなら上向きを使う。
     * 有限でない入力、L または dt が正でない値、負の減衰、1〜8 以外の分割数、
     * 計算結果を float で表せない場合は拒否し、body と step を変更しない。
     * 可変のフレームで 1 フレームに 1 回呼ぶ。固定ステップでは 1 フレームに 0〜8 回走り、
     * 姿勢が同じで張力が 1 回目に集中するため。
     */
    EGripSolverResult StepGripSolver(const GripSolverInput& input, GripBodyState& inOutBody, GripSolverStep& outStep);

    /**
     * @brief 荷重を alpha = 1 - exp(-dt / τ) で指数平滑する。
     * 荷重は g、dt と時定数 τ は秒。τ = 0 なら生の荷重をそのまま返す。
     * 有限でない入力、負の dt または時定数は拒否し、NaN を返す。
     * 可変のフレームで 1 フレームに 1 回呼ぶ。固定ステップでは 1 フレームに 0〜8 回走り、
     * 姿勢が同じで張力が 1 回目に集中するため。
     */
    float SmoothGripLoadG(float previousSmoothedLoadG, float rawLoadG, float deltaSeconds, float smoothingSeconds);

    /**
     * @brief 張力とアンカーの加速度から振り落とす荷重を求める。
     * clamp(TensionWeight × tensionLoadG + AccelerationWeight × |anchorAcceleration| / 9.81,
     * 0, MaxLoadG)。荷重は g、加速度は m/s²、重みは無次元。
     * 有限でない荷重・加速度や、不正な調整値は拒否し、NaN を返す。
     * 可変のフレームで 1 フレームに 1 回呼ぶ。固定ステップでは 1 フレームに 0〜8 回走り、
     * 姿勢が同じで張力が 1 回目に集中するため。
     */
    float MixGripShakeLoadG(float tensionLoadG, const Math::Vector3& anchorAcceleration, const GripTuning& tuning);

    struct GripGaugeState
    {
        float Value = 1.0f;
        bool bDepleted = false;
    };

    struct GripGaugeStep
    {
        float Drained = 0.0f;
        float Recovered = 0.0f;
        bool bDepletedNow = false;
    };

    /**
     * @brief 荷重による顎のゲージの減少、または足場上での回復を計算する。
     * 減少は (base + k × smoothstep(load; free, panic)) × dt、回復は recovery × dt。
     * t = clamp((load - free) / (panic - free), 0, 1)、smoothstep = t²(3 - 2t)。
     * 荷重が free 以下かつ足が接地しているときは減少せず回復し、枯渇後はリセットまで変化しない。
     * 荷重は g、dt は秒、各率は毎秒、ゲージと実際の増減は満タンを 1 とする無次元値。
     * 有限でない入力、負の dt、[0, 1] 外のゲージ、不正な調整値は拒否し、state と step を変えない。
     * 可変のフレームで 1 フレームに 1 回呼ぶ。固定ステップでは 1 フレームに 0〜8 回走り、
     * 姿勢が同じで張力が 1 回目に集中するため。
     */
    bool StepGripGauge(GripGaugeState& state, float shakeLoadG, bool bFeetOnSurface, float deltaSeconds,
                       const GripTuning& tuning, GripGaugeStep& outStep);

    /**
     * @brief ゲージを Value = 1、bDepleted = false に戻す。
     * Value は満タンを 1 とする無次元値。入力を拒否せず、再開時に呼ぶ。
     * フレームの計算列は可変のフレームで 1 フレームに 1 回呼ぶ。固定ステップでは
     * 1 フレームに 0〜8 回走り、姿勢が同じで張力が 1 回目に集中するため。
     */
    void ResetGripGauge(GripGaugeState& state);
} // namespace NorvesLib::Modules::Gameplay::Grip
