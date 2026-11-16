// GameThread の気象の正本。2 つ以上の系が参照する値だけを持ち、毛の濡れや局所の雨遮蔽などは持たない。
#pragma once

#include "Rendering/EnvironmentParameters.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "Rendering/Wind.h"

#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Environment
{
    /** @brief GameThread で共有する気象の状態。 */
    struct EnvironmentState
    {
        /// 雨の強さ（無次元、[0, 1]）。
        float RainIntensity = 0.0f;
        /// 積分して更新する地表の濡れ（無次元、[0, 1]）。
        float Wetness = 0.0f;
        /// 均一な曇天の度合い（無次元、[0, 1]）。雲の層の被覆とは別。
        float CloudCover = 0.0f;

        /// 風が吹いていく水平な方位（度、[0, 360)）。
        float WindDirectionYawDegrees = 0.0f;
        /// 平均風速（m/s、[0, 100]）。
        float WindBaseSpeed = 3.0f;
        /// 平均風速に対する突風の振れ幅の比（無次元、[0, 1]）。
        float WindGustStrength = 0.4f;

        /// 基準の高さでの霧の密度（ワールド単位の逆数、0 以上）。
        float FogDensityAtBaseHeight = 0.01f;
        /// 霧の密度の基準となる高さ（ワールド単位、有限値）。
        float FogBaseHeight = 0.0f;
        /// 高さに対する霧の密度の減衰（ワールド単位の逆数、0 以上）。
        float FogHeightFalloffPerUnit = 0.01f;
        /// 霧を積む距離の上限（m、0 以上。0 は視線全体）。
        float FogMaxDistanceMeters = 0.0f;
        /// 霧の周りの光に掛ける倍率（無次元、0 以上）。
        float FogAmbientScale = 1.0f;

        /// 雲の層を使うか（false または true）。
        bool bCloudLayerEnabled = false;
        /// 雲の層が空を覆う割合の目安（無次元、[0, 1]）。
        float CloudLayerCoverage = 0.45f;

        /// 手動で指定する太陽の高度（度、[-90, 90]）。
        float SunAltitudeDegrees = 45.0f;
        /// 手動で指定する太陽の方位（度、[-360, 360]。空の SkyAtmosphereParameters と同じ範囲）。
        float SunAzimuthDegrees = 0.0f;
        /// 保持する時刻（時、[0, 24)）。自動では進めない。
        float TimeOfDayHours = 12.0f;

        /// 雨量 1 のとき、平衡との差が約 5 % になる時間（秒、0.001 以上）。
        float WetTimeConstantSeconds = 45.0f;
        /// 雨量 0 のとき、平衡との差が約 5 % になる時間（秒、0.001 以上）。
        float DryTimeConstantSeconds = 900.0f;
        /// ゲーム内の経過時刻（秒、0 以上）。
        double GameTimeSeconds = 0.0;
        /// 気象の乱数の種（無次元、uint32_t の全範囲）。
        uint32_t Seed = 0;
        /// 気象の不連続を示す番号（無次元、uint32_t の全範囲）。
        uint32_t DiscontinuityId = 0;
    };

    static_assert(std::is_trivially_copyable_v<EnvironmentState>);

    /**
     * @brief 遷移または上書きの対象となる状態の欄。
     * Wetness は積分される状態なので SetTarget の遷移には使わず、SetImmediate と上書きの層でのみ使う。
     * CloudLayerEnabled は 0 または 1 の値として読み書きする。
     */
    enum class EEnvironmentField : uint32_t
    {
        RainIntensity,
        CloudCover,
        WindDirectionYaw,
        WindBaseSpeed,
        WindGustStrength,
        FogDensityAtBaseHeight,
        FogBaseHeight,
        FogHeightFalloffPerUnit,
        FogMaxDistanceMeters,
        FogAmbientScale,
        CloudLayerEnabled,
        CloudLayerCoverage,
        SunAltitude,
        SunAzimuth,
        TimeOfDayHours,
        Wetness,
        Count
    };

    using EnvironmentFieldMask = uint32_t;
    inline constexpr uint32_t EnvironmentFieldCount = static_cast<uint32_t>(EEnvironmentField::Count);
    static_assert(EnvironmentFieldCount < 32u);

    /// 有効な欄に対応するビットを返す。範囲外の欄は 0 を返す。
    constexpr EnvironmentFieldMask EnvironmentFieldBit(EEnvironmentField field)
    {
        const uint32_t index = static_cast<uint32_t>(field);
        return index < EnvironmentFieldCount ? (EnvironmentFieldMask{1} << index) : 0u;
    }

    /// 有効なすべての欄を示すマスク。
    inline constexpr EnvironmentFieldMask EnvironmentFieldMaskAll =
        (EnvironmentFieldMask{1} << EnvironmentFieldCount) - 1u;

    /** @brief 欄ごとの遷移の進み方。既定は SmoothStep。 */
    enum class ETransitionCurve : uint8_t
    {
        SmoothStep,
        Linear
    };

    /** @brief マスクで指定した欄の遷移先と、欄ごとの所要時間・進み方。 */
    struct EnvironmentTarget
    {
        /// 遷移先を読む欄のマスク。
        EnvironmentFieldMask Mask = 0;
        /// マスクで指定した欄だけを遷移先として読む。
        EnvironmentState Values;
        /// 欄ごとの遷移時間（秒、0 以下は即時）。
        float DurationSeconds[EnvironmentFieldCount] = {};
        /// 欄ごとの遷移の進み方。
        ETransitionCurve Curves[EnvironmentFieldCount] = {};
    };

    /// 全欄に同じ遷移時間と進み方を設定する。
    void SetEnvironmentTargetDuration(EnvironmentTarget& target, float seconds, ETransitionCurve curve);

    /** @brief 状態が持たない描画設定を保持する場面の土台。 */
    struct EnvironmentSceneBase
    {
        Rendering::SkyAtmosphereParameters Sky;
        Rendering::VolumetricFogParameters Fog;
        Rendering::CloudLayerParameters CloudLayer;
        Rendering::WindParameters Wind;
    };

    /** @brief 場面の土台と気象の状態から解決した描画用の値。 */
    struct EnvironmentResolved
    {
        Rendering::SkyAtmosphereParameters Sky;
        Rendering::VolumetricFogParameters Fog;
        Rendering::CloudLayerParameters CloudLayer;
        Rendering::WindParameters Wind;
        Rendering::EnvironmentParameters Environment;
    };

    /// 場面の空・霧・雲の層・風から共有する欄を写し、残りを既定値にした状態を返す。
    EnvironmentState MakeEnvironmentStateFromScene(const Rendering::SkyAtmosphereParameters& sky,
                                                   const Rendering::VolumetricFogParameters& fog,
                                                   const Rendering::CloudLayerParameters& cloudLayer,
                                                   const Rendering::WindParameters& wind);

    /// 場面の描画設定をそのまま保持する土台を返す。
    EnvironmentSceneBase MakeEnvironmentSceneBase(const Rendering::SkyAtmosphereParameters& sky,
                                                   const Rendering::VolumetricFogParameters& fog,
                                                   const Rendering::CloudLayerParameters& cloudLayer,
                                                   const Rendering::WindParameters& wind);

    /// 非有限値を既定値に戻し、各欄を定められた範囲へ整える。
    EnvironmentState SanitizeEnvironmentState(const EnvironmentState& in);

    /// 場面の土台に状態の欄を重ね、値を整えずに描画用の設定を返す。
    EnvironmentResolved ResolveEnvironment(const EnvironmentState& state, const EnvironmentSceneBase& base);

    /**
     * @brief 雨量を一定とした区間の濡れを厳密解で進める。
     * R_eff = smoothstep(0.02, 0.2, rain)、a_wet = 3 / τ_wet、a_dry = 3 / τ_dry、
     * k = a_wet・R_eff + a_dry・(1 - R_eff)、W_eq = a_wet・R_eff / k、
     * W' = W_eq + (W - W_eq)・exp(-dt・k)。τ は平衡との差が exp(-3)、約 5 % になる時間。
     * 引数は有限値、時定数は正とする。dt または k が 0 以下なら濡れを変えずに返す。
     */
    float AdvanceEnvironmentWetness(float wetness, float rainIntensity, double dtSeconds,
                                    float wetTimeConstantSeconds, float dryTimeConstantSeconds);

    /// 進捗を [0, 1] に丸め、指定した遷移曲線の値を返す。
    float EvaluateTransitionCurve(ETransitionCurve curve, float progress);

    /// 指定した欄を数値として読む。範囲外の欄は 0 を返す。
    float GetEnvironmentField(const EnvironmentState& state, EEnvironmentField field);

    /// 指定した欄へ数値を書き込む。範囲外の欄は無視する。
    void SetEnvironmentField(EnvironmentState& state, EEnvironmentField field, float value);
} // namespace NorvesLib::Core::Environment
