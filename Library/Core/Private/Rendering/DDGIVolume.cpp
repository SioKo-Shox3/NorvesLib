#include "Rendering/DDGIVolume.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float kGoldenAngleRadians = 2.39996322972865332f;

        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool TryComputeProbeCount(
            const DDGIVolumeParameters& parameters,
            std::uint32_t& outCount)
        {
            outCount = 0u;
            if (parameters.ProbeCountX == 0u ||
                parameters.ProbeCountY == 0u ||
                parameters.ProbeCountZ == 0u)
            {
                return false;
            }

            const std::uint64_t xyCount =
                static_cast<std::uint64_t>(parameters.ProbeCountX) *
                static_cast<std::uint64_t>(parameters.ProbeCountY);
            if (xyCount > DDGIMaxProbeCount)
            {
                return false;
            }

            const std::uint64_t totalCount =
                xyCount * static_cast<std::uint64_t>(parameters.ProbeCountZ);
            if (totalCount == 0u || totalCount > DDGIMaxProbeCount)
            {
                return false;
            }

            outCount = static_cast<std::uint32_t>(totalCount);
            return true;
        }

        bool IsVolumeGeometryValid(const DDGIVolumeParameters& parameters)
        {
            if (!IsFiniteVector(parameters.Origin) ||
                !IsFiniteVector(parameters.ProbeSpacing) ||
                parameters.ProbeSpacing.x <= 0.0f ||
                parameters.ProbeSpacing.y <= 0.0f ||
                parameters.ProbeSpacing.z <= 0.0f)
            {
                return false;
            }

            std::uint32_t probeCount = 0u;
            if (!TryComputeProbeCount(parameters, probeCount))
            {
                return false;
            }

            const float lastX = parameters.Origin.x + parameters.ProbeSpacing.x *
                static_cast<float>(parameters.ProbeCountX - 1u);
            const float lastY = parameters.Origin.y + parameters.ProbeSpacing.y *
                static_cast<float>(parameters.ProbeCountY - 1u);
            const float lastZ = parameters.Origin.z + parameters.ProbeSpacing.z *
                static_cast<float>(parameters.ProbeCountZ - 1u);
            return std::isfinite(lastX) && std::isfinite(lastY) && std::isfinite(lastZ);
        }

        float SignNotZero(float value)
        {
            return value < 0.0f ? -1.0f : 1.0f;
        }

        bool IsFiniteVector(const Math::Vector2& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }
    } // namespace

    DDGIVolumeParameters MakeDefaultDDGIVolumeParameters()
    {
        return DDGIVolumeParameters{};
    }

    bool IsDDGIVolumeValid(const DDGIVolumeParameters& parameters)
    {
        return parameters.bEnabled && IsVolumeGeometryValid(parameters);
    }

    DDGIVolumeParameters SanitizeDDGIVolumeParameters(
        const DDGIVolumeParameters& parameters)
    {
        if (!IsVolumeGeometryValid(parameters))
        {
            return MakeDefaultDDGIVolumeParameters();
        }
        return parameters;
    }

    std::uint32_t GetDDGIProbeCount(const DDGIVolumeParameters& parameters)
    {
        if (!IsDDGIVolumeValid(parameters))
        {
            return 0u;
        }

        std::uint32_t probeCount = 0u;
        TryComputeProbeCount(parameters, probeCount);
        return probeCount;
    }

    bool TryGetDDGIProbeIndex(
        const DDGIVolumeParameters& parameters,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z,
        std::uint32_t& outIndex)
    {
        outIndex = 0u;
        if (!IsDDGIVolumeValid(parameters) ||
            x >= parameters.ProbeCountX ||
            y >= parameters.ProbeCountY ||
            z >= parameters.ProbeCountZ)
        {
            return false;
        }

        const std::uint64_t planeSize =
            static_cast<std::uint64_t>(parameters.ProbeCountX) *
            static_cast<std::uint64_t>(parameters.ProbeCountY);
        const std::uint64_t index = static_cast<std::uint64_t>(x) +
            static_cast<std::uint64_t>(y) * parameters.ProbeCountX +
            static_cast<std::uint64_t>(z) * planeSize;
        if (index >= GetDDGIProbeCount(parameters))
        {
            return false;
        }

        outIndex = static_cast<std::uint32_t>(index);
        return true;
    }

    bool TryGetDDGIProbeCoordinates(
        const DDGIVolumeParameters& parameters,
        std::uint32_t index,
        std::uint32_t& outX,
        std::uint32_t& outY,
        std::uint32_t& outZ)
    {
        outX = 0u;
        outY = 0u;
        outZ = 0u;
        if (!IsDDGIVolumeValid(parameters))
        {
            return false;
        }

        const std::uint32_t probeCount = GetDDGIProbeCount(parameters);
        if (index >= probeCount)
        {
            return false;
        }

        const std::uint32_t planeSize = parameters.ProbeCountX * parameters.ProbeCountY;
        outX = index % parameters.ProbeCountX;
        outY = (index / parameters.ProbeCountX) % parameters.ProbeCountY;
        outZ = index / planeSize;
        return true;
    }

    bool EncodeDDGIOctahedralDirection(
        const Math::Vector3& direction,
        Math::Vector2& outUv)
    {
        outUv = Math::Vector2(0.5f, 0.5f);
        if (!IsFiniteVector(direction))
        {
            return false;
        }

        const float scale = std::max(std::abs(direction.x),
                                     std::max(std::abs(direction.y), std::abs(direction.z)));
        if (scale <= 0.0f)
        {
            return false;
        }

        float x = direction.x / scale;
        float y = direction.y / scale;
        const float z = direction.z / scale;
        const float denominator = std::abs(x) + std::abs(y) + std::abs(z);
        if (!std::isfinite(denominator) || denominator <= 0.0f)
        {
            return false;
        }

        x /= denominator;
        y /= denominator;
        if (z < 0.0f)
        {
            const float oldX = x;
            x = (1.0f - std::abs(y)) * SignNotZero(oldX);
            y = (1.0f - std::abs(oldX)) * SignNotZero(y);
        }

        outUv = Math::Vector2(x * 0.5f + 0.5f, y * 0.5f + 0.5f);
        if (!IsFiniteVector(outUv))
        {
            outUv = Math::Vector2(0.5f, 0.5f);
            return false;
        }
        return true;
    }

    bool DecodeDDGIOctahedralDirection(
        const Math::Vector2& uv,
        Math::Vector3& outDirection)
    {
        outDirection = Math::Vector3(0.0f, 0.0f, 1.0f);
        if (!IsFiniteVector(uv) ||
            uv.x < 0.0f || uv.x > 1.0f ||
            uv.y < 0.0f || uv.y > 1.0f)
        {
            return false;
        }

        float x = uv.x * 2.0f - 1.0f;
        float y = uv.y * 2.0f - 1.0f;
        float z = 1.0f - std::abs(x) - std::abs(y);
        if (z < 0.0f)
        {
            const float oldX = x;
            x = (1.0f - std::abs(y)) * SignNotZero(oldX);
            y = (1.0f - std::abs(oldX)) * SignNotZero(y);
        }

        const float length = std::sqrt(x * x + y * y + z * z);
        if (!std::isfinite(length) || length <= 0.0f)
        {
            return false;
        }

        outDirection = Math::Vector3(x / length, y / length, z / length);
        return IsFiniteVector(outDirection);
    }

    bool TryGetDDGIProbeRayDirection(
        std::uint32_t index,
        Math::Vector3& outDirection)
    {
        outDirection = Math::Vector3(0.0f, 0.0f, 1.0f);
        if (index >= DDGIProbeRayDirectionCount)
        {
            return false;
        }

        const float sample = (static_cast<float>(index) + 0.5f) /
            static_cast<float>(DDGIProbeRayDirectionCount);
        const float z = 1.0f - 2.0f * sample;
        const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float angle = kGoldenAngleRadians * static_cast<float>(index);
        outDirection = Math::Vector3(radius * std::cos(angle),
                                     radius * std::sin(angle),
                                     z);
        if (!IsFiniteVector(outDirection))
        {
            outDirection = Math::Vector3(0.0f, 0.0f, 1.0f);
            return false;
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering
