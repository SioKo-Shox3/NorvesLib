#include "Rendering/ViewClearance.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float SanitizeNonNegative(float value, float defaultValue)
        {
            return std::isfinite(value) && value >= 0.0f ? value : defaultValue;
        }

        float SanitizeUnit(float value, float defaultValue)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : defaultValue;
        }

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        double SmoothStep(double start, double end, double value)
        {
            if (end <= start)
            {
                return value < start ? 0.0 : 1.0;
            }
            const double x = std::clamp((value - start) / (end - start), 0.0, 1.0);
            return x * x * (3.0 - 2.0 * x);
        }
    }

    ViewClearanceParameters MakeDefaultViewClearanceParameters()
    {
        return ViewClearanceParameters{};
    }

    ViewClearanceParameters SanitizeViewClearanceParameters(
        const ViewClearanceParameters& parameters)
    {
        const ViewClearanceParameters defaults = MakeDefaultViewClearanceParameters();
        ViewClearanceParameters result = parameters;
        result.bEnabled = parameters.bEnabled && IsFinite(parameters.CameraPosition) &&
                          IsFinite(parameters.TargetPosition);
        result.Strength = SanitizeUnit(parameters.Strength, defaults.Strength);
        result.CameraRadius = SanitizeNonNegative(parameters.CameraRadius, defaults.CameraRadius);
        result.TargetRadius = SanitizeNonNegative(parameters.TargetRadius, defaults.TargetRadius);
        result.FeatherRatio = SanitizeNonNegative(parameters.FeatherRatio, defaults.FeatherRatio);
        result.NearFadeStart = SanitizeNonNegative(parameters.NearFadeStart, defaults.NearFadeStart);
        result.NearFadeEnd = SanitizeNonNegative(parameters.NearFadeEnd, defaults.NearFadeEnd);
        if (result.NearFadeEnd < result.NearFadeStart)
        {
            result.NearFadeStart = defaults.NearFadeStart;
            result.NearFadeEnd = defaults.NearFadeEnd;
        }
        result.MinHeightScale = SanitizeUnit(parameters.MinHeightScale, defaults.MinHeightScale);
        return result;
    }

    ViewClearanceGpuData MakeViewClearanceGpuData(const ViewClearanceParameters& parameters, bool bApply)
    {
        const ViewClearanceParameters sanitized = SanitizeViewClearanceParameters(parameters);
        ViewClearanceGpuData result;
        if (!bApply || !sanitized.bEnabled || sanitized.Strength == 0.0f)
        {
            return result;
        }

        result.CameraRadius[0] = sanitized.CameraPosition.x;
        result.CameraRadius[1] = sanitized.CameraPosition.y;
        result.CameraRadius[2] = sanitized.CameraPosition.z;
        result.CameraRadius[3] = sanitized.CameraRadius;
        result.TargetRadius[0] = sanitized.TargetPosition.x;
        result.TargetRadius[1] = sanitized.TargetPosition.y;
        result.TargetRadius[2] = sanitized.TargetPosition.z;
        result.TargetRadius[3] = sanitized.TargetRadius;
        result.Shape[0] = sanitized.FeatherRatio;
        result.Shape[1] = sanitized.NearFadeStart;
        result.Shape[2] = sanitized.NearFadeEnd;
        result.Shape[3] = sanitized.MinHeightScale;
        result.Control[0] = sanitized.Strength;
        result.Control[1] = 1.0f;
        return result;
    }

    float ComputeViewClearanceVisibility(const ViewClearanceParameters& parameters,
                                         const Math::Vector3& point)
    {
        if (!parameters.bEnabled)
        {
            return 1.0f;
        }

        if (parameters.Strength == 0.0f)
        {
            return 1.0f;
        }

        // 有限な float の差や二乗が途中であふれないよう、距離の計算は double で行う。
        const double dx = static_cast<double>(point.x) - parameters.CameraPosition.x;
        const double dy = static_cast<double>(point.y) - parameters.CameraPosition.y;
        const double dz = static_cast<double>(point.z) - parameters.CameraPosition.z;
        const double axisX = static_cast<double>(parameters.TargetPosition.x) - parameters.CameraPosition.x;
        const double axisY = static_cast<double>(parameters.TargetPosition.y) - parameters.CameraPosition.y;
        const double axisZ = static_cast<double>(parameters.TargetPosition.z) - parameters.CameraPosition.z;
        const double axisLengthSquared = axisX * axisX + axisY * axisY + axisZ * axisZ;

        double clearance = 1.0;
        // 線分の長さが 1 マイクロメートル以下なら切り抜かない。
        if (axisLengthSquared > 1.0e-12)
        {
            const double t = (dx * axisX + dy * axisY + dz * axisZ) / axisLengthSquared;
            if (t >= 0.0 && t <= 1.0)
            {
                const double radius = static_cast<double>(parameters.CameraRadius) +
                    (static_cast<double>(parameters.TargetRadius) - parameters.CameraRadius) * t;
                if (radius > 0.0)
                {
                    const double offsetX = dx - t * axisX;
                    const double offsetY = dy - t * axisY;
                    const double offsetZ = dz - t * axisZ;
                    const double distance = std::sqrt(offsetX * offsetX + offsetY * offsetY +
                                                      offsetZ * offsetZ);
                    const double featherEnd = radius * (1.0 + parameters.FeatherRatio);
                    clearance = SmoothStep(radius, featherEnd, distance);
                }
            }
        }

        const double cameraDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double nearFade = SmoothStep(parameters.NearFadeStart, parameters.NearFadeEnd, cameraDistance);
        const double visibility = std::min(clearance, nearFade);
        return static_cast<float>(1.0 - parameters.Strength * (1.0 - visibility));
    }
}
