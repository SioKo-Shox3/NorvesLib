// 空の遮蔽設定の検証と、遮蔽物の鉛直区間の計算を行う。
#include "Rendering/SkyOcclusion.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float InRangeOrDefault(float value, float minimum, float maximum, float fallback)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum
                ? value : fallback;
        }

        float PositiveInRangeOrDefault(float value, float maximum, float fallback)
        {
            return std::isfinite(value) && value > 0.0f && value <= maximum
                ? value : fallback;
        }

        float ClampOrKeepNaN(float value, float minimum, float maximum)
        {
            return std::isnan(value) ? value : std::clamp(value, minimum, maximum);
        }
    } // namespace

    SkyOcclusionParameters MakeDefaultSkyOcclusionParameters()
    {
        return SkyOcclusionParameters{};
    }

    SkyOcclusionParameters SanitizeSkyOcclusionParameters(
        const SkyOcclusionParameters& parameters)
    {
        const SkyOcclusionParameters defaults = MakeDefaultSkyOcclusionParameters();
        SkyOcclusionParameters result = parameters;
        result.Strength = InRangeOrDefault(parameters.Strength, 0.0f, 4.0f, defaults.Strength);
        result.DiffuseExponent = InRangeOrDefault(
            parameters.DiffuseExponent, 0.1f, 8.0f, defaults.DiffuseExponent);
        result.SpecularStrength = InRangeOrDefault(
            parameters.SpecularStrength, 0.0f, 4.0f, defaults.SpecularStrength);
        result.OccluderBounceColorR = InRangeOrDefault(
            parameters.OccluderBounceColorR, 0.0f, 1.0f, defaults.OccluderBounceColorR);
        result.OccluderBounceColorG = InRangeOrDefault(
            parameters.OccluderBounceColorG, 0.0f, 1.0f, defaults.OccluderBounceColorG);
        result.OccluderBounceColorB = InRangeOrDefault(
            parameters.OccluderBounceColorB, 0.0f, 1.0f, defaults.OccluderBounceColorB);
        result.OccluderBounceIntensity = InRangeOrDefault(
            parameters.OccluderBounceIntensity, 0.0f, 4.0f, defaults.OccluderBounceIntensity);
        result.ReceiverHeightMeters = InRangeOrDefault(
            parameters.ReceiverHeightMeters, 0.0f, 10.0f, defaults.ReceiverHeightMeters);
        result.GroundHeightMeters = InRangeOrDefault(
            parameters.GroundHeightMeters, -1.0e5f, 1.0e5f, defaults.GroundHeightMeters);
        if (parameters.LevelCount < 1u || parameters.LevelCount > 4u)
        {
            result.LevelCount = defaults.LevelCount;
        }
        result.BaseTexelMeters = PositiveInRangeOrDefault(
            parameters.BaseTexelMeters, 64.0f, defaults.BaseTexelMeters);
        if (parameters.Resolution < 32u || parameters.Resolution > 1024u ||
            (parameters.Resolution & (parameters.Resolution - 1u)) != 0u)
        {
            result.Resolution = defaults.Resolution;
        }
        result.MaxHorizonMeters = PositiveInRangeOrDefault(
            parameters.MaxHorizonMeters, 4096.0f, defaults.MaxHorizonMeters);
        result.CenterOverrideX = std::isfinite(parameters.CenterOverrideX)
            ? parameters.CenterOverrideX : defaults.CenterOverrideX;
        result.CenterOverrideZ = std::isfinite(parameters.CenterOverrideZ)
            ? parameters.CenterOverrideZ : defaults.CenterOverrideZ;
        return result;
    }

    bool AreEqualSkyOcclusionParameters(
        const SkyOcclusionParameters& lhs,
        const SkyOcclusionParameters& rhs)
    {
        return lhs.bEnabled == rhs.bEnabled &&
            lhs.Strength == rhs.Strength &&
            lhs.DiffuseExponent == rhs.DiffuseExponent &&
            lhs.SpecularStrength == rhs.SpecularStrength &&
            lhs.OccluderBounceColorR == rhs.OccluderBounceColorR &&
            lhs.OccluderBounceColorG == rhs.OccluderBounceColorG &&
            lhs.OccluderBounceColorB == rhs.OccluderBounceColorB &&
            lhs.OccluderBounceIntensity == rhs.OccluderBounceIntensity &&
            lhs.ReceiverHeightMeters == rhs.ReceiverHeightMeters &&
            lhs.GroundHeightMeters == rhs.GroundHeightMeters &&
            lhs.LevelCount == rhs.LevelCount &&
            lhs.BaseTexelMeters == rhs.BaseTexelMeters &&
            lhs.Resolution == rhs.Resolution &&
            lhs.MaxHorizonMeters == rhs.MaxHorizonMeters &&
            lhs.bUseCenterOverride == rhs.bUseCenterOverride &&
            lhs.CenterOverrideX == rhs.CenterOverrideX &&
            lhs.CenterOverrideZ == rhs.CenterOverrideZ;
    }

    SkyOccluderProxy MakeDefaultSkyOccluderProxy()
    {
        return SkyOccluderProxy{};
    }

    bool IsValidSkyOccluderProxy(const SkyOccluderProxy& proxy)
    {
        return std::isfinite(proxy.CenterX) && std::isfinite(proxy.CenterY) &&
            std::isfinite(proxy.CenterZ) && std::isfinite(proxy.Yaw) &&
            std::isfinite(proxy.HalfExtentX) && proxy.HalfExtentX > 0.0f &&
            std::isfinite(proxy.HalfExtentY) && proxy.HalfExtentY > 0.0f &&
            std::isfinite(proxy.HalfExtentZ) && proxy.HalfExtentZ > 0.0f &&
            std::isfinite(proxy.BaseOffset) && std::isfinite(proxy.Opacity);
    }

    SkyOccluderProxy SanitizeSkyOccluderProxy(const SkyOccluderProxy& proxy)
    {
        const SkyOccluderProxy defaults = MakeDefaultSkyOccluderProxy();
        SkyOccluderProxy result = proxy;
        result.CenterX = std::isfinite(proxy.CenterX) ? proxy.CenterX : defaults.CenterX;
        result.CenterY = std::isfinite(proxy.CenterY) ? proxy.CenterY : defaults.CenterY;
        result.CenterZ = std::isfinite(proxy.CenterZ) ? proxy.CenterZ : defaults.CenterZ;
        result.Yaw = std::isfinite(proxy.Yaw) ? proxy.Yaw : defaults.Yaw;
        result.HalfExtentX = ClampOrKeepNaN(proxy.HalfExtentX, 0.01f, 1.0e4f);
        result.HalfExtentY = ClampOrKeepNaN(proxy.HalfExtentY, 0.01f, 1.0e4f);
        result.HalfExtentZ = ClampOrKeepNaN(proxy.HalfExtentZ, 0.01f, 1.0e4f);
        result.Opacity = ClampOrKeepNaN(proxy.Opacity, 0.0f, 1.0f);
        if (std::isfinite(result.HalfExtentY))
        {
            result.BaseOffset = ClampOrKeepNaN(proxy.BaseOffset, 0.0f, 2.0f * result.HalfExtentY);
        }
        result.Flags &= SkyOccluderKnownFlags;
        for (std::uint32_t& reserved : result.Reserved)
        {
            reserved = 0u;
        }
        return result;
    }

    bool AreEqualSkyOccluderProxy(const SkyOccluderProxy& lhs, const SkyOccluderProxy& rhs)
    {
        if (lhs.CenterX != rhs.CenterX || lhs.CenterY != rhs.CenterY ||
            lhs.CenterZ != rhs.CenterZ || lhs.Yaw != rhs.Yaw ||
            lhs.HalfExtentX != rhs.HalfExtentX || lhs.HalfExtentY != rhs.HalfExtentY ||
            lhs.HalfExtentZ != rhs.HalfExtentZ || lhs.BaseOffset != rhs.BaseOffset ||
            lhs.Opacity != rhs.Opacity || lhs.Flags != rhs.Flags)
        {
            return false;
        }
        for (std::uint32_t index = 0u; index < 6u; ++index)
        {
            if (lhs.Reserved[index] != rhs.Reserved[index])
            {
                return false;
            }
        }
        return true;
    }

    bool EvaluateSkyOccluderColumn(
        const SkyOccluderProxy& proxy,
        float worldX,
        float worldZ,
        float& outBottomY,
        float& outTopY)
    {
        if (!IsValidSkyOccluderProxy(proxy) || !std::isfinite(worldX) || !std::isfinite(worldZ))
        {
            return false;
        }

        const float offsetX = worldX - proxy.CenterX;
        const float offsetZ = worldZ - proxy.CenterZ;
        const float cosine = std::cos(proxy.Yaw);
        const float sine = std::sin(proxy.Yaw);
        const float localX = cosine * offsetX - sine * offsetZ;
        const float localZ = sine * offsetX + cosine * offsetZ;
        if (!std::isfinite(localX) || !std::isfinite(localZ))
        {
            return false;
        }

        float bottom = proxy.CenterY - proxy.HalfExtentY;
        float top = proxy.CenterY + proxy.HalfExtentY;
        if (GetSkyOccluderShape(proxy) == SkyOccluderShape::Ellipsoid)
        {
            const float normalizedX = localX / proxy.HalfExtentX;
            const float normalizedZ = localZ / proxy.HalfExtentZ;
            const float q = 1.0f - normalizedX * normalizedX - normalizedZ * normalizedZ;
            if (q < 0.0f)
            {
                return false;
            }
            const float verticalRadius = proxy.HalfExtentY * std::sqrt(q);
            bottom = proxy.CenterY - verticalRadius;
            top = proxy.CenterY + verticalRadius;
        }
        else if (std::abs(localX) > proxy.HalfExtentX || std::abs(localZ) > proxy.HalfExtentZ)
        {
            return false;
        }

        if (proxy.BaseOffset > 0.0f)
        {
            bottom = std::max(bottom, proxy.CenterY - proxy.HalfExtentY + proxy.BaseOffset);
        }
        if (bottom > top || !std::isfinite(bottom) || !std::isfinite(top))
        {
            return false;
        }
        outBottomY = bottom;
        outTopY = top;
        return true;
    }
} // namespace NorvesLib::Core::Rendering
