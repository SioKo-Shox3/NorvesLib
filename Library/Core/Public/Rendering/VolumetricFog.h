#pragma once

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief Parameters for exponentially decreasing height fog density.
     *
     * Density at height y is DensityAtBaseHeight * exp(-HeightFalloffPerUnit * (y - BaseHeight)).
     * Heights and distances use world units; HeightFalloffPerUnit uses inverse world units.
     */
    struct VolumetricFogParameters
    {
        bool bEnabled = false;
        float DensityAtBaseHeight = 0.01f;
        float BaseHeight = 0.0f;
        float HeightFalloffPerUnit = 0.01f;
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
