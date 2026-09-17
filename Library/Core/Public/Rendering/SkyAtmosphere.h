#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Core::Rendering
{

    // The visible solar disk is integrated once for the atmosphere source.
    // Keep this shared by the CPU reference and the later LUT generation path.
    inline constexpr float SolarDiskSolidAngleSteradians = 6.87e-5f;

    /**
     * @brief FramePacketで渡す空と太陽のパラメータ
     *
     * 太陽方向は空の高度・方位角から求める。方向ライトが太陽を表す場合は、
     * 同じスナップショット上でその方向と一致させる。ここでの太陽方向は観測点
     * から太陽へ向かう方向で、LightProxy/LightDataのDirection（光の進行方向）
     * にはその逆ベクトルを設定する。
     */
    struct SkyAtmosphereParameters
    {
        bool bEnabled = false;
        float SunAltitudeDegrees = 45.0f;
        float SunAzimuthDegrees = 0.0f;
        float SunLuminanceNits = 1.6e9f;

        float PlanetRadiusMeters = 6360000.0f;
        float AtmosphereHeightMeters = 80000.0f;
        float RayleighScaleHeightMeters = 8000.0f;
        float MieScaleHeightMeters = 1200.0f;
        float MieAnisotropy = 0.8f;
        Math::Vector3 GroundAlbedo = Math::Vector3(0.1f, 0.1f, 0.1f);
    };

    /**
     * @brief 空の参照評価結果
     *
     * R2-P1ではCPUの数値契約に使う。LUTとGPUの接続は後続タスクが担当する。
     */
    struct SkyRadianceSample
    {
        Math::Vector3 Radiance = Math::Vector3::Zero;
        float MeanSunTransmittance = 1.0f;
        bool bValid = false;
    };

    SkyAtmosphereParameters MakeDefaultSkyAtmosphereParameters();

    SkyAtmosphereParameters SanitizeSkyAtmosphereParameters(
        const SkyAtmosphereParameters& parameters);

    /**
     * @brief 高度・方位角から空間上の太陽方向を求める
     *
     * 戻り値は「観測点から太陽へ向かう」方向で、Y上向き・X/Z水平面の規約を使う。
     */
    Math::Vector3 MakeSunDirectionFromAltitudeAzimuth(float altitudeDegrees,
                                                       float azimuthDegrees);

    /**
     * @brief Hillaire 2020系の空実装へ突き合わせるCPU参照サンプルを評価する
     *
     * R2-P1では数値契約を安定させるための平行大気・単一散乱参照経路とする。
     * SunLuminanceNitsは可視太陽ディスクの放射輝度として扱い、固定した太陽
     * ディスク立体角で積分した等価照度を散乱源に使う。後続LUTも同じ単位・
     * sanitize済み入力・float精度の参照条件で突き合わせる。
     * 球殻の有限曲率、多重散乱、地表からの反射はこの参照の適用範囲外であり、
     * PlanetRadiusMeters/AtmosphereHeightMeters/GroundAlbedoは後続LUT用に保持・
     * 正規化するが、P1の平行大気評価では使用しない。
     */
    SkyRadianceSample EvaluateHillaireSkyReference(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection);

    float ComputeSunDiskIrradiance(
        const SkyAtmosphereParameters& parameters);

    float ComputeSunDiskPreExposedLuminance(
        const SkyAtmosphereParameters& parameters,
        float preExposure);

    bool IsSunDiskWithinFp16SafetyRange(
        const SkyAtmosphereParameters& parameters,
        float preExposure,
        float safetyFraction = 0.9f);

} // namespace NorvesLib::Core::Rendering
