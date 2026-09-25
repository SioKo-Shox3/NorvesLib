#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Core::Rendering
{

    // The visible solar disk is integrated once for the atmosphere source.
    // This corresponds to an angular radius of about 0.00468 rad (0.268 deg).
    // Keep this shared by the CPU reference and the later LUT generation path.
    inline constexpr float SolarDiskSolidAngleSteradians = 6.87e-5f;

    /**
     * @brief FramePacketで渡す空と太陽のパラメータ
     *
     * 太陽方向は空の高度・方位角から求める。空が有効なとき、エンジンは空の太陽を
     * 表す方向光（SkySunLight.h）を光源表へ加えるので、シーンに太陽の方向光を
     * 別に置かない。ここでの太陽方向は観測点から太陽へ向かう方向で、
     * LightProxy/LightDataのDirection（光の進行方向）にはその逆ベクトルを設定する。
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
     * 視線経路の減光、オゾン吸収、Mie消散、球殻の有限曲率、多重散乱、地表から
     * の反射はこの参照の適用範囲外であり、PlanetRadiusMeters/AtmosphereHeightMeters/
     * GroundAlbedoは後続LUT用に保持・正規化するが、P1の平行大気評価では使用しない。
     * ここで固定する値はP1のCPU回帰アンカーであり、P2 LUTの受入れ目標値ではない。
     */
    SkyRadianceSample EvaluateHillaireSkyReference(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection);

    /**
     * @brief 地表から見た空の放射輝度（散乱光×地表からの視線方向の透過率）
     *
     * EvaluateHillaireSkyReferenceの散乱光に、地表からviewDirectionへの大気の透過率
     * （ComputeAtmosphereTransmittance、高度0）を掛ける。空のradiance LUT、空由来のIBL、
     * ラスタの背景、PTの不交差、RTGI・DDGIの不交差は、この値を同じ空として共有する。
     * bValidとMeanSunTransmittanceはEvaluateHillaireSkyReferenceと同じ。
     */
    SkyRadianceSample EvaluateSkyViewRadiance(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection);

    float ComputeSunDiskIrradiance(
        const SkyAtmosphereParameters& parameters);

    /**
     * @brief 大気を通る光の透過率（RGB）を求める
     *
     * 透過率LUTと同じ式で、Rayleighの波長別の散乱とMieの散乱を、高度の密度と
     * 平行大気の光路長（天頂からの余弦、下限0.05）で指数減衰させる。
     * altitudeFractionは0が地表、1が大気の上端で、範囲外は丸める。
     */
    Math::Vector3 ComputeAtmosphereTransmittance(
        const SkyAtmosphereParameters& parameters,
        float altitudeFraction,
        float cosine);

    /**
     * @brief 地表から見た太陽の透過率（RGB）。空が無効なら0。
     */
    Math::Vector3 ComputeSunGroundTransmittance(
        const SkyAtmosphereParameters& parameters);

    /**
     * @brief 地表での太陽の照度（lux、RGB）。太陽円盤の照度×地表の透過率。
     *
     * ラスタの空の太陽の方向光とPTの太陽円盤の光源標本は、この値を共有する。
     * 空が無効なら0。
     */
    Math::Vector3 ComputeSunGroundIlluminance(
        const SkyAtmosphereParameters& parameters);

    float ComputeSunDiskPreExposedLuminance(
        const SkyAtmosphereParameters& parameters,
        float preExposure);

    bool IsSunDiskWithinFp16SafetyRange(
        const SkyAtmosphereParameters& parameters,
        float preExposure,
        float safetyFraction = 0.9f);

} // namespace NorvesLib::Core::Rendering
