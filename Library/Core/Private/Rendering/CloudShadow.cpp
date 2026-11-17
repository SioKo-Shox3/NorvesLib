// 雲の影の値の無害化、切れ間の重み、および雲の底への投影とマスクの配置を計算する。
#include "Rendering/CloudShadow.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float ClampFinite(float value, float minimum, float maximum, float fallback)
        {
            return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
        }

        float SanitizeMaskExtent(float extentMeters)
        {
            return ClampFinite(extentMeters, 100.0f, 200000.0f, CloudShadowParameters{}.MaskExtentMeters);
        }
    } // namespace

    CloudShadowParameters SanitizeCloudShadowParameters(const CloudShadowParameters& parameters)
    {
        CloudShadowParameters result;
        result.bEnabled = parameters.bEnabled;
        result.MaskExtentMeters = SanitizeMaskExtent(parameters.MaskExtentMeters);
        result.SunGapCount = std::min(parameters.SunGapCount, MaxCloudSunGaps);
        result.bApplyToGroundDirect = parameters.bApplyToGroundDirect;
        const CloudSunGap defaults;
        for (uint32_t i = 0u; i < result.SunGapCount; ++i)
        {
            const CloudSunGap& input = parameters.SunGaps[i];
            CloudSunGap& gap = result.SunGaps[i];
            gap.CenterX = std::isfinite(input.CenterX) ? input.CenterX : defaults.CenterX;
            gap.CenterZ = std::isfinite(input.CenterZ) ? input.CenterZ : defaults.CenterZ;
            gap.RadiusMeters = ClampFinite(input.RadiusMeters, 0.0f, 100000.0f, defaults.RadiusMeters);
            gap.SoftnessMeters = ClampFinite(input.SoftnessMeters, 0.0f, 100000.0f, defaults.SoftnessMeters);
            gap.Strength = ClampFinite(input.Strength, 0.0f, 1.0f, defaults.Strength);
        }
        return result;
    }

    bool AreCloudShadowParametersEqual(const CloudShadowParameters& a, const CloudShadowParameters& b)
    {
        if (a.bEnabled != b.bEnabled || a.MaskExtentMeters != b.MaskExtentMeters ||
            a.SunGapCount != b.SunGapCount || a.bApplyToGroundDirect != b.bApplyToGroundDirect)
        {
            return false;
        }
        for (uint32_t i = 0u; i < MaxCloudSunGaps; ++i)
        {
            const CloudSunGap& gapA = a.SunGaps[i];
            const CloudSunGap& gapB = b.SunGaps[i];
            if (gapA.CenterX != gapB.CenterX || gapA.CenterZ != gapB.CenterZ ||
                gapA.RadiusMeters != gapB.RadiusMeters || gapA.SoftnessMeters != gapB.SoftnessMeters ||
                gapA.Strength != gapB.Strength)
            {
                return false;
            }
        }
        return true;
    }

    float ComputeCloudSunGapWeight(const CloudSunGap& gap, float x, float z)
    {
        const float deltaX = x - gap.CenterX;
        const float deltaZ = z - gap.CenterZ;
        const float distance = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
        if (gap.SoftnessMeters > 0.0f)
        {
            if (distance <= gap.RadiusMeters)
            {
                return 1.0f;
            }
            const float outerRadius = gap.RadiusMeters + gap.SoftnessMeters;
            if (distance >= outerRadius)
            {
                return 0.0f;
            }
            const float t = std::clamp((distance - gap.RadiusMeters) / (outerRadius - gap.RadiusMeters),
                                       0.0f, 1.0f);
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }
        return distance <= gap.RadiusMeters ? 1.0f : 0.0f;
    }

    float ComputeCloudSunGapOpenness(const CloudShadowParameters& parameters, float x, float z)
    {
        if (!parameters.bEnabled)
        {
            return 0.0f;
        }
        float openness = 0.0f;
        const uint32_t count = std::min(parameters.SunGapCount, MaxCloudSunGaps);
        for (uint32_t i = 0u; i < count; ++i)
        {
            const CloudSunGap& gap = parameters.SunGaps[i];
            openness = std::max(openness, ComputeCloudSunGapWeight(gap, x, z) * gap.Strength);
        }
        return std::clamp(openness, 0.0f, 1.0f);
    }

    bool ProjectToCloudBottomPlane(const Math::Vector3& position, const Math::Vector3& sunDirection,
                                   float bottomAltitudeMeters, float& outX, float& outZ)
    {
        if (!std::isfinite(sunDirection.x) || !std::isfinite(sunDirection.y) ||
            !std::isfinite(sunDirection.z) || !std::isfinite(position.x) ||
            !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(bottomAltitudeMeters))
        {
            return false;
        }
        const float length = std::hypot(sunDirection.x, sunDirection.y, sunDirection.z);
        if (!std::isfinite(length) || length <= 0.0f)
        {
            return false;
        }
        const Math::Vector3 s(sunDirection.x / length, sunDirection.y / length, sunDirection.z / length);
        if (s.y < CloudShadowMinSunHeight)
        {
            return false;
        }
        const float t = std::max(bottomAltitudeMeters - position.y, 0.0f) / s.y;
        const float projectedX = position.x + s.x * t;
        const float projectedZ = position.z + s.z * t;
        if (!std::isfinite(projectedX) || !std::isfinite(projectedZ))
        {
            return false;
        }
        outX = projectedX;
        outZ = projectedZ;
        return true;
    }

    CloudShadowMaskPlacement ComputeCloudShadowMaskPlacement(const Math::Vector3& cameraPosition,
                                                             const Math::Vector3& sunDirection,
                                                             float bottomAltitudeMeters, float extentMeters)
    {
        CloudShadowMaskPlacement result;
        result.ExtentMeters = SanitizeMaskExtent(extentMeters);
        result.TexelMeters = result.ExtentMeters / static_cast<float>(CloudShadowMaskResolution);
        float projectedX = 0.0f;
        float projectedZ = 0.0f;
        if (!ProjectToCloudBottomPlane(cameraPosition, sunDirection, bottomAltitudeMeters, projectedX, projectedZ))
        {
            return result;
        }
        const float centerX = std::round(projectedX / result.TexelMeters) * result.TexelMeters;
        const float centerZ = std::round(projectedZ / result.TexelMeters) * result.TexelMeters;
        if (!std::isfinite(centerX) || !std::isfinite(centerZ))
        {
            return result;
        }
        result.CenterX = centerX;
        result.CenterZ = centerZ;
        result.bValid = true;
        return result;
    }
} // namespace NorvesLib::Core::Rendering
