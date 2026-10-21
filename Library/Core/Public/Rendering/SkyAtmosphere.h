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
        // シーンの地表の、惑星の表面（海面）からの高度（m）。地表の太陽の透過率・照度、地表の空の照度はこの高度で、
        // 空の放射輝度の観測点はこの高度から SkyAtmosphereModel::ObserverAltitudeMeters 上で求める。惑星の球（地平線より下の
        // 視線が当たる地面）と媒質の密度の高度は海面から数える。0 で地表を海面とみなす
        float GroundAltitudeMeters = 0.0f;
    };

    /**
     * @brief 不透明物に掛ける空気遠近（遠景の霞）のパラメータ
     *
     * 空が有効なときだけ効く。カメラから不透明物までの視線について、空の媒質（Rayleigh・Mie の
     * 消散係数と高度の指数密度）の透過率と、空の放射輝度 LUT・透過率 LUT から求めた散乱光を
     * VolumetricsPass が掛ける。距離は StartDistanceMeters から先だけを数え、DistanceScale 倍する。
     * 媒質の密度は、カメラのワールドの y に OriginAltitudeMeters を足した高度で数える（高地のシーンでは
     * 地表の空気とエアロゾルが海面より薄い）。空の放射輝度 LUT の観測点の高度は変えない。
     */
    struct AerialPerspectiveParameters
    {
        bool bEnabled = false;
        // 視線の距離に掛ける倍率（1 で物理どおり。大きいほど近くから霞む）
        float DistanceScale = 1.0f;
        // これより近い所には霞を掛けない（m）
        float StartDistanceMeters = 0.0f;
        // ワールドの y = 0 の、空の媒質の地表（海面）からの高度（m）。0 で y = 0 を海面とみなす
        float OriginAltitudeMeters = 0.0f;
        // 空気遠近で数える Mie（エアロゾル）の密度の倍率。空の模型の 1 つの指数分布は、空の柱の量に合わせると地表の濃さが
        // 澄んだ乾燥地の実測（視程）より濃くなるので、地表の視線の霞だけを薄める。空の放射輝度 LUT は変えない（1 で空と同じ）
        float MieDensityScale = 1.0f;
    };

    AerialPerspectiveParameters SanitizeAerialPerspectiveParameters(
        const AerialPerspectiveParameters& parameters);

    /**
     * @brief 空に描く雲の層のパラメータ
     *
     * 空が有効なときだけ効く。地表からの高度 BottomAltitudeMeters〜TopAltitudeMeters の球殻に、起動後に作る
     * 3D ノイズ（Perlin-Worley と Worley）の密度を置き、CloudLayerPass が空の画素（深度が遠方面）で光線を進めて
     * 太陽の単一散乱（前方・後方の 2 つの位相）・多重散乱の近似・空の環境光を積分してシーンの色へ重ねる。
     * 雲は太陽を遮らない（地面に影を落とさない）。空由来の IBL にも入れない（IBL は空のパラメータが
     * 変わったときだけ作り直す静的な表で、毎フレーム流れる雲を入れると作り直しが毎フレームになるため）。
     */
    struct CloudLayerParameters
    {
        bool bEnabled = false;
        // 雲の層の底と頂の地表からの高度（m）
        float BottomAltitudeMeters = 1500.0f;
        float TopAltitudeMeters = 4000.0f;
        // 空を覆う割合の目安（0〜1）。天気の模様をこの割合で切り抜く
        float Coverage = 0.45f;
        // 密度 1 の所の消散係数（1/m）。視線の 1 歩（数十〜150 m）で不透明にならない程度にして、照らされた面の光を残す
        float ExtinctionPerMeter = 0.01f;
        // 雲の形の大きさ（m）。基本の形のノイズ 1 周期の水平の長さ
        float ShapeScaleMeters = 9000.0f;
        // 風で流れる速さ（m/s、ワールドの X・Z）
        float WindVelocityX = 8.0f;
        float WindVelocityZ = 3.0f;
    };

    CloudLayerParameters SanitizeCloudLayerParameters(const CloudLayerParameters& parameters);

    /**
     * @brief 空の媒質の高度 0 での消散係数（1/m）
     *
     * Rayleigh は RGB（散乱のみで吸収なし）、Mie は散乱と吸収の和で波長によらない。
     * オゾンは地表付近で密度 0 なので含めない。
     */
    struct AtmosphereGroundExtinction
    {
        Math::Vector3 Rayleigh = Math::Vector3::Zero;
        float Mie = 0.0f;
    };

    AtmosphereGroundExtinction GetAtmosphereGroundExtinction();

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

        /**
         * @brief previous と太陽に依らない設定が同じなら、透過率と多重散乱の表を previous から写して作る
         *
         * 2 つの表は惑星の半径・大気の厚さ・尺度高さ・地表のアルベドだけで決まり、太陽の向きと明るさ・地表の高度・
         * Mie の異方性に依らない。太陽が動くたびに作り直す側（空の LUT・空由来の IBL）は、前の model を渡して
         * 表の前計算を省く（多重散乱の表がいちばん重い）。previous が nullptr か設定が違えば 1 引数の構築と同じ。
         */
        SkyAtmosphereModel(const SkyAtmosphereParameters& parameters, const SkyAtmosphereModel* previous);

        /** 透過率と多重散乱の表を、previous から写して作ったか */
        bool ReusedSunIndependentTables() const { return m_bReusedSunIndependentTables; }

        /** sanitize済みのパラメータ */
        const SkyAtmosphereParameters& GetParameters() const { return m_Parameters; }

        /**
         * @brief 観測点（地表 GroundAltitudeMeters から ObserverAltitudeMeters の高さ）からviewDirectionを見た放射輝度
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

        /** 地表（GroundAltitudeMeters）での太陽の透過率（RGB）。ComputeSunGroundTransmittanceと同じ値。 */
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

        /** 地表（GroundAltitudeMeters）の水平面に届く空の照度（太陽円盤を除く、lux、RGB） */
        Math::Vector3 GetGroundSkyIlluminance() const;

        /** 空の放射輝度を評価する観測点の、地表（GroundAltitudeMeters）からの高さ（m） */
        static constexpr float ObserverAltitudeMeters = 100.0f;

        /** 空の放射輝度を評価する観測点の、惑星の表面（海面）からの高度（m）。GroundAltitudeMeters + ObserverAltitudeMeters */
        float GetObserverAltitudeMeters() const
        {
            return m_Parameters.GroundAltitudeMeters + ObserverAltitudeMeters;
        }

        /**
         * @brief 前計算の表の 1 つ（撮影の大気の書き出しが、表の値をそのまま書くための読み出し口）
         *
         * Data は Rows × Columns のセルが行の順に並び、1 セルは RGB の 3 つ。表が無ければ Data は nullptr。
         */
        struct TableView
        {
            const float* Data = nullptr;
            uint32_t Rows = 0;
            uint32_t Columns = 0;
        };

        /** 透過率の表。セルは光学的深さ（無次元。透過率は exp(-値)）。行 = 高度（GetTransmittanceTableAltitudeMeters）、列 = 天頂角の余弦 */
        TableView GetTransmittanceTable() const;
        double GetTransmittanceTableAltitudeMeters(uint32_t row) const;
        static double GetTransmittanceTableCosine(uint32_t column);

        /** 多重散乱の表 Ψ_ms（太陽の照度 1 lux あたりの放射輝度、cd/m² / lux = 1/sr）。行 = 高度、列 = 太陽の天頂角の余弦 */
        TableView GetMultipleScatteringTable() const;
        double GetMultipleScatteringTableAltitudeMeters(uint32_t row) const;
        static double GetMultipleScatteringTableSunCosine(uint32_t column);

        /** sky-view の表（BuildSkyViewTable の後だけ。放射輝度 cd/m²）。行 = 仰角、列 = 太陽からの方位差（どちらもラジアン） */
        TableView GetSkyViewTable() const;
        double GetSkyViewTableElevationRadians(uint32_t row) const;
        static double GetSkyViewTableAzimuthDifferenceRadians(uint32_t column);

        /** 太陽の方位角（atan2(z, x)、ラジアン）と、観測点から見た地平線の仰角（ラジアン、負） */
        double GetSunAzimuthRadians() const { return m_SunAzimuth; }
        double GetHorizonElevationRadians() const { return m_HorizonElevation; }

        /** 大気の外での太陽円盤の照度（lux。太陽に垂直な面） */
        float GetSunDiskIrradiance() const { return m_SunDiskIrradiance; }

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
        bool m_bReusedSunIndependentTables = false;
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
     * @brief 地表（GroundAltitudeMeters）から見た太陽の透過率（RGB）。空が無効なら0。
     */
    Math::Vector3 ComputeSunGroundTransmittance(
        const SkyAtmosphereParameters& parameters);

    /**
     * @brief 地表（GroundAltitudeMeters）での太陽の照度（lux、RGB）。太陽円盤の照度×地表の透過率。
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
