// 空の遮蔽設定の検証と、遮蔽物の鉛直区間の計算を行う。
#include "Rendering/SkyOcclusion.h"

#include "Rendering/Terrain/TerrainHeightField.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

    bool IsValidSkyOcclusionFieldLayout(const SkyOcclusionFieldLayout& layout)
    {
        return std::isfinite(layout.OriginX) && std::isfinite(layout.OriginZ) &&
            std::isfinite(layout.TexelMeters) && layout.TexelMeters > 0.0f &&
            layout.Width >= 1u && layout.Width <= 8192u &&
            layout.Height >= 1u && layout.Height <= 8192u &&
            layout.Reserved[0] == 0u && layout.Reserved[1] == 0u && layout.Reserved[2] == 0u;
    }

    void ComputeSkyOcclusionFieldWorldToUv(
        const SkyOcclusionFieldLayout& layout, float outScaleOffset[4])
    {
        if (!IsValidSkyOcclusionFieldLayout(layout))
        {
            for (std::size_t index = 0u; index < 4u; ++index)
            {
                outScaleOffset[index] = 0.0f;
            }
            return;
        }
        const double scaleX = 1.0 / (static_cast<double>(layout.Width) * layout.TexelMeters);
        const double scaleZ = 1.0 / (static_cast<double>(layout.Height) * layout.TexelMeters);
        outScaleOffset[0] = static_cast<float>(scaleX);
        outScaleOffset[1] = static_cast<float>(scaleZ);
        outScaleOffset[2] = static_cast<float>(-layout.OriginX * scaleX);
        outScaleOffset[3] = static_cast<float>(-layout.OriginZ * scaleZ);
    }

    float SampleSkyOcclusionField(const SkyOcclusionFieldLayout& layout,
        Container::Span<const float> values, float x, float z, float fallback)
    {
        if (!IsValidSkyOcclusionFieldLayout(layout) || !std::isfinite(x) || !std::isfinite(z) ||
            values.size() != static_cast<std::size_t>(layout.Width) * layout.Height)
        {
            return fallback;
        }
        const double texelX = (static_cast<double>(x) - layout.OriginX) / layout.TexelMeters;
        const double texelZ = (static_cast<double>(z) - layout.OriginZ) / layout.TexelMeters;
        if (texelX < 0.0 || texelX > layout.Width || texelZ < 0.0 || texelZ > layout.Height)
        {
            return fallback;
        }
        // テクセル中心を整数へ移し、端の延長を補間前に適用する。
        const double sampleX = std::clamp(texelX - 0.5, 0.0, static_cast<double>(layout.Width - 1u));
        const double sampleZ = std::clamp(texelZ - 0.5, 0.0, static_cast<double>(layout.Height - 1u));
        const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(sampleX));
        const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(sampleZ));
        const std::uint32_t x1 = std::min(x0 + 1u, layout.Width - 1u);
        const std::uint32_t z1 = std::min(z0 + 1u, layout.Height - 1u);
        const float v00 = values[static_cast<std::size_t>(z0) * layout.Width + x0];
        const float v10 = values[static_cast<std::size_t>(z0) * layout.Width + x1];
        const float v01 = values[static_cast<std::size_t>(z1) * layout.Width + x0];
        const float v11 = values[static_cast<std::size_t>(z1) * layout.Width + x1];
        if (!std::isfinite(v00) || !std::isfinite(v10) || !std::isfinite(v01) || !std::isfinite(v11))
        {
            return fallback;
        }
        const double fractionX = sampleX - x0;
        const double fractionZ = sampleZ - z0;
        const double lower = (1.0 - fractionX) * v00 + fractionX * v10;
        const double upper = (1.0 - fractionX) * v01 + fractionX * v11;
        return static_cast<float>((1.0 - fractionZ) * lower + fractionZ * upper);
    }

    SkyOcclusionUndergrowthTexel ComputeSkyOcclusionUndergrowthLayer(
        float density, float grassHeightMeters)
    {
        const float d = std::isfinite(density) ? std::clamp(density, 0.0f, 1.0f) : 0.0f;
        const float h = std::isfinite(grassHeightMeters) ? std::max(grassHeightMeters, 0.0f) : 0.0f;
        if (d <= 0.0f || h < SkyOcclusionUndergrowthMinTopMeters)
        {
            return {};
        }
        return {h, d * SkyOcclusionUndergrowthMaxKappa};
    }

    float ApplySkyOcclusionTrample(float kappa, float flatten)
    {
        const float amount = std::isfinite(flatten) ? std::clamp(flatten, 0.0f, 1.0f) : 0.0f;
        return kappa * (1.0f - amount);
    }

    float ComputeSkyOcclusionUndergrowthExtinction(float topMeters, float kappa)
    {
        if (kappa <= 0.0f || topMeters < SkyOcclusionUndergrowthMinTopMeters)
        {
            return 0.0f;
        }
        // GPU の写し込みもこの消散係数の式を使う。
        return -std::log(1.0f - std::min(kappa, 0.999f)) / std::max(topMeters, 0.05f);
    }

    void ComputeSkyOcclusionSkyWeightCoefficients(float cloudCover,
        float zenithToHorizonRatio, float& outConstant, float& outSlope)
    {
        const float c = std::isfinite(cloudCover) ? std::clamp(cloudCover, 0.0f, 1.0f) : 0.0f;
        const float r = std::isfinite(zenithToHorizonRatio)
            ? std::clamp(zenithToHorizonRatio, 1.0f, 64.0f) : 3.0f;
        const float denominator = 2.0f * r + 1.0f;
        outConstant = (1.0f - c) + c * 3.0f / denominator;
        outSlope = c * 3.0f * (r - 1.0f) / denominator;
    }

    float EvaluateSkyOcclusionSkyWeight(float constant, float slope, float sinElevation)
    {
        return constant + slope * sinElevation;
    }

    bool MakeSkyOccluderFromInstance(const SkyOccluderSpeciesDesc& desc,
        float x, float groundY, float z, float yaw, float scale, SkyOccluderProxy& outProxy)
    {
        if (!desc.bEnabled || !std::isfinite(desc.RadiusMeters) || desc.RadiusMeters <= 0.0f ||
            !std::isfinite(desc.HeightMeters) || desc.HeightMeters <= 0.0f ||
            !std::isfinite(scale) || scale <= 0.0f ||
            !std::isfinite(x) || !std::isfinite(groundY) || !std::isfinite(z) || !std::isfinite(yaw))
        {
            return false;
        }
        SkyOccluderProxy proxy{};
        proxy.HalfExtentX = desc.RadiusMeters * scale;
        proxy.HalfExtentZ = proxy.HalfExtentX;
        proxy.HalfExtentY = 0.5f * desc.HeightMeters * scale;
        proxy.CenterX = x;
        proxy.CenterY = groundY + proxy.HalfExtentY;
        proxy.CenterZ = z;
        proxy.Yaw = yaw;
        proxy.BaseOffset = std::clamp(desc.BaseRatio, 0.0f, 0.95f) * desc.HeightMeters * scale;
        proxy.Opacity = desc.Class == SkyOccluderClass::Canopy ? std::clamp(desc.Kappa, 0.0f, 1.0f) : 1.0f;
        SetSkyOccluderShape(proxy, desc.Shape);
        SetSkyOccluderClass(proxy, desc.Class);
        proxy = SanitizeSkyOccluderProxy(proxy);
        if (!IsValidSkyOccluderProxy(proxy))
        {
            return false;
        }
        outProxy = proxy;
        return true;
    }

    void GetSkyOccluderHorizontalBounds(const SkyOccluderProxy& proxy,
        double& outMinX, double& outMinZ, double& outMaxX, double& outMaxZ)
    {
        const double cosine = std::abs(std::cos(static_cast<double>(proxy.Yaw)));
        const double sine = std::abs(std::sin(static_cast<double>(proxy.Yaw)));
        // GPU 側の三角関数の丸め差を含め、ビンから交差物を落とさない。
        const double extentX = (cosine * proxy.HalfExtentX + sine * proxy.HalfExtentZ) * 1.000001;
        const double extentZ = (sine * proxy.HalfExtentX + cosine * proxy.HalfExtentZ) * 1.000001;
        outMinX = proxy.CenterX - extentX;
        outMinZ = proxy.CenterZ - extentZ;
        outMaxX = proxy.CenterX + extentX;
        outMaxZ = proxy.CenterZ + extentZ;
    }

    bool IsValidSkyOccluderChunkRanges(
        Container::Span<const SkyOccluderChunkRange> ranges, std::size_t occluderCount)
    {
        std::size_t end = 0u;
        std::uint64_t previousKey = 0u;
        for (std::size_t index = 0u; index < ranges.size(); ++index)
        {
            const SkyOccluderChunkRange& range = ranges[index];
            if ((index > 0u && range.ChunkKey <= previousKey) || range.Offset != end ||
                range.Count == 0u || end > occluderCount || range.Count > occluderCount - end ||
                !std::isfinite(range.MinX) || !std::isfinite(range.MinZ) ||
                !std::isfinite(range.MaxX) || !std::isfinite(range.MaxZ) ||
                range.MinX > range.MaxX || range.MinZ > range.MaxZ)
            {
                return false;
            }
            end += range.Count;
            previousKey = range.ChunkKey;
        }
        return end == occluderCount;
    }

    bool BuildSkyOcclusionGroundHeights(const Terrain::TerrainHeightField& field,
        std::uint32_t maxResolution, float fallbackHeight, SkyOcclusionFieldLayout& outLayout,
        Container::VariableArray<float>& outHeights)
    {
        const Terrain::TerrainGridLayout grid = field.GetGridLayout();
        if (grid.Columns < 2u || grid.Rows < 2u ||
            !std::isfinite(grid.FirstX) || !std::isfinite(grid.FirstZ) ||
            !std::isfinite(grid.Spacing) || grid.Spacing <= 0.0)
        {
            return false;
        }
        const double extentX = (grid.Columns - 1u) * grid.Spacing;
        const double extentZ = (grid.Rows - 1u) * grid.Spacing;
        if (!std::isfinite(extentX) || !std::isfinite(extentZ) || extentX <= 0.0 || extentZ <= 0.0)
        {
            return false;
        }
        maxResolution = std::clamp(maxResolution, 1u, 4096u);
        const double texelMeters = std::max(grid.Spacing, std::max(extentX, extentZ) / maxResolution);
        const double maxFloat = std::numeric_limits<float>::max();
        if (std::abs(grid.FirstX) > maxFloat || std::abs(grid.FirstZ) > maxFloat || texelMeters > maxFloat)
        {
            return false;
        }
        SkyOcclusionFieldLayout layout{};
        layout.OriginX = static_cast<float>(grid.FirstX);
        layout.OriginZ = static_cast<float>(grid.FirstZ);
        layout.TexelMeters = static_cast<float>(texelMeters);
        layout.Width = static_cast<std::uint32_t>(std::max(1.0, std::ceil(extentX / texelMeters)));
        layout.Height = static_cast<std::uint32_t>(std::max(1.0, std::ceil(extentZ / texelMeters)));
        if (!IsValidSkyOcclusionFieldLayout(layout))
        {
            return false;
        }
        const float fallback = std::isfinite(fallbackHeight) ? fallbackHeight : 0.0f;
        Container::VariableArray<float> heights(static_cast<std::size_t>(layout.Width) * layout.Height);
        for (std::uint32_t j = 0u; j < layout.Height; ++j)
        {
            const float z = layout.OriginZ + (static_cast<float>(j) + 0.5f) * layout.TexelMeters;
            for (std::uint32_t i = 0u; i < layout.Width; ++i)
            {
                const float x = layout.OriginX + (static_cast<float>(i) + 0.5f) * layout.TexelMeters;
                const float height = field.SampleHeight(x, z);
                heights[static_cast<std::size_t>(j) * layout.Width + i] = std::isfinite(height) ? height : fallback;
            }
        }
        outHeights.swap(heights);
        outLayout = layout;
        return true;
    }
} // namespace NorvesLib::Core::Rendering
