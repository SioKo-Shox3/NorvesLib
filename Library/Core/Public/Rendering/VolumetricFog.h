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
    };

    VolumetricFogParameters MakeDefaultVolumetricFogParameters();

    VolumetricFogParameters SanitizeVolumetricFogParameters(
        const VolumetricFogParameters& parameters);

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
