#include "Rendering/SkyAtmosphere.h"

#include "Math/MathTypes.h"
#include "Math/VectorUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegreesToRadians = kPi / 180.0f;
        constexpr float kFp16Max = 65504.0f;

        constexpr float kRayleighScatteringR = 5.802e-6f;
        constexpr float kRayleighScatteringG = 13.558e-6f;
        constexpr float kRayleighScatteringB = 33.100e-6f;
        // 晴天の典型的なエアロゾル。尺度高さ1.2 kmで550 nmの散乱の光学的深さ約0.12、単散乱アルベド0.9。
        constexpr float kMieScattering = 1.0e-4f;
        constexpr float kMieAbsorption = 1.11e-5f;
        // オゾンの吸収（Hillaire 2020）。高度25 kmを中心に幅±15 kmのテント形の密度。
        // 低い太陽で緑・赤が吸われ、夕方の天頂が青く残る。
        constexpr double kOzoneAbsorption[3] = {0.650e-6, 1.881e-6, 0.085e-6};
        constexpr double kOzoneCenterAltitudeMeters = 25000.0;
        constexpr double kOzoneHalfWidthMeters = 15000.0;

        // 光学的深さの数値積分と前計算の表の刻み。
        constexpr uint32_t kOpticalDepthSteps = 64u;
        constexpr uint32_t kTransmittanceTableAltitudeCount = 32u;
        constexpr uint32_t kTransmittanceTableCosineCount = 128u;
        constexpr uint32_t kMultipleScatteringAltitudeCount = 8u;
        constexpr uint32_t kMultipleScatteringCosineCount = 32u;
        constexpr uint32_t kMultipleScatteringDirectionSqrt = 8u;
        constexpr uint32_t kMultipleScatteringSteps = 16u;
        constexpr uint32_t kViewSteps = 32u;
        constexpr uint32_t kGroundIrradianceElevationCount = 8u;
        constexpr uint32_t kGroundIrradianceAzimuthCount = 16u;
        constexpr uint32_t kSkyViewElevationCount = 80u;
        constexpr uint32_t kSkyViewAzimuthCount = 40u;
        // 惑星に当たる光路の光学的深さ。表の補間で透過率がほぼ0になる大きさにする。
        constexpr float kGroundOpticalDepth = 1.0e3f;

        // 余弦 [-1, 1] を地平線（0）の側が細かい表の位置 [0, 1] へ写す（x|x| の逆）。
        double CosineToTableCoordinate(double cosine)
        {
            const double clamped = std::clamp(cosine, -1.0, 1.0);
            const double x = clamped >= 0.0 ? std::sqrt(clamped) : -std::sqrt(-clamped);
            return x * 0.5 + 0.5;
        }

        double TableCoordinateToCosine(uint32_t index, uint32_t count)
        {
            const double x = -1.0 + 2.0 * static_cast<double>(index) / (count - 1u);
            return x * std::abs(x);
        }

        float ClampFinite(float value, float minimum, float maximum, float fallback)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        float ClampPositiveFinite(float value, float minimum, float maximum, float fallback)
        {
            if (!std::isfinite(value) || value <= 0.0f)
            {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        Math::Vector3 NormalizeDirection(const Math::Vector3& value)
        {
            if (!IsFiniteVector(value) ||
                Math::VectorUtils::Length(value) <= Math::Constants::EPSILON)
            {
                return Math::Vector3::Zero;
            }
            return Math::VectorUtils::Normalize(value);
        }

        Math::Vector3 MakeSunDirection(const SkyAtmosphereParameters& parameters)
        {
            const float altitude = parameters.SunAltitudeDegrees * kDegreesToRadians;
            const float azimuth = parameters.SunAzimuthDegrees * kDegreesToRadians;
            const float horizontal = std::cos(altitude);
            return NormalizeDirection(Math::Vector3(horizontal * std::cos(azimuth),
                                                    std::sin(altitude),
                                                    horizontal * std::sin(azimuth)));
        }

        float ComputeHenyeyGreensteinPhase(float cosine, float anisotropy)
        {
            const float g2 = anisotropy * anisotropy;
            const float denominator = std::max(1.0f + g2 - 2.0f * anisotropy * cosine, 1.0e-4f);
            return (1.0f - g2) /
                (4.0f * kPi * denominator * std::sqrt(denominator));
        }

        float ComputeRayleighPhase(float cosine)
        {
            return 3.0f * (1.0f + cosine * cosine) / (16.0f * kPi);
        }

        double OzoneDensity(double altitude)
        {
            return std::max(0.0, 1.0 - std::abs(altitude - kOzoneCenterAltitudeMeters) /
                                           kOzoneHalfWidthMeters);
        }

        // 半径radiusの点から天頂との余弦cosineの向きに進み、大気の上端（半径top）を出るまでの距離。
        double DistanceToTopBoundary(double radius, double cosine, double top)
        {
            const double discriminant = radius * radius * (cosine * cosine - 1.0) + top * top;
            return std::max(0.0, -radius * cosine + std::sqrt(std::max(discriminant, 0.0)));
        }

        // 同じ光路が惑星（半径bottom）に当たるまでの距離。当たらなければ負。
        double DistanceToGround(double radius, double cosine, double bottom)
        {
            if (cosine >= 0.0)
            {
                return -1.0;
            }
            const double discriminant = radius * radius * (cosine * cosine - 1.0) + bottom * bottom;
            if (discriminant < 0.0)
            {
                return -1.0;
            }
            const double distance = -radius * cosine - std::sqrt(discriminant);
            return distance >= 0.0 ? distance : -1.0;
        }

        // 半径radiusの点から天頂との余弦cosineの向きに大気の上端まで、Rayleigh・Mieの消散係数と
        // オゾンの吸収係数×高度の密度を数値積分した光学的深さ（RGB）。光路が惑星に当たるならfalse。
        // 刻みは始点の側（低く濃い側）を細かくする2乗の配置で、各区間の中点の密度を使う。
        bool IntegrateOpticalDepth(const SkyAtmosphereParameters& sanitized,
                                   double radius,
                                   double cosine,
                                   double outOpticalDepth[3])
        {
            const double bottom = sanitized.PlanetRadiusMeters;
            const double top = bottom + sanitized.AtmosphereHeightMeters;
            radius = std::clamp(radius, bottom, top);
            cosine = std::clamp(cosine, -1.0, 1.0);
            if (DistanceToGround(radius, cosine, bottom) >= 0.0)
            {
                return false;
            }

            const double length = DistanceToTopBoundary(radius, cosine, top);
            double rayleigh = 0.0;
            double mie = 0.0;
            double ozone = 0.0;
            double previous = 0.0;
            for (uint32_t step = 0u; step < kOpticalDepthSteps; ++step)
            {
                const double fraction = static_cast<double>(step + 1u) / kOpticalDepthSteps;
                const double current = length * fraction * fraction;
                const double middle = 0.5 * (previous + current);
                const double segment = current - previous;
                previous = current;
                const double sampleRadius =
                    std::sqrt(radius * radius + middle * middle + 2.0 * radius * middle * cosine);
                const double altitude = std::max(sampleRadius - bottom, 0.0);
                rayleigh += std::exp(-altitude / sanitized.RayleighScaleHeightMeters) * segment;
                mie += std::exp(-altitude / sanitized.MieScaleHeightMeters) * segment;
                ozone += OzoneDensity(altitude) * segment;
            }
            const double mieExtinction =
                (static_cast<double>(kMieScattering) + kMieAbsorption) * mie;
            outOpticalDepth[0] =
                kRayleighScatteringR * rayleigh + mieExtinction + kOzoneAbsorption[0] * ozone;
            outOpticalDepth[1] =
                kRayleighScatteringG * rayleigh + mieExtinction + kOzoneAbsorption[1] * ozone;
            outOpticalDepth[2] =
                kRayleighScatteringB * rayleigh + mieExtinction + kOzoneAbsorption[2] * ozone;
            return true;
        }

        Math::Vector3 ComputeAtmosphereTransmittanceFromSanitized(
            const SkyAtmosphereParameters& sanitized,
            float altitudeFraction,
            float cosine)
        {
            const double altitude = std::isfinite(altitudeFraction)
                ? std::clamp(static_cast<double>(altitudeFraction), 0.0, 1.0)
                : 0.0;
            const double safeCosine = std::isfinite(cosine) ? cosine : 0.0;
            double opticalDepth[3] = {0.0, 0.0, 0.0};
            if (!IntegrateOpticalDepth(sanitized,
                                       sanitized.PlanetRadiusMeters +
                                           altitude * sanitized.AtmosphereHeightMeters,
                                       safeCosine,
                                       opticalDepth))
            {
                return Math::Vector3::Zero;
            }
            return Math::Vector3(static_cast<float>(std::exp(-opticalDepth[0])),
                                 static_cast<float>(std::exp(-opticalDepth[1])),
                                 static_cast<float>(std::exp(-opticalDepth[2])));
        }

        float ComputeSunDiskIrradianceFromSanitized(
            const SkyAtmosphereParameters& sanitized)
        {
            if (!sanitized.bEnabled)
            {
                return 0.0f;
            }
            return sanitized.SunLuminanceNits * SolarDiskSolidAngleSteradians;
        }
    } // namespace

    SkyAtmosphereParameters MakeDefaultSkyAtmosphereParameters()
    {
        return SkyAtmosphereParameters{};
    }

    SkyAtmosphereParameters SanitizeSkyAtmosphereParameters(
        const SkyAtmosphereParameters& parameters)
    {
        SkyAtmosphereParameters result = MakeDefaultSkyAtmosphereParameters();
        result.bEnabled = parameters.bEnabled;
        result.SunAltitudeDegrees = ClampFinite(parameters.SunAltitudeDegrees,
                                                0.0f,
                                                90.0f,
                                                result.SunAltitudeDegrees);
        result.SunAzimuthDegrees = ClampFinite(parameters.SunAzimuthDegrees,
                                               -360.0f,
                                               360.0f,
                                               result.SunAzimuthDegrees);
        result.SunLuminanceNits = ClampPositiveFinite(parameters.SunLuminanceNits,
                                                      1.0e3f,
                                                      2.0e9f,
                                                      result.SunLuminanceNits);
        result.PlanetRadiusMeters = ClampPositiveFinite(parameters.PlanetRadiusMeters,
                                                        1.0e6f,
                                                        1.0e8f,
                                                        result.PlanetRadiusMeters);
        result.AtmosphereHeightMeters = ClampPositiveFinite(parameters.AtmosphereHeightMeters,
                                                            1.0e3f,
                                                            2.0e5f,
                                                            result.AtmosphereHeightMeters);
        result.RayleighScaleHeightMeters = ClampPositiveFinite(parameters.RayleighScaleHeightMeters,
                                                               1.0e2f,
                                                               5.0e4f,
                                                               result.RayleighScaleHeightMeters);
        result.MieScaleHeightMeters = ClampPositiveFinite(parameters.MieScaleHeightMeters,
                                                          1.0e2f,
                                                          5.0e4f,
                                                          result.MieScaleHeightMeters);
        result.MieAnisotropy = ClampFinite(parameters.MieAnisotropy,
                                           -0.9f,
                                           0.9f,
                                           result.MieAnisotropy);
        result.GroundAlbedo.x = ClampFinite(parameters.GroundAlbedo.x,
                                             0.0f,
                                             1.0f,
                                             result.GroundAlbedo.x);
        result.GroundAlbedo.y = ClampFinite(parameters.GroundAlbedo.y,
                                             0.0f,
                                             1.0f,
                                             result.GroundAlbedo.y);
        result.GroundAlbedo.z = ClampFinite(parameters.GroundAlbedo.z,
                                             0.0f,
                                             1.0f,
                                             result.GroundAlbedo.z);
        return result;
    }

    Math::Vector3 MakeSunDirectionFromAltitudeAzimuth(float altitudeDegrees,
                                                       float azimuthDegrees)
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.SunAltitudeDegrees = altitudeDegrees;
        parameters.SunAzimuthDegrees = azimuthDegrees;
        return MakeSunDirection(SanitizeSkyAtmosphereParameters(parameters));
    }

    bool AreSkyAtmosphereParametersEqual(const SkyAtmosphereParameters& lhs,
                                         const SkyAtmosphereParameters& rhs)
    {
        return lhs.bEnabled == rhs.bEnabled &&
               lhs.SunAltitudeDegrees == rhs.SunAltitudeDegrees &&
               lhs.SunAzimuthDegrees == rhs.SunAzimuthDegrees &&
               lhs.SunLuminanceNits == rhs.SunLuminanceNits &&
               lhs.PlanetRadiusMeters == rhs.PlanetRadiusMeters &&
               lhs.AtmosphereHeightMeters == rhs.AtmosphereHeightMeters &&
               lhs.RayleighScaleHeightMeters == rhs.RayleighScaleHeightMeters &&
               lhs.MieScaleHeightMeters == rhs.MieScaleHeightMeters &&
               lhs.MieAnisotropy == rhs.MieAnisotropy &&
               lhs.GroundAlbedo.x == rhs.GroundAlbedo.x &&
               lhs.GroundAlbedo.y == rhs.GroundAlbedo.y &&
               lhs.GroundAlbedo.z == rhs.GroundAlbedo.z;
    }

    SkyAtmosphereModel::SkyAtmosphereModel(const SkyAtmosphereParameters& parameters)
        : m_Parameters(SanitizeSkyAtmosphereParameters(parameters))
    {
        if (!m_Parameters.bEnabled)
        {
            return;
        }

        const Math::Vector3 sun = MakeSunDirection(m_Parameters);
        m_SunDirection[0] = sun.x;
        m_SunDirection[1] = sun.y;
        m_SunDirection[2] = sun.z;
        m_SunAzimuth = std::atan2(m_SunDirection[2], m_SunDirection[0]);
        m_HorizonElevation = -std::acos(std::clamp(
            static_cast<double>(m_Parameters.PlanetRadiusMeters) /
                (m_Parameters.PlanetRadiusMeters + ObserverAltitudeMeters),
            -1.0,
            1.0));
        m_SunDiskIrradiance = ComputeSunDiskIrradianceFromSanitized(m_Parameters);
        m_SunGroundTransmittance =
            ComputeAtmosphereTransmittanceFromSanitized(m_Parameters, 0.0f, sun.y);

        // 多重散乱の表は透過率の表を、地表の空の照度は両方の表を引く。
        BuildTransmittanceTable();
        BuildMultipleScatteringTable();
        BuildGroundSkyIrradiance();
    }

    void SkyAtmosphereModel::BuildTransmittanceTable()
    {
        // 高度は v^2（v∈[0,1]）で地表の側を、余弦は x|x|（x∈[-1,1]）で地平線の側を細かくする。
        // 透過率は指数で変わるので、光学的深さを持って補間してから指数を取る。
        m_TransmittanceTable.resize(static_cast<size_t>(kTransmittanceTableAltitudeCount) *
                                    kTransmittanceTableCosineCount * 3u);
        for (uint32_t row = 0u; row < kTransmittanceTableAltitudeCount; ++row)
        {
            const double v = static_cast<double>(row) / (kTransmittanceTableAltitudeCount - 1u);
            const double radius = m_Parameters.PlanetRadiusMeters +
                                  m_Parameters.AtmosphereHeightMeters * v * v;
            for (uint32_t column = 0u; column < kTransmittanceTableCosineCount; ++column)
            {
                double opticalDepth[3] = {0.0, 0.0, 0.0};
                const bool bReachesSpace = IntegrateOpticalDepth(
                    m_Parameters,
                    radius,
                    TableCoordinateToCosine(column, kTransmittanceTableCosineCount),
                    opticalDepth);
                const size_t offset =
                    (static_cast<size_t>(row) * kTransmittanceTableCosineCount + column) * 3u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    m_TransmittanceTable[offset + channel] = bReachesSpace
                        ? std::min(static_cast<float>(opticalDepth[channel]), kGroundOpticalDepth)
                        : kGroundOpticalDepth;
                }
            }
        }
    }

    SkyAtmosphereModel::Rgb SkyAtmosphereModel::LookupTransmittance(double radius,
                                                                    double cosine) const
    {
        const double altitudeFraction = std::clamp(
            (radius - m_Parameters.PlanetRadiusMeters) / m_Parameters.AtmosphereHeightMeters,
            0.0,
            1.0);
        const double rowPosition = std::sqrt(altitudeFraction) *
                                   (kTransmittanceTableAltitudeCount - 1u);
        const double columnPosition =
            CosineToTableCoordinate(cosine) * (kTransmittanceTableCosineCount - 1u);
        const uint32_t row = std::min(static_cast<uint32_t>(rowPosition),
                                      kTransmittanceTableAltitudeCount - 2u);
        const uint32_t column = std::min(static_cast<uint32_t>(columnPosition),
                                         kTransmittanceTableCosineCount - 2u);
        const float rowWeight = static_cast<float>(rowPosition - row);
        const float columnWeight = static_cast<float>(columnPosition - column);
        const float* data = m_TransmittanceTable.data();
        const size_t offset00 =
            (static_cast<size_t>(row) * kTransmittanceTableCosineCount + column) * 3u;
        const size_t offset10 = offset00 + static_cast<size_t>(kTransmittanceTableCosineCount) * 3u;
        const float weight00 = (1.0f - rowWeight) * (1.0f - columnWeight);
        const float weight01 = (1.0f - rowWeight) * columnWeight;
        const float weight10 = rowWeight * (1.0f - columnWeight);
        const float weight11 = rowWeight * columnWeight;
        Rgb result;
        result.r = std::exp(-(data[offset00] * weight00 + data[offset00 + 3u] * weight01 +
                              data[offset10] * weight10 + data[offset10 + 3u] * weight11));
        result.g = std::exp(-(data[offset00 + 1u] * weight00 + data[offset00 + 4u] * weight01 +
                              data[offset10 + 1u] * weight10 + data[offset10 + 4u] * weight11));
        result.b = std::exp(-(data[offset00 + 2u] * weight00 + data[offset00 + 5u] * weight01 +
                              data[offset10 + 2u] * weight10 + data[offset10 + 5u] * weight11));
        return result;
    }

    SkyAtmosphereModel::Rgb SkyAtmosphereModel::LookupMultipleScattering(double radius,
                                                                         double sunCosine) const
    {
        const double altitudeFraction = std::clamp(
            (radius - m_Parameters.PlanetRadiusMeters) / m_Parameters.AtmosphereHeightMeters,
            0.0,
            1.0);
        const double rowPosition = std::cbrt(altitudeFraction) *
                                   (kMultipleScatteringAltitudeCount - 1u);
        const double columnPosition =
            CosineToTableCoordinate(sunCosine) * (kMultipleScatteringCosineCount - 1u);
        const uint32_t row = std::min(static_cast<uint32_t>(rowPosition),
                                      kMultipleScatteringAltitudeCount - 2u);
        const uint32_t column = std::min(static_cast<uint32_t>(columnPosition),
                                         kMultipleScatteringCosineCount - 2u);
        const float rowWeight = static_cast<float>(rowPosition - row);
        const float columnWeight = static_cast<float>(columnPosition - column);
        const float* data = m_MultipleScatteringTable.data();
        const size_t offset00 =
            (static_cast<size_t>(row) * kMultipleScatteringCosineCount + column) * 3u;
        const size_t offset10 =
            offset00 + static_cast<size_t>(kMultipleScatteringCosineCount) * 3u;
        const float weight00 = (1.0f - rowWeight) * (1.0f - columnWeight);
        const float weight01 = (1.0f - rowWeight) * columnWeight;
        const float weight10 = rowWeight * (1.0f - columnWeight);
        const float weight11 = rowWeight * columnWeight;
        Rgb result;
        result.r = data[offset00] * weight00 + data[offset00 + 3u] * weight01 +
                   data[offset10] * weight10 + data[offset10 + 3u] * weight11;
        result.g = data[offset00 + 1u] * weight00 + data[offset00 + 4u] * weight01 +
                   data[offset10 + 1u] * weight10 + data[offset10 + 4u] * weight11;
        result.b = data[offset00 + 2u] * weight00 + data[offset00 + 5u] * weight01 +
                   data[offset10 + 2u] * weight10 + data[offset10 + 5u] * weight11;
        return result;
    }

    SkyAtmosphereModel::MarchResult SkyAtmosphereModel::March(
        const double origin[3],
        const double direction[3],
        const double sunDirection[3],
        uint32_t stepCount,
        bool bIsotropicSingleScattering,
        bool bIncludeMultipleScattering) const
    {
        MarchResult result;
        const double bottom = m_Parameters.PlanetRadiusMeters;
        const double top = bottom + m_Parameters.AtmosphereHeightMeters;
        const double originRadius = std::sqrt(origin[0] * origin[0] +
                                              origin[1] * origin[1] +
                                              origin[2] * origin[2]);
        const double viewCosine = (origin[0] * direction[0] +
                                   origin[1] * direction[1] +
                                   origin[2] * direction[2]) / originRadius;
        const double groundDistance = DistanceToGround(originRadius, viewCosine, bottom);
        result.bHitGround = groundDistance >= 0.0;
        const double length = result.bHitGround
            ? groundDistance
            : DistanceToTopBoundary(originRadius, viewCosine, top);

        const float cosineToSun = static_cast<float>(std::clamp(
            direction[0] * sunDirection[0] + direction[1] * sunDirection[1] +
                direction[2] * sunDirection[2],
            -1.0,
            1.0));
        const double rayleighPhase = bIsotropicSingleScattering
            ? 1.0 / (4.0 * kPi)
            : ComputeRayleighPhase(cosineToSun);
        const double miePhase = bIsotropicSingleScattering
            ? 1.0 / (4.0 * kPi)
            : ComputeHenyeyGreensteinPhase(cosineToSun, m_Parameters.MieAnisotropy);
        const double rayleighCoefficients[3] = {
            kRayleighScatteringR, kRayleighScatteringG, kRayleighScatteringB};
        const double mieExtinction = static_cast<double>(kMieScattering) + kMieAbsorption;

        double inscattering[3] = {0.0, 0.0, 0.0};
        double transfer[3] = {0.0, 0.0, 0.0};
        double throughput[3] = {1.0, 1.0, 1.0};
        double previous = 0.0;
        for (uint32_t step = 0u; step < stepCount; ++step)
        {
            const double fraction = static_cast<double>(step + 1u) / stepCount;
            const double current = length * fraction * fraction;
            const double middle = 0.5 * (previous + current);
            const double segment = current - previous;
            previous = current;

            const double position[3] = {origin[0] + direction[0] * middle,
                                        origin[1] + direction[1] * middle,
                                        origin[2] + direction[2] * middle};
            const double radius = std::sqrt(position[0] * position[0] +
                                            position[1] * position[1] +
                                            position[2] * position[2]);
            const double altitude = std::max(radius - bottom, 0.0);
            const double rayleighDensity =
                std::exp(-altitude / m_Parameters.RayleighScaleHeightMeters);
            const double mieDensity = std::exp(-altitude / m_Parameters.MieScaleHeightMeters);
            const double ozoneDensity = OzoneDensity(altitude);
            const double sunCosine = (position[0] * sunDirection[0] +
                                      position[1] * sunDirection[1] +
                                      position[2] * sunDirection[2]) / radius;
            const Rgb sunTransmittance = LookupTransmittance(radius, sunCosine);
            const double sunTransmittanceChannels[3] = {
                sunTransmittance.r, sunTransmittance.g, sunTransmittance.b};
            Rgb multipleScattering;
            if (bIncludeMultipleScattering)
            {
                multipleScattering = LookupMultipleScattering(radius, sunCosine);
            }
            const double multipleScatteringChannels[3] = {
                multipleScattering.r, multipleScattering.g, multipleScattering.b};

            const double mieScattering = kMieScattering * mieDensity;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double rayleighScattering = rayleighCoefficients[channel] * rayleighDensity;
                const double scattering = rayleighScattering + mieScattering;
                const double extinction = rayleighScattering + mieExtinction * mieDensity +
                                          kOzoneAbsorption[channel] * ozoneDensity;
                const double source =
                    (rayleighScattering * rayleighPhase + mieScattering * miePhase) *
                        sunTransmittanceChannels[channel] +
                    scattering * multipleScatteringChannels[channel];
                // 区間の中で媒質を一定とみなした (1 - e^(-σt·Δt)) / σt の積分。
                const double stepTransmittance = std::exp(-extinction * segment);
                const double integral = (1.0 - stepTransmittance) / extinction;
                inscattering[channel] += throughput[channel] * source * integral;
                transfer[channel] += throughput[channel] * scattering * integral;
                throughput[channel] *= stepTransmittance;
            }
        }

        result.Inscattering = {static_cast<float>(inscattering[0]),
                               static_cast<float>(inscattering[1]),
                               static_cast<float>(inscattering[2])};
        result.Transfer = {static_cast<float>(transfer[0]),
                           static_cast<float>(transfer[1]),
                           static_cast<float>(transfer[2])};
        result.Throughput = {static_cast<float>(throughput[0]),
                             static_cast<float>(throughput[1]),
                             static_cast<float>(throughput[2])};
        if (result.bHitGround)
        {
            const double hit[3] = {origin[0] + direction[0] * length,
                                   origin[1] + direction[1] * length,
                                   origin[2] + direction[2] * length};
            const double hitRadius =
                std::sqrt(hit[0] * hit[0] + hit[1] * hit[1] + hit[2] * hit[2]);
            result.GroundNormal[0] = hit[0] / hitRadius;
            result.GroundNormal[1] = hit[1] / hitRadius;
            result.GroundNormal[2] = hit[2] / hitRadius;
        }
        return result;
    }

    void SkyAtmosphereModel::BuildMultipleScatteringTable()
    {
        // Hillaire 2020 の等方の多重散乱。各点で全方向から、太陽の単位照度による等方の
        // 2次散乱の放射輝度 L_2nd と、散乱の割合 f_ms を集め、Ψ_ms = L_2nd / (1 - f_ms) とする。
        // 太陽の余弦は透過率の表と同じく地平線の側を細かくする（低い太陽で急に変わるため）。
        m_MultipleScatteringTable.resize(static_cast<size_t>(kMultipleScatteringAltitudeCount) *
                                         kMultipleScatteringCosineCount * 3u);
        const double albedo[3] = {m_Parameters.GroundAlbedo.x,
                                  m_Parameters.GroundAlbedo.y,
                                  m_Parameters.GroundAlbedo.z};
        constexpr uint32_t directionCount =
            kMultipleScatteringDirectionSqrt * kMultipleScatteringDirectionSqrt;
        // 立体角 4π/N と等方の位相関数 1/(4π) の積。
        constexpr double directionWeight = 1.0 / directionCount;
        for (uint32_t row = 0u; row < kMultipleScatteringAltitudeCount; ++row)
        {
            const double v = static_cast<double>(row) / (kMultipleScatteringAltitudeCount - 1u);
            const double origin[3] = {
                0.0,
                m_Parameters.PlanetRadiusMeters + m_Parameters.AtmosphereHeightMeters * v * v * v,
                0.0};
            for (uint32_t column = 0u; column < kMultipleScatteringCosineCount; ++column)
            {
                const double sunCosine =
                    TableCoordinateToCosine(column, kMultipleScatteringCosineCount);
                const double sun[3] = {std::sqrt(std::max(0.0, 1.0 - sunCosine * sunCosine)),
                                       sunCosine,
                                       0.0};
                double secondOrder[3] = {0.0, 0.0, 0.0};
                double transferSum[3] = {0.0, 0.0, 0.0};
                for (uint32_t polar = 0u; polar < kMultipleScatteringDirectionSqrt; ++polar)
                {
                    const double cosine =
                        1.0 - 2.0 * (polar + 0.5) / kMultipleScatteringDirectionSqrt;
                    const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
                    for (uint32_t azimuth = 0u; azimuth < kMultipleScatteringDirectionSqrt; ++azimuth)
                    {
                        const double phi =
                            2.0 * kPi * (azimuth + 0.5) / kMultipleScatteringDirectionSqrt;
                        const double direction[3] = {
                            sine * std::cos(phi), cosine, sine * std::sin(phi)};
                        const MarchResult march = March(origin, direction, sun,
                                                        kMultipleScatteringSteps, true, false);
                        const double inscattering[3] = {
                            march.Inscattering.r, march.Inscattering.g, march.Inscattering.b};
                        const double transfer[3] = {
                            march.Transfer.r, march.Transfer.g, march.Transfer.b};
                        const double throughput[3] = {
                            march.Throughput.r, march.Throughput.g, march.Throughput.b};
                        double groundLight[3] = {0.0, 0.0, 0.0};
                        if (march.bHitGround)
                        {
                            const double groundSunCosine =
                                march.GroundNormal[0] * sun[0] + march.GroundNormal[1] * sun[1] +
                                march.GroundNormal[2] * sun[2];
                            const Rgb groundSun = LookupTransmittance(
                                m_Parameters.PlanetRadiusMeters, groundSunCosine);
                            const double groundSunChannels[3] = {groundSun.r, groundSun.g, groundSun.b};
                            for (uint32_t channel = 0u; channel < 3u; ++channel)
                            {
                                groundLight[channel] = groundSunChannels[channel] *
                                                       std::max(groundSunCosine, 0.0) *
                                                       albedo[channel] / kPi;
                            }
                        }
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            secondOrder[channel] += inscattering[channel] +
                                                    throughput[channel] * groundLight[channel];
                            transferSum[channel] += transfer[channel];
                        }
                    }
                }

                const size_t offset =
                    (static_cast<size_t>(row) * kMultipleScatteringCosineCount + column) * 3u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double secondOrderLuminance = secondOrder[channel] * directionWeight;
                    const double transferFraction =
                        std::clamp(transferSum[channel] * directionWeight, 0.0, 0.99);
                    m_MultipleScatteringTable[offset + channel] =
                        static_cast<float>(secondOrderLuminance / (1.0 - transferFraction));
                }
            }
        }
    }

    void SkyAtmosphereModel::BuildGroundSkyIrradiance()
    {
        // 地表の水平面に上半球の空から届く照度（太陽の単位照度あたり）。仰角の正弦で等分した
        // 格子で余弦の重みを積分する。地平線より下の地面を照らすのに使う。
        const double origin[3] = {0.0, m_Parameters.PlanetRadiusMeters, 0.0};
        double irradiance[3] = {0.0, 0.0, 0.0};
        for (uint32_t elevation = 0u; elevation < kGroundIrradianceElevationCount; ++elevation)
        {
            const double sine = (elevation + 0.5) / kGroundIrradianceElevationCount;
            const double cosine = std::sqrt(std::max(0.0, 1.0 - sine * sine));
            for (uint32_t azimuth = 0u; azimuth < kGroundIrradianceAzimuthCount; ++azimuth)
            {
                const double phi = 2.0 * kPi * (azimuth + 0.5) / kGroundIrradianceAzimuthCount;
                const double direction[3] = {cosine * std::cos(phi), sine, cosine * std::sin(phi)};
                const MarchResult march =
                    March(origin, direction, m_SunDirection, kViewSteps, false, true);
                const double weight = sine * (2.0 * kPi) /
                    (static_cast<double>(kGroundIrradianceElevationCount) *
                     kGroundIrradianceAzimuthCount);
                irradiance[0] += march.Inscattering.r * weight;
                irradiance[1] += march.Inscattering.g * weight;
                irradiance[2] += march.Inscattering.b * weight;
            }
        }
        m_GroundSkyIrradiance = {static_cast<float>(irradiance[0]),
                                 static_cast<float>(irradiance[1]),
                                 static_cast<float>(irradiance[2])};
    }

    Math::Vector3 SkyAtmosphereModel::GetTransmittance(float altitudeFraction, float cosine) const
    {
        if (!m_Parameters.bEnabled || m_TransmittanceTable.empty())
        {
            return Math::Vector3::Zero;
        }
        const double altitude = std::isfinite(altitudeFraction)
            ? std::clamp(static_cast<double>(altitudeFraction), 0.0, 1.0)
            : 0.0;
        const Rgb transmittance = LookupTransmittance(
            m_Parameters.PlanetRadiusMeters + altitude * m_Parameters.AtmosphereHeightMeters,
            std::isfinite(cosine) ? cosine : 0.0);
        return Math::Vector3(transmittance.r, transmittance.g, transmittance.b);
    }

    Math::Vector3 SkyAtmosphereModel::GetGroundSkyIlluminance() const
    {
        return Math::Vector3(m_GroundSkyIrradiance.r * m_SunDiskIrradiance,
                             m_GroundSkyIrradiance.g * m_SunDiskIrradiance,
                             m_GroundSkyIrradiance.b * m_SunDiskIrradiance);
    }

    SkyRadianceSample SkyAtmosphereModel::EvaluateViewRadiance(
        const Math::Vector3& viewDirection) const
    {
        SkyRadianceSample result;
        if (!m_Parameters.bEnabled)
        {
            return result;
        }
        const Math::Vector3 view = NormalizeDirection(viewDirection);
        if (view == Math::Vector3::Zero)
        {
            return result;
        }

        const double origin[3] = {
            0.0, m_Parameters.PlanetRadiusMeters + ObserverAltitudeMeters, 0.0};
        const double direction[3] = {view.x, view.y, view.z};
        const MarchResult march =
            March(origin, direction, m_SunDirection, kViewSteps, false, true);
        double radiance[3] = {march.Inscattering.r, march.Inscattering.g, march.Inscattering.b};
        if (march.bHitGround)
        {
            // 地面はランバート面。透過した太陽と空の照度で照らし、そこまでの透過率を掛ける。
            const double groundSunCosine = march.GroundNormal[0] * m_SunDirection[0] +
                                           march.GroundNormal[1] * m_SunDirection[1] +
                                           march.GroundNormal[2] * m_SunDirection[2];
            const Rgb groundSun =
                LookupTransmittance(m_Parameters.PlanetRadiusMeters, groundSunCosine);
            const double directCosine = std::max(groundSunCosine, 0.0);
            radiance[0] += march.Throughput.r * m_Parameters.GroundAlbedo.x / kPi *
                           (groundSun.r * directCosine + m_GroundSkyIrradiance.r);
            radiance[1] += march.Throughput.g * m_Parameters.GroundAlbedo.y / kPi *
                           (groundSun.g * directCosine + m_GroundSkyIrradiance.g);
            radiance[2] += march.Throughput.b * m_Parameters.GroundAlbedo.z / kPi *
                           (groundSun.b * directCosine + m_GroundSkyIrradiance.b);
        }

        result.Radiance = Math::Vector3(static_cast<float>(radiance[0] * m_SunDiskIrradiance),
                                        static_cast<float>(radiance[1] * m_SunDiskIrradiance),
                                        static_cast<float>(radiance[2] * m_SunDiskIrradiance));
        result.MeanSunTransmittance = (m_SunGroundTransmittance.x +
                                       m_SunGroundTransmittance.y +
                                       m_SunGroundTransmittance.z) / 3.0f;
        result.bValid = true;
        if (!IsFiniteVector(result.Radiance) || !std::isfinite(result.MeanSunTransmittance))
        {
            return SkyRadianceSample{};
        }
        return result;
    }

    double SkyAtmosphereModel::SkyViewRowToElevation(uint32_t row) const
    {
        // u∈[-1,1] の2乗で、観測点の地平線（u=0）の近くを細かくする。行の数は偶数で、
        // 空と地面の境目は2つの行の間に入る。
        const double u = -1.0 + 2.0 * static_cast<double>(row) / (kSkyViewElevationCount - 1u);
        const double halfPi = 0.5 * kPi;
        return u >= 0.0 ? m_HorizonElevation + (halfPi - m_HorizonElevation) * u * u
                        : m_HorizonElevation - (halfPi + m_HorizonElevation) * u * u;
    }

    void SkyAtmosphereModel::BuildSkyViewTable()
    {
        if (!m_Parameters.bEnabled)
        {
            return;
        }
        m_SkyViewTable.resize(static_cast<size_t>(kSkyViewElevationCount) *
                              kSkyViewAzimuthCount * 3u);
        for (uint32_t row = 0u; row < kSkyViewElevationCount; ++row)
        {
            const double elevation = SkyViewRowToElevation(row);
            const double horizontal = std::cos(elevation);
            const double vertical = std::sin(elevation);
            for (uint32_t column = 0u; column < kSkyViewAzimuthCount; ++column)
            {
                // 方位差は w∈[0,1] の2乗で太陽の側を細かくする。
                const double w = static_cast<double>(column) / (kSkyViewAzimuthCount - 1u);
                const double azimuth = m_SunAzimuth + kPi * w * w;
                const SkyRadianceSample sample = EvaluateViewRadiance(Math::Vector3(
                    static_cast<float>(horizontal * std::cos(azimuth)),
                    static_cast<float>(vertical),
                    static_cast<float>(horizontal * std::sin(azimuth))));
                const size_t offset =
                    (static_cast<size_t>(row) * kSkyViewAzimuthCount + column) * 3u;
                m_SkyViewTable[offset + 0u] = sample.Radiance.x;
                m_SkyViewTable[offset + 1u] = sample.Radiance.y;
                m_SkyViewTable[offset + 2u] = sample.Radiance.z;
            }
        }
    }

    SkyRadianceSample SkyAtmosphereModel::SampleSkyView(const Math::Vector3& viewDirection) const
    {
        if (m_SkyViewTable.empty())
        {
            return EvaluateViewRadiance(viewDirection);
        }
        SkyRadianceSample result;
        const Math::Vector3 view = NormalizeDirection(viewDirection);
        if (view == Math::Vector3::Zero)
        {
            return result;
        }

        const double halfPi = 0.5 * kPi;
        const double elevation = std::asin(std::clamp(static_cast<double>(view.y), -1.0, 1.0));
        const double u = elevation >= m_HorizonElevation
            ? std::sqrt((elevation - m_HorizonElevation) / (halfPi - m_HorizonElevation))
            : -std::sqrt((m_HorizonElevation - elevation) / (halfPi + m_HorizonElevation));
        const double azimuthDifference = std::abs(std::remainder(
            std::atan2(static_cast<double>(view.z), static_cast<double>(view.x)) - m_SunAzimuth,
            2.0 * kPi));
        const double w = std::sqrt(std::clamp(azimuthDifference / kPi, 0.0, 1.0));

        const double rowPosition = std::clamp(u * 0.5 + 0.5, 0.0, 1.0) *
                                   (kSkyViewElevationCount - 1u);
        const double columnPosition = w * (kSkyViewAzimuthCount - 1u);
        const uint32_t row = std::min(static_cast<uint32_t>(rowPosition),
                                      kSkyViewElevationCount - 2u);
        const uint32_t column = std::min(static_cast<uint32_t>(columnPosition),
                                         kSkyViewAzimuthCount - 2u);
        const float rowWeight = static_cast<float>(rowPosition - row);
        const float columnWeight = static_cast<float>(columnPosition - column);
        const float* data = m_SkyViewTable.data();
        const size_t offset00 = (static_cast<size_t>(row) * kSkyViewAzimuthCount + column) * 3u;
        const size_t offset10 = offset00 + static_cast<size_t>(kSkyViewAzimuthCount) * 3u;
        const float weight00 = (1.0f - rowWeight) * (1.0f - columnWeight);
        const float weight01 = (1.0f - rowWeight) * columnWeight;
        const float weight10 = rowWeight * (1.0f - columnWeight);
        const float weight11 = rowWeight * columnWeight;
        result.Radiance = Math::Vector3(
            data[offset00] * weight00 + data[offset00 + 3u] * weight01 +
                data[offset10] * weight10 + data[offset10 + 3u] * weight11,
            data[offset00 + 1u] * weight00 + data[offset00 + 4u] * weight01 +
                data[offset10 + 1u] * weight10 + data[offset10 + 4u] * weight11,
            data[offset00 + 2u] * weight00 + data[offset00 + 5u] * weight01 +
                data[offset10 + 2u] * weight10 + data[offset10 + 5u] * weight11);
        result.MeanSunTransmittance = (m_SunGroundTransmittance.x +
                                       m_SunGroundTransmittance.y +
                                       m_SunGroundTransmittance.z) / 3.0f;
        result.bValid = IsFiniteVector(result.Radiance);
        return result;
    }

    SkyRadianceSample EvaluateHillaireSkyReference(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection)
    {
        return SkyAtmosphereModel(parameters).EvaluateViewRadiance(viewDirection);
    }

    SkyRadianceSample EvaluateSkyViewRadiance(
        const SkyAtmosphereParameters& parameters,
        const Math::Vector3& viewDirection)
    {
        return SkyAtmosphereModel(parameters).EvaluateViewRadiance(viewDirection);
    }

    float ComputeSunDiskIrradiance(
        const SkyAtmosphereParameters& parameters)
    {
        return ComputeSunDiskIrradianceFromSanitized(
            SanitizeSkyAtmosphereParameters(parameters));
    }

    Math::Vector3 ComputeAtmosphereTransmittance(
        const SkyAtmosphereParameters& parameters,
        float altitudeFraction,
        float cosine)
    {
        return ComputeAtmosphereTransmittanceFromSanitized(
            SanitizeSkyAtmosphereParameters(parameters), altitudeFraction, cosine);
    }

    Math::Vector3 ComputeSunGroundTransmittance(
        const SkyAtmosphereParameters& parameters)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled)
        {
            return Math::Vector3::Zero;
        }
        return ComputeAtmosphereTransmittanceFromSanitized(
            sanitized, 0.0f, MakeSunDirection(sanitized).y);
    }

    Math::Vector3 ComputeSunGroundIlluminance(
        const SkyAtmosphereParameters& parameters)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled)
        {
            return Math::Vector3::Zero;
        }
        const Math::Vector3 transmittance = ComputeAtmosphereTransmittanceFromSanitized(
            sanitized, 0.0f, MakeSunDirection(sanitized).y);
        const float irradiance = ComputeSunDiskIrradianceFromSanitized(sanitized);
        return Math::Vector3(transmittance.x * irradiance,
                             transmittance.y * irradiance,
                             transmittance.z * irradiance);
    }

    float ComputeSunDiskPreExposedLuminance(
        const SkyAtmosphereParameters& parameters,
        float preExposure)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled || !std::isfinite(preExposure) || preExposure <= 0.0f)
        {
            return 0.0f;
        }
        return sanitized.SunLuminanceNits * preExposure;
    }

    bool IsSunDiskWithinFp16SafetyRange(
        const SkyAtmosphereParameters& parameters,
        float preExposure,
        float safetyFraction)
    {
        if (!std::isfinite(safetyFraction) || safetyFraction <= 0.0f || safetyFraction > 1.0f)
        {
            return false;
        }
        const float preExposedLuminance =
            ComputeSunDiskPreExposedLuminance(parameters, preExposure);
        return std::isfinite(preExposedLuminance) &&
            preExposedLuminance <= kFp16Max * safetyFraction;
    }
} // namespace NorvesLib::Core::Rendering
