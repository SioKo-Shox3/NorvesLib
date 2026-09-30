#pragma once

#include "Container/VariableArray.h"
#include "Math/Vector3.h"

#include <cstdint>

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
        Math::Vector3 GroundAlbedo = Math::Vector3(0.3f, 0.3f, 0.3f);
    };

    /**
     * @brief 空の評価結果
     *
     * Radianceは観測点から見た放射輝度（cd/m^2、RGB）。MeanSunTransmittanceは地表での
     * 太陽の透過率（ComputeSunGroundTransmittance）のRGBの平均。
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
     * @brief 2組の空のパラメータが全項目で等しいか
     *
     * LUT・空由来のIBLを作り直すかどうかの判定に使う（sanitize済みの値どうしで比べる）。
     */
    bool AreSkyAtmosphereParametersEqual(const SkyAtmosphereParameters& lhs,
                                         const SkyAtmosphereParameters& rhs);

    /**
     * @brief 空のパラメータ1組に対する前計算を持つ、球殻の大気の評価器
     *
     * 惑星（PlanetRadiusMeters）と大気（AtmosphereHeightMeters）を同心球とし、Rayleigh・Mieの
     * 散乱係数×高度の指数密度とオゾンの吸収の媒質を視線に沿ってレイマーチする（Hillaire 2020）。各点で
     * 太陽への透過率（同じ密度の光学的深さの積分）と位相関数で単一散乱を求め、2次以降の散乱は
     * 等方の多重散乱 Ψ_ms = L_2nd / (1 - f_ms) の表で足す。視線の透過率は区間ごとの
     * (1 - e^(-τ)) で積分するので、地平線の付近は飽和して白っぽくなる。地平線より下の視線は
     * 地面（GroundAlbedoのランバート面を透過した太陽と空の照度で照らしたもの）に、そこまでの
     * 透過率を掛けた値と、そこまでの散乱を返す。
     *
     * 前計算（太陽への透過率の表・多重散乱の表・地表の空の照度）は構築時に1回だけ行う。
     * LUT・空由来のIBLのように多くの方向を評価する側は、1つ作って使い回す。
     */
    class SkyAtmosphereModel
    {
    public:
        explicit SkyAtmosphereModel(const SkyAtmosphereParameters& parameters);

        /** sanitize済みのパラメータ */
        const SkyAtmosphereParameters& GetParameters() const { return m_Parameters; }

        /**
         * @brief 観測点（地表から ObserverAltitudeMeters の高さ）からviewDirectionを見た放射輝度
         *
         * 空が無効、または方向が0・非有限ならbValid=falseで放射輝度0。
         */
        SkyRadianceSample EvaluateViewRadiance(const Math::Vector3& viewDirection) const;

        /**
         * @brief 仰角×太陽からの方位差の表（sky-view）を作る
         *
         * 空の放射輝度は太陽を含む鉛直面について対称なので、正距円筒のLUT・空由来のIBLのように
         * 多くの方向を評価する側は、この表を1回作ってSampleSkyViewで補間する（直接の評価より
         * レイマーチの回数が1桁少ない）。仰角は観測点の地平線（わずかに下向き）を境に両側へ、
         * 方位差は太陽の側を細かくする。
         */
        void BuildSkyViewTable();

        /** BuildSkyViewTableの表を補間した放射輝度。表が無ければEvaluateViewRadianceと同じ。 */
        SkyRadianceSample SampleSkyView(const Math::Vector3& viewDirection) const;

        /** 地表での太陽の透過率（RGB）。ComputeSunGroundTransmittanceと同じ値。 */
        const Math::Vector3& GetSunGroundTransmittance() const
        {
            return m_SunGroundTransmittance;
        }

        /**
         * @brief 前計算した表から引く大気の透過率（RGB）
         *
         * ComputeAtmosphereTransmittanceと同じ積分の表を、光学的深さで補間する。透過率LUTを
         * 埋めるときのように多くの点を引く側が使う。空が無効なら0。
         */
        Math::Vector3 GetTransmittance(float altitudeFraction, float cosine) const;

        /** 地表の水平面に届く空の照度（太陽円盤を除く、lux、RGB） */
        Math::Vector3 GetGroundSkyIlluminance() const;

        /** 空の放射輝度を評価する観測点の地表からの高さ（m） */
        static constexpr float ObserverAltitudeMeters = 100.0f;

    private:
        struct Rgb
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
        };

        struct MarchResult
        {
            Rgb Inscattering;
            Rgb Throughput;
            Rgb Transfer;
            bool bHitGround = false;
            double GroundNormal[3] = {0.0, 0.0, 0.0};
        };

        void BuildTransmittanceTable();
        void BuildMultipleScatteringTable();
        void BuildGroundSkyIrradiance();

        Rgb LookupTransmittance(double radius, double cosine) const;
        Rgb LookupMultipleScattering(double radius, double sunCosine) const;
        MarchResult March(const double origin[3],
                          const double direction[3],
                          const double sunDirection[3],
                          uint32_t stepCount,
                          bool bIsotropicSingleScattering,
                          bool bIncludeMultipleScattering) const;

        double SkyViewRowToElevation(uint32_t row) const;

        SkyAtmosphereParameters m_Parameters;
        double m_SunDirection[3] = {0.0, 1.0, 0.0};
        double m_SunAzimuth = 0.0;
        double m_HorizonElevation = 0.0;
        float m_SunDiskIrradiance = 0.0f;
        Math::Vector3 m_SunGroundTransmittance = Math::Vector3::Zero;
        Rgb m_GroundSkyIrradiance;
        Container::VariableArray<float> m_TransmittanceTable;
        Container::VariableArray<float> m_MultipleScatteringTable;
        Container::VariableArray<float> m_SkyViewTable;
    };

    /**
     * @brief 観測点から見た空の放射輝度をSkyAtmosphereModelで評価する
     *
     * 1方向だけ評価する入口で、呼ぶたびに前計算を作る。多くの方向を評価するときは
     * SkyAtmosphereModelを1つ作って使う。SunLuminanceNitsは可視太陽ディスクの放射輝度として
     * 扱い、固定した太陽ディスク立体角で積分した照度を散乱源に使う。
     */
    SkyRadianceSample EvaluateHillaireSkyReference(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection);

    /**
     * @brief 地表の観測点から見た空の放射輝度（EvaluateHillaireSkyReferenceと同じ値）
     *
     * 視線の透過率はレイマーチの中で積分済みなので、別に透過率を掛けない。空のradiance LUT、
     * 空由来のIBL、ラスタの背景、PTの不交差、RTGI・DDGIの不交差は、この値を同じ空として共有する。
     */
    SkyRadianceSample EvaluateSkyViewRadiance(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection);

    float ComputeSunDiskIrradiance(
        const SkyAtmosphereParameters& parameters);

    /**
     * @brief 大気を通る光の透過率（RGB）を求める
     *
     * 高度 altitudeFraction×AtmosphereHeightMeters の点から天頂との余弦 cosine の向きに、
     * 大気の上端まで Rayleigh・Mie の消散係数とオゾンの吸収係数×高度の密度を数値積分した
     * 光学的深さの指数減衰。
     * 光路が惑星に当たるなら0。透過率LUT・空の太陽の地表照度・空の評価はこの積分を共有する。
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
