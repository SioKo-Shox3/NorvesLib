#pragma once

namespace NorvesLib::Core::Rendering
{
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
    };

    VolumetricFogParameters MakeDefaultVolumetricFogParameters();

    VolumetricFogParameters SanitizeVolumetricFogParameters(
        const VolumetricFogParameters& parameters);

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
