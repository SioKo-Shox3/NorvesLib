// 空の遮蔽物の写し込みと、地平線からの CPU 積分を行う。
#include "Rendering/SkyOcclusionReference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        double ClampUnit(double value)
        {
            return std::clamp(value, -1.0, 1.0);
        }
    } // namespace

    SkyOcclusionReferenceGrid::SkyOcclusionReferenceGrid(
        float originX, float originZ, float texelMeters, std::uint32_t resolution)
        : OriginX(originX), OriginZ(originZ), TexelMeters(texelMeters), Resolution(resolution),
          m_Ground(static_cast<std::size_t>(resolution) * resolution, 0.0f),
          m_CanopyBottom(static_cast<std::size_t>(resolution) * resolution, 0.0f),
          m_CanopyTop(static_cast<std::size_t>(resolution) * resolution, 0.0f),
          m_CanopyOpacity(static_cast<std::size_t>(resolution) * resolution, 0.0f)
    {
    }

    SkyOcclusionReferenceColumnRef SkyOcclusionReferenceGrid::At(
        std::uint32_t ix, std::uint32_t iz)
    {
        const std::size_t index = static_cast<std::size_t>(iz) * Resolution + ix;
        return {m_Ground[index], m_CanopyBottom[index], m_CanopyTop[index],
                m_CanopyOpacity[index]};
    }

    SkyOcclusionReferenceColumn SkyOcclusionReferenceGrid::At(
        std::uint32_t ix, std::uint32_t iz) const
    {
        const std::size_t index = static_cast<std::size_t>(iz) * Resolution + ix;
        return {m_Ground[index], m_CanopyBottom[index], m_CanopyTop[index],
                m_CanopyOpacity[index]};
    }

    SkyOcclusionReferenceColumn SkyOcclusionReferenceGrid::Sample(float worldX, float worldZ) const
    {
        const float lastIndex = static_cast<float>(Resolution - 1u);
        const auto ix = static_cast<std::uint32_t>(std::clamp(
            std::floor((worldX - OriginX) / TexelMeters), 0.0f, lastIndex));
        const auto iz = static_cast<std::uint32_t>(std::clamp(
            std::floor((worldZ - OriginZ) / TexelMeters), 0.0f, lastIndex));
        return At(ix, iz);
    }

    SkyOcclusionReferenceGrid BuildSkyOcclusionReferenceGrid(
        const SkyOcclusionParameters& parameters,
        Container::Span<const SkyOccluderProxy> occluders,
        float originX, float originZ, float texelMeters, std::uint32_t resolution)
    {
        SkyOcclusionReferenceGrid grid(originX, originZ, texelMeters, resolution);
        Container::VariableArray<const SkyOccluderProxy*> ordered;
        ordered.reserve(occluders.size());
        for (const SkyOccluderProxy& proxy : occluders)
        {
            if (IsValidSkyOccluderProxy(proxy))
            {
                ordered.push_back(&proxy);
            }
        }
        // 積の丸めが入力順に左右されないよう、不透明度の順に並べる。
        std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs)
        {
            return std::clamp(lhs->Opacity, 0.0f, 1.0f) <
                std::clamp(rhs->Opacity, 0.0f, 1.0f);
        });

        for (std::uint32_t iz = 0; iz < resolution; ++iz)
        {
            for (std::uint32_t ix = 0; ix < resolution; ++ix)
            {
                const float worldX = originX + (static_cast<float>(ix) + 0.5f) * texelMeters;
                const float worldZ = originZ + (static_cast<float>(iz) + 0.5f) * texelMeters;
                float ground = parameters.GroundHeightMeters;
                float bottom = ground;
                float top = ground;
                double transmittance = 1.0;
                bool bHasCanopy = false;
                for (const SkyOccluderProxy* proxy : ordered)
                {
                    float proxyBottom = 0.0f;
                    float proxyTop = 0.0f;
                    if (!EvaluateSkyOccluderColumn(*proxy, worldX, worldZ, proxyBottom, proxyTop))
                    {
                        continue;
                    }
                    if (GetSkyOccluderClass(*proxy) == SkyOccluderClass::Solid)
                    {
                        ground = std::max(ground, proxyTop);
                    }
                    else
                    {
                        bottom = bHasCanopy ? std::min(bottom, proxyBottom) : proxyBottom;
                        top = bHasCanopy ? std::max(top, proxyTop) : proxyTop;
                        bHasCanopy = true;
                        transmittance *= 1.0 - std::clamp(static_cast<double>(proxy->Opacity), 0.0, 1.0);
                    }
                }
                const SkyOcclusionReferenceColumnRef column = grid.At(ix, iz);
                column.Ground = ground;
                column.CanopyBottom = bHasCanopy ? std::max(bottom, ground) : ground;
                column.CanopyTop = bHasCanopy ? top : ground;
                column.CanopyOpacity = static_cast<float>(1.0 - transmittance);
            }
        }
        return grid;
    }

    SkyOcclusionL1 IntegrateSkyOcclusion(
        const SkyOcclusionReferenceGrid& grid, const SkyOcclusionParameters& parameters,
        float worldX, float worldZ)
    {
        const double y0 = grid.Sample(worldX, worldZ).Ground + parameters.ReceiverHeightMeters;
        double sumB = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        double sumZ = 0.0;
        for (std::uint32_t azimuth = 0; azimuth < SkyOcclusionAzimuthCount; ++azimuth)
        {
            const double phi = (static_cast<double>(azimuth) + 0.5) *
                (2.0 * kPi / SkyOcclusionAzimuthCount);
            const double directionX = std::cos(phi);
            const double directionZ = std::sin(phi);
            for (std::uint32_t elevation = 0; elevation < SkyOcclusionElevationCount; ++elevation)
            {
                const double sine = (static_cast<double>(elevation) + 0.5) /
                    SkyOcclusionElevationCount;
                const double cosine = std::sqrt(1.0 - sine * sine);
                const double tangent = sine / cosine;
                double start = 0.0;
                double nextEnd = SkyOcclusionHorizonFirstStepMeters;
                double opticalDepth = 0.0;
                bool bOpaque = false;
                for (std::uint32_t step = 0;
                     step < SkyOcclusionHorizonMaxStepCount && start < parameters.MaxHorizonMeters;
                     ++step)
                {
                    const double end = std::min(nextEnd, static_cast<double>(parameters.MaxHorizonMeters));
                    const double midpoint = (start + end) * 0.5;
                    const SkyOcclusionReferenceColumn column = grid.Sample(
                        static_cast<float>(worldX + midpoint * directionX),
                        static_cast<float>(worldZ + midpoint * directionZ));
                    if (y0 + midpoint * tangent < column.Ground)
                    {
                        bOpaque = true;
                        break;
                    }
                    if (column.CanopyOpacity > 0.0f && column.CanopyTop > column.CanopyBottom)
                    {
                        const double overlap = std::max(0.0,
                            std::min(y0 + end * tangent, static_cast<double>(column.CanopyTop)) -
                            std::max(y0 + start * tangent, static_cast<double>(column.CanopyBottom)));
                        if (overlap > 0.0)
                        {
                            if (column.CanopyOpacity >= 0.9999f)
                            {
                                bOpaque = true;
                                break;
                            }
                            const double sigma = -std::log(1.0 - column.CanopyOpacity) /
                                std::max(static_cast<double>(column.CanopyTop - column.CanopyBottom), 0.5);
                            opticalDepth += sigma * (overlap / tangent) / cosine;
                        }
                    }
                    start = end;
                    nextEnd *= SkyOcclusionHorizonStepRatio;
                }
                const double b = bOpaque ? 1.0 : 1.0 - std::exp(-opticalDepth);
                sumB += b;
                sumX += b * cosine * directionX;
                sumY += b * sine;
                sumZ += b * cosine * directionZ;
            }
        }
        const double weight = 1.0 /
            (2.0 * SkyOcclusionAzimuthCount * SkyOcclusionElevationCount);
        return {static_cast<float>(weight * sumB), static_cast<float>(3.0 * weight * sumX),
                static_cast<float>(3.0 * weight * sumY), static_cast<float>(3.0 * weight * sumZ)};
    }

    float EvaluateSkyOcclusionB(const SkyOcclusionL1& coefficients, float nx, float ny, float nz)
    {
        return coefficients.B0 + (2.0f / 3.0f) *
            (coefficients.Cx * nx + coefficients.Cy * ny + coefficients.Cz * nz);
    }

    float EvaluateSkyOcclusionVisibility(
        const SkyOcclusionL1& coefficients, float nx, float ny, float nz)
    {
        return std::clamp(1.0f - EvaluateSkyOcclusionB(coefficients, nx, ny, nz), 0.0f, 1.0f);
    }

    Math::Vector3 EvaluateSkyOcclusionBentNormal(const SkyOcclusionL1& coefficients)
    {
        const Math::Vector3 bent(-coefficients.Cx, 0.75f - coefficients.Cy, -coefficients.Cz);
        const float length = std::sqrt(bent.x * bent.x + bent.y * bent.y + bent.z * bent.z);
        return length < 1.0e-4f ? Math::Vector3(0.0f, 1.0f, 0.0f) : bent / length;
    }

    float EvaluateSkySpecularOcclusionCone(
        const Math::Vector3& visibleAxis, float visibility,
        const Math::Vector3& reflection, float roughness)
    {
        const double cosineAlpha = std::sqrt(
            1.0 - std::clamp(static_cast<double>(visibility), 0.0, 1.0));
        const double r = std::clamp(static_cast<double>(roughness), 0.0, 1.0);
        const double ggxAlpha = r * r;
        const double ggxAlphaSquared = ggxAlpha * ggxAlpha;
        const double cosineDelta = ClampUnit(static_cast<double>(visibleAxis.x) * reflection.x +
            static_cast<double>(visibleAxis.y) * reflection.y +
            static_cast<double>(visibleAxis.z) * reflection.z);
        const double sineDelta = std::sqrt(std::max(0.0, 1.0 - cosineDelta * cosineDelta));
        double weightedVisibility = 0.0;
        double totalWeight = 0.0;
        for (std::uint32_t index = 0; index < SkySpecularOcclusionLobeSampleCount; ++index)
        {
            const double xi = (static_cast<double>(index) + 0.5) /
                SkySpecularOcclusionLobeSampleCount;
            const double angle = 2.0 * std::atan(ggxAlpha * std::sqrt(xi / (1.0 - xi)));
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            const double positiveCosine = std::max(cosine, 0.0);
            const double smithDenominator = positiveCosine + std::sqrt(
                ggxAlphaSquared + (1.0 - ggxAlphaSquared) * positiveCosine * positiveCosine);
            const double weight = 2.0 * positiveCosine / std::max(smithDenominator, 1.0e-6);
            const double x = (cosineAlpha - cosine * cosineDelta) /
                std::max(sine * sineDelta, 1.0e-6);
            const double fraction = std::acos(ClampUnit(x)) / kPi;
            weightedVisibility += weight * fraction;
            totalWeight += weight;
        }
        return static_cast<float>(std::clamp(
            weightedVisibility / std::max(totalWeight, 1.0e-6), 0.0, 1.0));
    }

    float EvaluateSkySpecularOcclusion(
        const SkyOcclusionL1& coefficients, const Math::Vector3& reflection, float roughness)
    {
        const Math::Vector3 bent = EvaluateSkyOcclusionBentNormal(coefficients);
        const float visibility = EvaluateSkyOcclusionVisibility(coefficients, bent.x, bent.y, bent.z);
        return EvaluateSkySpecularOcclusionCone(bent, visibility, reflection, roughness);
    }
} // namespace NorvesLib::Core::Rendering
