#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 霧と空気遠近の視線の歩の配置。
    enum class VolumetricFogStepDistribution : uint8_t
    {
        Uniform,     // 等間隔（中点則。既定）
        Exponential, // 手前を細かくし、隣の歩の長さを ExponentialStepRatio 倍ずつ伸ばす
    };

    /// 霧だけの経路の歩数。
    inline constexpr uint32_t VolumetricFogOnlyStepCount = 24u;
    /// 霧と空気遠近を一緒に積む歩数。
    inline constexpr uint32_t VolumetricAtmosphereStepCount = 32u;
    /// 霧と空気遠近の経路で、上限より奥の区間に割く歩数。
    inline constexpr uint32_t VolumetricAtmosphereFarStepCount = 8u;
    /// 開発用の参照の歩数（等間隔・ずらし無し・上限無し）。
    inline constexpr uint32_t VolumetricReferenceStepCount = 256u;
    /// 比がこれ未満なら等間隔として扱う。
    inline constexpr float VolumetricMinimumExponentialStepRatio = 1.0001f;

    /**
     * @brief 高さに応じて指数関数的に密度が減る霧の設定。
     *
     * 高さ y の密度は DensityAtBaseHeight * exp(-HeightFalloffPerUnit * (y - BaseHeight))。
     * 高さと距離はワールドの単位、HeightFalloffPerUnit はその逆数を使う。
     */
    struct VolumetricFogParameters
    {
        bool bEnabled = false;
        float DensityAtBaseHeight = 0.01f;
        float BaseHeight = 0.0f;
        float HeightFalloffPerUnit = 0.01f;
        /// 霧を積む距離の上限（m）。0 以下では視線全体。
        float MaxDistanceMeters = 0.0f;
        /// 霧の周りの光に掛ける倍率。
        float AmbientScale = 1.0f;
        /// 霧の色に掛ける色味（線形 RGB の倍率）。
        float Tint[3] = {1.0f, 1.0f, 1.0f};
        /// 霧の色を、視線方向の空の放射輝度から、方位を保って仰角を基準の仰角へ傾けた方向の空の放射輝度へ寄せる割合（0..1）。
        /// 空の放射輝度 LUT を引く経路だけで効く。
        float SkyBlend = 0.0f;
        /// SkyBlend が寄せる先の仰角（度）。
        float ReferenceElevationDegrees = 10.0f;
        /// 歩の配置。
        VolumetricFogStepDistribution StepDistribution = VolumetricFogStepDistribution::Uniform;
        /// 指数の配置で隣の歩の長さの比（1〜2）。VolumetricMinimumExponentialStepRatio 未満は等間隔と同じ。
        float ExponentialStepRatio = 1.15f;
        /// 影つきの散乱を歩で積む距離の上限（m）。0 以下で上限なし。
        /// 霧だけの経路では上限より奥を影なしの解析の透過率の差で足し、
        /// 霧と空気遠近の経路では上限より奥を等間隔の少ない歩で進める。
        float ScatteringCutoffMeters = 0.0f;
        /// 歩の始まりを画素ごとに Interleaved Gradient Noise でずらす。
        bool bPixelJitter = false;
        /// TAA が掛かるフレームだけ、画素ごとのずらしを時間方向にも動かす（真なら画素ごとのずらしも有効）。
        bool bTemporalJitter = false;
    };

    VolumetricFogParameters MakeDefaultVolumetricFogParameters();

    VolumetricFogParameters SanitizeVolumetricFogParameters(
        const VolumetricFogParameters& parameters);

    /// 視線の上の 1 歩（カメラからの距離 m）。
    struct VolumetricFogStep
    {
        float StartMeters = 0.0f;
        float LengthMeters = 0.0f;
    };

    /// [0, spanMeters] を stepCount 歩に分けたときの stepIndex 番目の歩（GLSL の PlaceFogStep と同じ式）。
    VolumetricFogStep ComputeVolumetricFogStep(VolumetricFogStepDistribution distribution, float ratio,
                                             uint32_t stepIndex, uint32_t stepCount, float spanMeters);

    /// 霧だけの経路で、影つきの散乱を歩で積む距離（上限があれば霧の距離と上限の小さい方）。
    float ComputeVolumetricFogScatteringSpan(const VolumetricFogParameters& parameters, float fogDistanceMeters);

    /// 霧と空気遠近を一緒に積む経路の stepIndex 番目の歩（GLSL の PlaceAtmosphereMarchStep と同じ式）。
    VolumetricFogStep ComputeVolumetricAtmosphereMarchStep(const VolumetricFogParameters& parameters,
                                                        uint32_t stepIndex, uint32_t stepCount,
                                                        float rayDistanceMeters);

    /// 霧の色を寄せる基準の仰角の正弦と余弦。
    struct FogReferenceElevationSinCos
    {
        float Sin = 0.0f;
        float Cos = 1.0f;
    };

    /// 安全化した ReferenceElevationDegrees の正弦と余弦を返す（体積の霧・前向きの霧・パストレーサーが同じ値を使う）。
    FogReferenceElevationSinCos ComputeFogReferenceElevationSinCos(const VolumetricFogParameters& parameters);

    /**
     * @brief Evaluate analytic Beer-Lambert transmittance through exponential height fog.
     *
     * rayDirectionY is the vertical component of a normalized ray direction. Invalid ray
     * inputs and disabled fog return identity transmittance (1).
     */
    float ComputeHeightFogTransmittance(
        const VolumetricFogParameters& parameters,
        float rayOriginHeight,
        float rayDirectionY,
        float rayDistance);

} // namespace NorvesLib::Core::Rendering
