#include "Rendering/SkyAtmosphere.h"
#include "Rendering/SkySunLight.h"

#include "Math/VectorUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Rendering;

static_assert(std::is_trivially_copyable_v<SkyAtmosphereParameters>);
static_assert(std::is_standard_layout_v<SkyAtmosphereParameters>);

namespace
{
    int g_FailureCount = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++g_FailureCount;
            std::cout << "FAILED: " << message << "\n";
        }
    }

    bool NearlyEqual(float lhs, float rhs, float epsilon)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool RelativeNearlyEqual(float lhs, float rhs, float relativeError)
    {
        const float scale = std::max(1.0f, std::abs(rhs));
        return std::abs(lhs - rhs) <= scale * relativeError;
    }

    void ExpectFiniteVector(const NorvesLib::Math::Vector3& value, const char* message)
    {
        Expect(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
               message);
    }

    void TestSunDirectionCoordinateContract()
    {
        const NorvesLib::Math::Vector3 horizon =
            MakeSunDirectionFromAltitudeAzimuth(0.0f, 0.0f);
        Expect(NearlyEqual(horizon.x, 1.0f, 1.0e-5f) &&
                   NearlyEqual(horizon.y, 0.0f, 1.0e-5f) &&
                   NearlyEqual(horizon.z, 0.0f, 1.0e-5f),
               "zero altitude and azimuth point along +X");

        const NorvesLib::Math::Vector3 zenith =
            MakeSunDirectionFromAltitudeAzimuth(90.0f, 135.0f);
        Expect(NearlyEqual(zenith.x, 0.0f, 1.0e-5f) &&
                   NearlyEqual(zenith.y, 1.0f, 1.0e-5f) &&
                   NearlyEqual(zenith.z, 0.0f, 1.0e-5f),
               "ninety degree altitude points along +Y");

        const NorvesLib::Math::Vector3 diagonal =
            MakeSunDirectionFromAltitudeAzimuth(45.0f, 90.0f);
        const float diagonalComponent = std::sqrt(0.5f);
        Expect(NearlyEqual(diagonal.x, 0.0f, 1.0e-5f) &&
                   NearlyEqual(diagonal.y, diagonalComponent, 1.0e-5f) &&
                   NearlyEqual(diagonal.z, diagonalComponent, 1.0e-5f),
               "altitude and azimuth use the horizontal XZ plane");
        Expect(NearlyEqual(NorvesLib::Math::VectorUtils::Length(diagonal), 1.0f, 1.0e-5f),
               "sun direction is normalized");
    }

    void TestSanitizationKeepsSnapshotFinite()
    {
        SkyAtmosphereParameters invalid = MakeDefaultSkyAtmosphereParameters();
        invalid.bEnabled = true;
        invalid.SunAltitudeDegrees = std::numeric_limits<float>::quiet_NaN();
        invalid.SunAzimuthDegrees = std::numeric_limits<float>::infinity();
        invalid.SunLuminanceNits = -1.0f;
        invalid.PlanetRadiusMeters = std::numeric_limits<float>::infinity();
        invalid.AtmosphereHeightMeters = -1.0f;
        invalid.RayleighScaleHeightMeters = std::numeric_limits<float>::quiet_NaN();
        invalid.MieAnisotropy = 3.0f;
        invalid.GroundAlbedo = NorvesLib::Math::Vector3(
            -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN());

        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(invalid);
        Expect(sanitized.bEnabled, "sanitization preserves enabled state");
        Expect(sanitized.SunAltitudeDegrees == 45.0f,
               "non-finite altitude uses the default");
        Expect(sanitized.SunAzimuthDegrees == 0.0f,
               "non-finite azimuth uses the default");
        Expect(sanitized.SunLuminanceNits == 1.6e9f,
               "non-positive luminance uses the default");
        Expect(sanitized.PlanetRadiusMeters == 6360000.0f &&
                   sanitized.AtmosphereHeightMeters == 80000.0f &&
                   sanitized.RayleighScaleHeightMeters == 8000.0f &&
                   sanitized.MieScaleHeightMeters == 1200.0f,
               "atmosphere dimensions and density heights use finite defaults");
        Expect(sanitized.MieAnisotropy == 0.9f,
               "anisotropy is clamped to the phase-function domain");
        Expect(sanitized.GroundAlbedo.x == 0.0f &&
                   sanitized.GroundAlbedo.y == 1.0f &&
                   sanitized.GroundAlbedo.z == 0.3f,
               "ground albedo is finite and clamped");
        ExpectFiniteVector(MakeSunDirectionFromAltitudeAzimuth(
                               invalid.SunAltitudeDegrees,
                               invalid.SunAzimuthDegrees),
                           "sanitized sun direction is finite");
    }

    // 検証用の独立な実装。モデルと同じ物理定数（Rayleigh・Mieの係数、密度の尺度高さ、位相関数、
    // 観測点の高さ）を持ち、モデルの表や刻みを使わず、細かい等間隔の刻みの直接の数値積分で求める。
    // 多重散乱はモデルと同じ Hillaire 2020 の近似だが、方向・高度・余弦の格子と刻みを独立に細かくする。
    namespace Reference
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kRayleigh[3] = {5.802e-6, 13.558e-6, 33.100e-6};
        constexpr double kMieScattering = 1.0e-4;
        constexpr double kMieExtinction = 1.0e-4 + 1.11e-5;
        constexpr double kOzone[3] = {0.650e-6, 1.881e-6, 0.085e-6};

        constexpr int kTransmittanceSteps = 2000;
        constexpr int kSunTransmittanceSteps = 200;
        constexpr int kViewSteps = 800;
        constexpr int kTableTransmittanceSteps = 400;
        constexpr int kTableAltitudes = 48;
        constexpr int kTableCosines = 256;
        constexpr int kPsiAltitudes = 12;
        constexpr int kPsiCosines = 64;
        constexpr int kPsiDirectionSqrt = 12;
        constexpr int kPsiSteps = 64;
        constexpr int kIrradianceElevations = 6;
        constexpr int kIrradianceAzimuths = 12;
        constexpr int kIrradianceSteps = 300;

        struct Atmosphere
        {
            double Bottom = 0.0;
            double Top = 0.0;
            double RayleighHeight = 0.0;
            double MieHeight = 0.0;
            double Anisotropy = 0.0;
            double Albedo[3] = {0.0, 0.0, 0.0};
            double Sun[3] = {0.0, 1.0, 0.0};
            double SunIrradiance = 0.0;
            NorvesLib::Core::Container::VariableArray<double> TransmittanceTable;
            NorvesLib::Core::Container::VariableArray<double> PsiTable;
            double GroundSkyIrradiance[3] = {0.0, 0.0, 0.0};
        };

        double Dot(const double a[3], const double b[3])
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }

        // 惑星中心を原点とする点pから方向dに進む光路の長さ。惑星に当たるならbGround=true。
        double PathLength(const Atmosphere& atmosphere, const double p[3], const double d[3],
                          bool& bGround)
        {
            const double b = Dot(p, d);
            const double c = Dot(p, p);
            bGround = false;
            const double groundDiscriminant = b * b - (c - atmosphere.Bottom * atmosphere.Bottom);
            if (b < 0.0 && groundDiscriminant >= 0.0)
            {
                const double nearHit = -b - std::sqrt(groundDiscriminant);
                if (nearHit >= -1.0e-3)
                {
                    bGround = true;
                    return std::max(nearHit, 0.0);
                }
            }
            const double topDiscriminant = b * b - (c - atmosphere.Top * atmosphere.Top);
            return std::max(0.0, -b + std::sqrt(std::max(topDiscriminant, 0.0)));
        }

        // 高度の密度。オゾンは25 kmを中心に±15 kmのテント形。
        void SampleDensity(const Atmosphere& atmosphere, const double p[3],
                           double& rayleigh, double& mie, double& ozone)
        {
            const double altitude = std::max(std::sqrt(Dot(p, p)) - atmosphere.Bottom, 0.0);
            rayleigh = std::exp(-altitude / atmosphere.RayleighHeight);
            mie = std::exp(-altitude / atmosphere.MieHeight);
            ozone = std::max(0.0, 1.0 - std::abs(altitude - 25000.0) / 15000.0);
        }

        // 点pから方向dへ大気の上端までの透過率。等間隔の中点で密度を直接積分する。
        void Transmittance(const Atmosphere& atmosphere, const double p[3], const double d[3],
                           int steps, double out[3])
        {
            bool bGround = false;
            const double length = PathLength(atmosphere, p, d, bGround);
            if (bGround)
            {
                out[0] = out[1] = out[2] = 0.0;
                return;
            }
            const double segment = length / steps;
            double rayleigh = 0.0;
            double mie = 0.0;
            double ozone = 0.0;
            for (int step = 0; step < steps; ++step)
            {
                const double t = (step + 0.5) * segment;
                const double q[3] = {p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t};
                double densityRayleigh = 0.0;
                double densityMie = 0.0;
                double densityOzone = 0.0;
                SampleDensity(atmosphere, q, densityRayleigh, densityMie, densityOzone);
                rayleigh += densityRayleigh * segment;
                mie += densityMie * segment;
                ozone += densityOzone * segment;
            }
            for (int channel = 0; channel < 3; ++channel)
            {
                out[channel] = std::exp(-(kRayleigh[channel] * rayleigh + kMieExtinction * mie +
                                          kOzone[channel] * ozone));
            }
        }

        // 多重散乱の格子の高度（地表の側を細かくする3乗の配置）。
        double PsiAltitude(const Atmosphere& atmosphere, int row)
        {
            const double v = static_cast<double>(row) / (kPsiAltitudes - 1);
            return (atmosphere.Top - atmosphere.Bottom) * v * v * v;
        }

        double TableAltitude(const Atmosphere& atmosphere, int row)
        {
            const double v = static_cast<double>(row) / (kTableAltitudes - 1);
            return (atmosphere.Top - atmosphere.Bottom) * v * v * v;
        }

        // 高度と余弦の線形補間。高度は3乗の配置の逆写像で位置を求める。
        void LookupGrid(const NorvesLib::Core::Container::VariableArray<double>& table,
                        int altitudeCount, int cosineCount, double altitudeFraction,
                        double cosine, double out[3])
        {
            const double rowPosition =
                std::cbrt(std::clamp(altitudeFraction, 0.0, 1.0)) * (altitudeCount - 1);
            const double columnPosition = (std::clamp(cosine, -1.0, 1.0) * 0.5 + 0.5) *
                                          (cosineCount - 1);
            const int row = std::min(static_cast<int>(rowPosition), altitudeCount - 2);
            const int column = std::min(static_cast<int>(columnPosition), cosineCount - 2);
            const double rowWeight = rowPosition - row;
            const double columnWeight = columnPosition - column;
            for (int channel = 0; channel < 3; ++channel)
            {
                const auto at = [&](int r, int c)
                {
                    return table[(static_cast<size_t>(r) * cosineCount + c) * 3u + channel];
                };
                out[channel] = at(row, column) * (1.0 - rowWeight) * (1.0 - columnWeight) +
                               at(row, column + 1) * (1.0 - rowWeight) * columnWeight +
                               at(row + 1, column) * rowWeight * (1.0 - columnWeight) +
                               at(row + 1, column + 1) * rowWeight * columnWeight;
            }
        }

        void TableTransmittance(const Atmosphere& atmosphere, const double p[3],
                                const double sun[3], double out[3])
        {
            const double radius = std::sqrt(Dot(p, p));
            LookupGrid(atmosphere.TransmittanceTable, kTableAltitudes, kTableCosines,
                       (radius - atmosphere.Bottom) / (atmosphere.Top - atmosphere.Bottom),
                       Dot(p, sun) / radius, out);
        }

        double RayleighPhase(double cosine)
        {
            return 3.0 * (1.0 + cosine * cosine) / (16.0 * kPi);
        }

        double MiePhase(double cosine, double g)
        {
            const double denominator = std::max(1.0 + g * g - 2.0 * g * cosine, 1.0e-4);
            return (1.0 - g * g) / (4.0 * kPi * denominator * std::sqrt(denominator));
        }

        // 光路に沿った散乱（等間隔の中点、区間内は媒質一定の (1 - e^(-σt·Δt)) / σt）。
        // bSecondOrder: 等方の位相関数・太陽の透過率は表・多重散乱なし（Ψ の計算用）。
        void MarchScattering(const Atmosphere& atmosphere, const double origin[3],
                             const double d[3], const double sun[3], int steps,
                             bool bSecondOrder, double outInscattering[3],
                             double outTransfer[3], double outThroughput[3], bool& bGround,
                             double outGroundNormal[3])
        {
            const double length = PathLength(atmosphere, origin, d, bGround);
            const double cosineToSun = Dot(d, sun);
            const double rayleighPhase = bSecondOrder ? 1.0 / (4.0 * kPi) : RayleighPhase(cosineToSun);
            const double miePhase = bSecondOrder ? 1.0 / (4.0 * kPi)
                                                 : MiePhase(cosineToSun, atmosphere.Anisotropy);
            const double segment = length / steps;
            for (int channel = 0; channel < 3; ++channel)
            {
                outInscattering[channel] = 0.0;
                outTransfer[channel] = 0.0;
                outThroughput[channel] = 1.0;
            }
            for (int step = 0; step < steps; ++step)
            {
                const double t = (step + 0.5) * segment;
                const double q[3] = {origin[0] + d[0] * t, origin[1] + d[1] * t, origin[2] + d[2] * t};
                double densityRayleigh = 0.0;
                double densityMie = 0.0;
                double densityOzone = 0.0;
                SampleDensity(atmosphere, q, densityRayleigh, densityMie, densityOzone);
                double sunTransmittance[3];
                double psi[3] = {0.0, 0.0, 0.0};
                if (bSecondOrder)
                {
                    TableTransmittance(atmosphere, q, sun, sunTransmittance);
                }
                else
                {
                    Transmittance(atmosphere, q, sun, kSunTransmittanceSteps, sunTransmittance);
                    const double radius = std::sqrt(Dot(q, q));
                    LookupGrid(atmosphere.PsiTable, kPsiAltitudes, kPsiCosines,
                               (radius - atmosphere.Bottom) / (atmosphere.Top - atmosphere.Bottom),
                               Dot(q, sun) / radius, psi);
                }
                for (int channel = 0; channel < 3; ++channel)
                {
                    const double rayleigh = kRayleigh[channel] * densityRayleigh;
                    const double mie = kMieScattering * densityMie;
                    const double extinction = rayleigh + kMieExtinction * densityMie +
                                              kOzone[channel] * densityOzone;
                    const double source = (rayleigh * rayleighPhase + mie * miePhase) *
                                              sunTransmittance[channel] +
                                          (rayleigh + mie) * psi[channel];
                    const double stepTransmittance = std::exp(-extinction * segment);
                    const double integral = extinction > 0.0
                        ? -std::expm1(-extinction * segment) / extinction
                        : segment;
                    outInscattering[channel] += outThroughput[channel] * source * integral;
                    outTransfer[channel] += outThroughput[channel] * (rayleigh + mie) * integral;
                    outThroughput[channel] *= stepTransmittance;
                }
            }
            if (bGround)
            {
                const double hit[3] = {origin[0] + d[0] * length, origin[1] + d[1] * length,
                                       origin[2] + d[2] * length};
                const double radius = std::sqrt(Dot(hit, hit));
                for (int axis = 0; axis < 3; ++axis)
                {
                    outGroundNormal[axis] = hit[axis] / radius;
                }
            }
        }

        Atmosphere Build(const SkyAtmosphereParameters& parameters)
        {
            const SkyAtmosphereParameters sanitized = SanitizeSkyAtmosphereParameters(parameters);
            Atmosphere atmosphere;
            atmosphere.Bottom = sanitized.PlanetRadiusMeters;
            atmosphere.Top = sanitized.PlanetRadiusMeters + sanitized.AtmosphereHeightMeters;
            atmosphere.RayleighHeight = sanitized.RayleighScaleHeightMeters;
            atmosphere.MieHeight = sanitized.MieScaleHeightMeters;
            atmosphere.Anisotropy = sanitized.MieAnisotropy;
            atmosphere.Albedo[0] = sanitized.GroundAlbedo.x;
            atmosphere.Albedo[1] = sanitized.GroundAlbedo.y;
            atmosphere.Albedo[2] = sanitized.GroundAlbedo.z;
            const NorvesLib::Math::Vector3 sun = MakeSunDirectionFromAltitudeAzimuth(
                sanitized.SunAltitudeDegrees, sanitized.SunAzimuthDegrees);
            atmosphere.Sun[0] = sun.x;
            atmosphere.Sun[1] = sun.y;
            atmosphere.Sun[2] = sun.z;
            atmosphere.SunIrradiance = sanitized.SunLuminanceNits * SolarDiskSolidAngleSteradians;

            // 多重散乱の格子の計算にだけ使う、太陽への透過率の細かい表。
            atmosphere.TransmittanceTable.resize(
                static_cast<size_t>(kTableAltitudes) * kTableCosines * 3u, 0.0);
            for (int row = 0; row < kTableAltitudes; ++row)
            {
                const double p[3] = {0.0, atmosphere.Bottom + TableAltitude(atmosphere, row), 0.0};
                for (int column = 0; column < kTableCosines; ++column)
                {
                    const double cosine = -1.0 + 2.0 * column / (kTableCosines - 1);
                    const double d[3] = {std::sqrt(std::max(0.0, 1.0 - cosine * cosine)), cosine, 0.0};
                    double transmittance[3];
                    Transmittance(atmosphere, p, d, kTableTransmittanceSteps, transmittance);
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        atmosphere.TransmittanceTable[
                            (static_cast<size_t>(row) * kTableCosines + column) * 3u + channel] =
                            transmittance[channel];
                    }
                }
            }

            atmosphere.PsiTable.resize(static_cast<size_t>(kPsiAltitudes) * kPsiCosines * 3u, 0.0);
            for (int row = 0; row < kPsiAltitudes; ++row)
            {
                const double origin[3] = {0.0, atmosphere.Bottom + PsiAltitude(atmosphere, row), 0.0};
                for (int column = 0; column < kPsiCosines; ++column)
                {
                    const double sunCosine = -1.0 + 2.0 * column / (kPsiCosines - 1);
                    const double psiSun[3] = {std::sqrt(std::max(0.0, 1.0 - sunCosine * sunCosine)),
                                              sunCosine, 0.0};
                    double secondOrder[3] = {0.0, 0.0, 0.0};
                    double transfer[3] = {0.0, 0.0, 0.0};
                    for (int polar = 0; polar < kPsiDirectionSqrt; ++polar)
                    {
                        const double cosine = 1.0 - 2.0 * (polar + 0.5) / kPsiDirectionSqrt;
                        const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
                        for (int azimuth = 0; azimuth < kPsiDirectionSqrt; ++azimuth)
                        {
                            const double phi = 2.0 * kPi * (azimuth + 0.5) / kPsiDirectionSqrt;
                            const double d[3] = {sine * std::cos(phi), cosine, sine * std::sin(phi)};
                            double inscattering[3];
                            double stepTransfer[3];
                            double throughput[3];
                            double normal[3] = {0.0, 1.0, 0.0};
                            bool bGround = false;
                            MarchScattering(atmosphere, origin, d, psiSun, kPsiSteps, true,
                                            inscattering, stepTransfer, throughput, bGround, normal);
                            double groundLight[3] = {0.0, 0.0, 0.0};
                            if (bGround)
                            {
                                const double groundPoint[3] = {normal[0] * atmosphere.Bottom,
                                                               normal[1] * atmosphere.Bottom,
                                                               normal[2] * atmosphere.Bottom};
                                double groundSun[3];
                                TableTransmittance(atmosphere, groundPoint, psiSun, groundSun);
                                const double cosineAtGround = std::max(Dot(normal, psiSun), 0.0);
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    groundLight[channel] = groundSun[channel] * cosineAtGround *
                                                           atmosphere.Albedo[channel] / kPi;
                                }
                            }
                            for (int channel = 0; channel < 3; ++channel)
                            {
                                secondOrder[channel] += inscattering[channel] +
                                                        throughput[channel] * groundLight[channel];
                                transfer[channel] += stepTransfer[channel];
                            }
                        }
                    }
                    const double weight = 1.0 / (kPsiDirectionSqrt * kPsiDirectionSqrt);
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        atmosphere.PsiTable[(static_cast<size_t>(row) * kPsiCosines + column) * 3u +
                                            channel] =
                            secondOrder[channel] * weight / (1.0 - transfer[channel] * weight);
                    }
                }
            }

            // 地表の水平面の空の照度（太陽の単位照度あたり）。
            const double ground[3] = {0.0, atmosphere.Bottom, 0.0};
            for (int elevation = 0; elevation < kIrradianceElevations; ++elevation)
            {
                const double sine = (elevation + 0.5) / kIrradianceElevations;
                const double cosine = std::sqrt(1.0 - sine * sine);
                for (int azimuth = 0; azimuth < kIrradianceAzimuths; ++azimuth)
                {
                    const double phi = 2.0 * kPi * (azimuth + 0.5) / kIrradianceAzimuths;
                    const double d[3] = {cosine * std::cos(phi), sine, cosine * std::sin(phi)};
                    double inscattering[3];
                    double transfer[3];
                    double throughput[3];
                    double normal[3];
                    bool bGround = false;
                    MarchScattering(atmosphere, ground, d, atmosphere.Sun, kIrradianceSteps, false,
                                    inscattering, transfer, throughput, bGround, normal);
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        atmosphere.GroundSkyIrradiance[channel] +=
                            inscattering[channel] * sine * 2.0 * kPi /
                            (kIrradianceElevations * kIrradianceAzimuths);
                    }
                }
            }
            return atmosphere;
        }

        // 観測点（地表から SkyAtmosphereModel::ObserverAltitudeMeters）から見た放射輝度。
        NorvesLib::Math::Vector3 ViewRadiance(const Atmosphere& atmosphere,
                                              const NorvesLib::Math::Vector3& view)
        {
            const double origin[3] = {
                0.0, atmosphere.Bottom + SkyAtmosphereModel::ObserverAltitudeMeters, 0.0};
            const double length = std::sqrt(view.x * view.x + view.y * view.y + view.z * view.z);
            const double d[3] = {view.x / length, view.y / length, view.z / length};
            double inscattering[3];
            double transfer[3];
            double throughput[3];
            double normal[3] = {0.0, 1.0, 0.0};
            bool bGround = false;
            MarchScattering(atmosphere, origin, d, atmosphere.Sun, kViewSteps, false,
                            inscattering, transfer, throughput, bGround, normal);
            if (bGround)
            {
                const double groundPoint[3] = {normal[0] * atmosphere.Bottom,
                                               normal[1] * atmosphere.Bottom,
                                               normal[2] * atmosphere.Bottom};
                double groundSun[3];
                Transmittance(atmosphere, groundPoint, atmosphere.Sun, kTransmittanceSteps, groundSun);
                const double cosineAtGround = std::max(Dot(normal, atmosphere.Sun), 0.0);
                for (int channel = 0; channel < 3; ++channel)
                {
                    inscattering[channel] += throughput[channel] * atmosphere.Albedo[channel] / kPi *
                                             (groundSun[channel] * cosineAtGround +
                                              atmosphere.GroundSkyIrradiance[channel]);
                }
            }
            return NorvesLib::Math::Vector3(
                static_cast<float>(inscattering[0] * atmosphere.SunIrradiance),
                static_cast<float>(inscattering[1] * atmosphere.SunIrradiance),
                static_cast<float>(inscattering[2] * atmosphere.SunIrradiance));
        }

        // 地表から太陽への透過率（細かい刻みの直接積分）。
        NorvesLib::Math::Vector3 SunGroundTransmittance(const Atmosphere& atmosphere)
        {
            const double ground[3] = {0.0, atmosphere.Bottom, 0.0};
            double transmittance[3];
            Transmittance(atmosphere, ground, atmosphere.Sun, kTransmittanceSteps, transmittance);
            return NorvesLib::Math::Vector3(static_cast<float>(transmittance[0]),
                                            static_cast<float>(transmittance[1]),
                                            static_cast<float>(transmittance[2]));
        }
    } // namespace Reference

    float Luminance(const NorvesLib::Math::Vector3& value)
    {
        return 0.2126f * value.x + 0.7152f * value.y + 0.0722f * value.z;
    }

    NorvesLib::Math::Vector3 DirectionFromElevationAzimuth(float elevationDegrees,
                                                           float azimuthDegrees)
    {
        const float elevation = elevationDegrees * 3.14159265358979323846f / 180.0f;
        const float azimuth = azimuthDegrees * 3.14159265358979323846f / 180.0f;
        return NorvesLib::Math::Vector3(std::cos(elevation) * std::cos(azimuth),
                                        std::sin(elevation),
                                        std::cos(elevation) * std::sin(azimuth));
    }

    bool ChannelsWithin(const NorvesLib::Math::Vector3& value,
                        const NorvesLib::Math::Vector3& reference,
                        float relativeError)
    {
        return std::abs(value.x - reference.x) <= relativeError * std::abs(reference.x) &&
               std::abs(value.y - reference.y) <= relativeError * std::abs(reference.y) &&
               std::abs(value.z - reference.z) <= relativeError * std::abs(reference.z);
    }

    void PrintVector(const char* label, const NorvesLib::Math::Vector3& value)
    {
        std::cout << label << " = (" << value.x << ", " << value.y << ", " << value.z << ")\n";
    }

    // 空の放射輝度を、テスト側の独立な細かい積分と比べる（太陽 仰角40°と3°、地平線より下を含む）。
    void TestSkyRadianceMatchesIndependentIntegral()
    {
        struct DirectionCase
        {
            const char* Name;
            float Elevation;
            float Azimuth;
        };
        const DirectionCase directions[] = {
            {"zenith", 90.0f, 0.0f},
            {"sun_side_30", 30.0f, 0.0f},
            {"anti_sun_30", 30.0f, 180.0f},
            {"side_10", 10.0f, 90.0f},
            {"anti_sun_horizon_1", 1.0f, 180.0f},
            {"sun_side_horizon_1", 1.0f, 0.0f},
            {"below_horizon_5", -5.0f, 180.0f},
            {"nadir", -90.0f, 0.0f},
        };
        const float sunAltitudes[] = {40.0f, 3.0f};
        for (const float sunAltitude : sunAltitudes)
        {
            SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
            parameters.bEnabled = true;
            parameters.SunAltitudeDegrees = sunAltitude;
            parameters.SunAzimuthDegrees = 0.0f;
            const SkyAtmosphereModel model(parameters);
            const Reference::Atmosphere reference = Reference::Build(parameters);
            for (const DirectionCase& direction : directions)
            {
                const NorvesLib::Math::Vector3 view =
                    DirectionFromElevationAzimuth(direction.Elevation, direction.Azimuth);
                const SkyRadianceSample sample = model.EvaluateViewRadiance(view);
                const NorvesLib::Math::Vector3 expected = Reference::ViewRadiance(reference, view);
                std::cout << "sun" << sunAltitude << " " << direction.Name << ": model=("
                          << sample.Radiance.x << ", " << sample.Radiance.y << ", "
                          << sample.Radiance.z << ") reference=(" << expected.x << ", "
                          << expected.y << ", " << expected.z << ")\n";
                Expect(sample.bValid, "空の評価は全方向で有効");
                // モデルは32の2乗配置の刻みと表の補間、参照は800の等間隔の刻みと直接積分。差は5%以内。
                Expect(ChannelsWithin(sample.Radiance, expected, 0.05f),
                       "空の放射輝度が独立な細かい数値積分と5%以内で一致する");
            }
        }
    }

    // LUT・IBLが引く sky-view の表の補間は、直接の評価と3%以内で一致する（地平線の境目の0.3°を除く）。
    void TestSkyViewTableMatchesDirectEvaluation()
    {
        const float sunAltitudes[] = {45.0f, 3.0f};
        const float elevations[] = {-80.0f, -20.0f, -3.0f, -1.0f, 0.5f, 1.5f, 4.0f, 8.0f,
                                    20.0f, 38.0f, 44.0f, 47.0f, 60.0f, 89.0f};
        const float azimuthOffsets[] = {0.0f, 1.0f, 2.5f, 7.0f, 20.0f, 60.0f, 100.0f,
                                        150.0f, 179.0f, -35.0f, -120.0f};
        for (const float sunAltitude : sunAltitudes)
        {
            SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
            parameters.bEnabled = true;
            parameters.SunAltitudeDegrees = sunAltitude;
            parameters.SunAzimuthDegrees = 30.0f;
            SkyAtmosphereModel model(parameters);
            model.BuildSkyViewTable();
            float maxError = 0.0f;
            for (const float elevation : elevations)
            {
                for (const float offset : azimuthOffsets)
                {
                    const NorvesLib::Math::Vector3 view =
                        DirectionFromElevationAzimuth(elevation, 30.0f + offset);
                    const SkyRadianceSample direct = model.EvaluateViewRadiance(view);
                    const SkyRadianceSample table = model.SampleSkyView(view);
                    Expect(table.bValid && table.MeanSunTransmittance == direct.MeanSunTransmittance,
                           "sky-view の表の値は有効で、太陽の透過率は直接の評価と同じ");
                    const float error = std::max({
                        std::abs(table.Radiance.x - direct.Radiance.x) / direct.Radiance.x,
                        std::abs(table.Radiance.y - direct.Radiance.y) / direct.Radiance.y,
                        std::abs(table.Radiance.z - direct.Radiance.z) / direct.Radiance.z});
                    maxError = std::max(maxError, error);
                    if (error > 0.03f)
                    {
                        std::cout << "sky-view sun" << sunAltitude << " elevation=" << elevation
                                  << " offset=" << offset << " error=" << error << "\n";
                    }
                }
            }
            std::cout << "sky-view sun" << sunAltitude << " max_relative_error=" << maxError << "\n";
            Expect(maxError <= 0.03f, "sky-view の表の補間は直接の評価と3%以内");
        }
    }

    // 太陽 仰角40°の空が晴天の物理的な範囲に入る。
    void TestClearSkyPhysicalRanges()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 40.0f;
        parameters.SunAzimuthDegrees = 0.0f;
        const SkyAtmosphereModel model(parameters);

        const NorvesLib::Math::Vector3 zenith =
            model.EvaluateViewRadiance(NorvesLib::Math::Vector3::UnitY).Radiance;
        PrintVector("sun40 zenith", zenith);
        std::cout << "sun40 zenith_luminance = " << Luminance(zenith) << "\n";
        Expect(zenith.z > zenith.y && zenith.y > zenith.x, "天頂は青（B > G > R）");
        Expect(Luminance(zenith) >= 2000.0f && Luminance(zenith) <= 8000.0f,
               "天頂の輝度は約2000から8000 nits");

        // 水平面の空の照度を上半球の積分で求め、太陽を含む全天の照度に対する割合を見る。
        constexpr int elevationCount = 16;
        constexpr int azimuthCount = 32;
        NorvesLib::Math::Vector3 skyIlluminance = NorvesLib::Math::Vector3::Zero;
        for (int elevation = 0; elevation < elevationCount; ++elevation)
        {
            const float sine = (elevation + 0.5f) / elevationCount;
            const float elevationDegrees = std::asin(sine) * 180.0f / 3.14159265358979323846f;
            for (int azimuth = 0; azimuth < azimuthCount; ++azimuth)
            {
                const float azimuthDegrees = 360.0f * (azimuth + 0.5f) / azimuthCount;
                const NorvesLib::Math::Vector3 radiance = model.EvaluateViewRadiance(
                    DirectionFromElevationAzimuth(elevationDegrees, azimuthDegrees)).Radiance;
                const float weight = sine * 2.0f * 3.14159265358979323846f /
                                     (elevationCount * azimuthCount);
                skyIlluminance = skyIlluminance + radiance * weight;
            }
        }
        const NorvesLib::Math::Vector3 sunIlluminance = ComputeSunGroundIlluminance(parameters);
        const float sunHorizontal = Luminance(sunIlluminance) *
                                    std::sin(40.0f * 3.14159265358979323846f / 180.0f);
        const float skyFraction =
            Luminance(skyIlluminance) / (Luminance(skyIlluminance) + sunHorizontal);
        PrintVector("sun40 sky_illuminance", skyIlluminance);
        std::cout << "sun40 sun_horizontal_illuminance = " << sunHorizontal
                  << " sky_fraction = " << skyFraction << "\n";
        Expect(skyFraction >= 0.10f && skyFraction <= 0.30f,
               "水平面の空の照度は太陽を含む全天の照度の10%から30%");
        // 地面を照らす空の照度（観測点は地表）も上の積分（観測点は100 m）と5%以内で揃う。
        Expect(ChannelsWithin(model.GetGroundSkyIlluminance(), skyIlluminance, 0.05f),
               "地面を照らす空の照度が上半球の積分と揃う");

        const NorvesLib::Math::Vector3 antiHorizon =
            model.EvaluateViewRadiance(DirectionFromElevationAzimuth(1.0f, 180.0f)).Radiance;
        PrintVector("sun40 anti_horizon", antiHorizon);
        Expect(antiHorizon.x / antiHorizon.z >= 0.5f && antiHorizon.x / antiHorizon.z <= 1.2f,
               "太陽と反対側の地平線は白っぽい（R/B が0.5から1.2）");
        Expect(zenith.x / zenith.z < antiHorizon.x / antiHorizon.z,
               "地平線は天頂より色が薄い");
        Expect(Luminance(antiHorizon) > Luminance(zenith), "地平線は天頂より明るい");

        const NorvesLib::Math::Vector3 belowDirections[] = {
            DirectionFromElevationAzimuth(-5.0f, 180.0f),
            DirectionFromElevationAzimuth(-30.0f, 90.0f),
            NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f)};
        for (const NorvesLib::Math::Vector3& direction : belowDirections)
        {
            const SkyRadianceSample below = model.EvaluateViewRadiance(direction);
            ExpectFiniteVector(below.Radiance, "地平線より下の値は有限");
            Expect(below.bValid && below.Radiance.x > 0.0f && below.Radiance.y > 0.0f &&
                       below.Radiance.z > 0.0f,
                   "地平線より下は0でない（地面の反射と手前の散乱）");
        }
        PrintVector("sun40 nadir", model.EvaluateViewRadiance(
                                       NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f)).Radiance);
    }

    // 太陽 仰角3°では太陽の周りが橙になる。
    void TestLowSunIsOrange()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 3.0f;
        parameters.SunAzimuthDegrees = 0.0f;
        const SkyAtmosphereModel model(parameters);
        const NorvesLib::Math::Vector3 nearSun =
            model.EvaluateViewRadiance(DirectionFromElevationAzimuth(5.0f, 0.0f)).Radiance;
        const NorvesLib::Math::Vector3 sunSideHorizon =
            model.EvaluateViewRadiance(DirectionFromElevationAzimuth(1.0f, 10.0f)).Radiance;
        PrintVector("sun3 near_sun", nearSun);
        PrintVector("sun3 sun_side_horizon", sunSideHorizon);
        Expect(nearSun.x > nearSun.y && nearSun.y > nearSun.z && nearSun.x > 2.0f * nearSun.z,
               "仰角3°の太陽の周りは橙（R > G > B、R が B の2倍超）");
        Expect(sunSideHorizon.x > sunSideHorizon.y && sunSideHorizon.y > sunSideHorizon.z,
               "仰角3°の太陽側の地平線は橙");
        const NorvesLib::Math::Vector3 transmittance = model.GetSunGroundTransmittance();
        Expect(transmittance.x > transmittance.y && transmittance.y > transmittance.z,
               "低い太陽の透過光は赤い");
    }

    // 許される最小の尺度高さでは上空の密度が0へ落ち、消散係数0の区間ができる。
    // その区間でも積分が0/0にならず、天頂・真下・地表の照度が有限で有効なままであること。
    void TestZeroExtinctionSegmentsStayFinite()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 40.0f;
        parameters.RayleighScaleHeightMeters = 100.0f;
        parameters.MieScaleHeightMeters = 100.0f;
        const SkyAtmosphereParameters sanitized = SanitizeSkyAtmosphereParameters(parameters);
        Expect(sanitized.RayleighScaleHeightMeters == 100.0f &&
                   sanitized.MieScaleHeightMeters == 100.0f,
               "尺度高さ100 mは許される範囲に入る");

        const SkyAtmosphereModel model(parameters);
        const SkyRadianceSample zenith =
            model.EvaluateViewRadiance(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        ExpectFiniteVector(zenith.Radiance, "小さな尺度高さでも天頂は有限");
        Expect(zenith.bValid, "小さな尺度高さでも天頂は有効");

        const SkyRadianceSample nadir =
            model.EvaluateViewRadiance(NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f));
        ExpectFiniteVector(nadir.Radiance, "小さな尺度高さでも真下は有限");
        Expect(nadir.bValid && nadir.Radiance.x > 0.0f && nadir.Radiance.y > 0.0f &&
                   nadir.Radiance.z > 0.0f,
               "小さな尺度高さでも真下は0でない地面を返す");

        const NorvesLib::Math::Vector3 groundSky = model.GetGroundSkyIlluminance();
        ExpectFiniteVector(groundSky, "小さな尺度高さでも地表の空の照度は有限");
        Expect(groundSky.x >= 0.0f && groundSky.y >= 0.0f && groundSky.z >= 0.0f,
               "小さな尺度高さでも地表の空の照度は負にならない");
        ExpectFiniteVector(ComputeSunGroundIlluminance(parameters),
                           "小さな尺度高さでも太陽の地表照度は有限");
        const SkyRadianceSample reference = EvaluateHillaireSkyReference(
            parameters, NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        Expect(reference.bValid, "小さな尺度高さでも参照の入口は有効");
        PrintVector("scale100 zenith", zenith.Radiance);
        PrintVector("scale100 nadir", nadir.Radiance);
    }

    void TestSunDiskPreExposureContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        const float ev15PreExposure = 1.0f / 32768.0f;
        const float ev14PreExposure = 1.0f / 16384.0f;

        Expect(NearlyEqual(ComputeSunDiskIrradiance(parameters), 109920.0f, 0.5f),
               "solar disk radiance is integrated over its documented solid angle");
        // R1 exposure uses 1 / (1.2 * 2^EV100); keep the EV labels aligned with
        // the camera exposure contract while the disk value remains in float.
        const float r1Ev15PreExposure = 1.0f / (1.2f * 32768.0f);
        const float r1Ev14PreExposure = 1.0f / (1.2f * 16384.0f);
        Expect(NearlyEqual(ComputeSunDiskPreExposedLuminance(
                               parameters, r1Ev15PreExposure),
                           40690.1f,
                           0.5f),
               "EV15 sun disk luminance is represented without FP16 saturation");
        Expect(!IsSunDiskWithinFp16SafetyRange(parameters, r1Ev14PreExposure),
               "EV14 identifies the nominal solar disk as outside the FP16 safety range");
        Expect(IsSunDiskWithinFp16SafetyRange(parameters, r1Ev15PreExposure),
               "EV15 is inside the FP16 safety range");
        Expect(NearlyEqual(ComputeSunDiskPreExposedLuminance(
                               parameters, ev15PreExposure),
                           48828.125f,
                           0.5f),
               "unscaled EV15 pre-exposure remains a valid direct disk value");
        Expect(ComputeSunDiskPreExposedLuminance(parameters, 0.0f) == 0.0f,
               "non-positive pre-exposure disables the disk value");
    }

    bool SameVector(const NorvesLib::Math::Vector3& lhs, const NorvesLib::Math::Vector3& rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }

    void TestSunGroundIlluminanceContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;

        // 地表の太陽の透過率は、球殻の大気を上端まで細かい刻みで直接積分した値と一致する。
        const float altitudes[] = {90.0f, 40.0f, 8.0f, 3.0f};
        for (const float altitude : altitudes)
        {
            parameters.SunAltitudeDegrees = altitude;
            const NorvesLib::Math::Vector3 transmittance = ComputeSunGroundTransmittance(parameters);
            const NorvesLib::Math::Vector3 expected =
                Reference::SunGroundTransmittance(Reference::Build(parameters));
            std::cout << "sun" << altitude << " ground_transmittance=(" << transmittance.x << ", "
                      << transmittance.y << ", " << transmittance.z << ") reference=("
                      << expected.x << ", " << expected.y << ", " << expected.z << ")\n";
            Expect(ChannelsWithin(transmittance, expected, 0.01f),
                   "地表の太陽の透過率が独立な直接積分と1%以内で一致する");
        }

        // 天頂の太陽は Rayleigh・Mie の光学的深さ（消散係数×尺度高さ）の指数減衰にほぼ等しい。
        parameters.SunAltitudeDegrees = 90.0f;
        const NorvesLib::Math::Vector3 zenith = ComputeSunGroundTransmittance(parameters);
        // オゾンの層の柱の厚さはテントの面積 15 km。
        const float mie = std::exp(-(1.0e-4f + 1.11e-5f) * 1200.0f);
        Expect(ChannelsWithin(zenith,
                              NorvesLib::Math::Vector3(
                                  std::exp(-5.802e-6f * 8000.0f - 0.650e-6f * 15000.0f) * mie,
                                  std::exp(-13.558e-6f * 8000.0f - 1.881e-6f * 15000.0f) * mie,
                                  std::exp(-33.100e-6f * 8000.0f - 0.085e-6f * 15000.0f) * mie),
                              0.002f),
               "天頂の太陽の透過率は平行大気の光学的深さの値に近い");

        // 低い太陽は長い光路で赤く暗くなる。
        parameters.SunAltitudeDegrees = 8.0f;
        const NorvesLib::Math::Vector3 low = ComputeSunGroundTransmittance(parameters);
        Expect(low.x > low.y && low.y > low.z && low.x < zenith.x && low.z > 0.0f,
               "low sun transmittance is reddened and dimmer than the zenith sun");

        // 空の評価が返す太陽の透過率・評価器の太陽の透過率と同じ値。
        const SkyRadianceSample sample = EvaluateHillaireSkyReference(
            parameters, NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        Expect(NearlyEqual((low.x + low.y + low.z) / 3.0f, sample.MeanSunTransmittance, 1.0e-6f),
               "ground sun transmittance equals the sky reference scattering source");
        Expect(SameVector(SkyAtmosphereModel(parameters).GetSunGroundTransmittance(), low),
               "空の評価器の太陽の透過率は地表の太陽の透過率と同じ");

        // 透過率LUTを埋める評価器の表は、直接の積分と光学的深さで1%以内（τ<1では0.01以内）で揃う。
        {
            const SkyAtmosphereModel model(parameters);
            const float altitudeFractions[] = {0.0f, 0.05f, 0.3f, 1.0f};
            const float cosines[] = {0.02f, 0.1f, 0.35f, 0.7f, 1.0f};
            const auto opticalDepthMatches = [](float table, float direct)
            {
                const float tableDepth = -std::log(table);
                const float directDepth = -std::log(direct);
                return std::abs(tableDepth - directDepth) <= 0.01f * std::max(1.0f, directDepth);
            };
            for (const float altitudeFraction : altitudeFractions)
            {
                for (const float cosine : cosines)
                {
                    const NorvesLib::Math::Vector3 direct =
                        ComputeAtmosphereTransmittance(parameters, altitudeFraction, cosine);
                    const NorvesLib::Math::Vector3 table =
                        model.GetTransmittance(altitudeFraction, cosine);
                    const bool bMatches = opticalDepthMatches(table.x, direct.x) &&
                                          opticalDepthMatches(table.y, direct.y) &&
                                          opticalDepthMatches(table.z, direct.z);
                    if (!bMatches)
                    {
                        std::cout << "transmittance_table altitude=" << altitudeFraction
                                  << " cosine=" << cosine << " table=(" << table.x << ", "
                                  << table.y << ", " << table.z << ") direct=(" << direct.x
                                  << ", " << direct.y << ", " << direct.z << ")" << std::endl;
                    }
                    Expect(bMatches, "透過率LUTの表が直接の積分と光学的深さで1%以内で揃う");
                }
            }
        }

        // 透過率LUTと同じ関数の地表・太陽の余弦の値。
        const float sunCosine = MakeSunDirectionFromAltitudeAzimuth(
            parameters.SunAltitudeDegrees, parameters.SunAzimuthDegrees).y;
        Expect(SameVector(ComputeAtmosphereTransmittance(parameters, 0.0f, sunCosine), low),
               "ground sun transmittance is the LUT formula at the ground altitude");
        const NorvesLib::Math::Vector3 ground =
            ComputeAtmosphereTransmittance(parameters, 0.0f, 0.2f);
        const NorvesLib::Math::Vector3 top =
            ComputeAtmosphereTransmittance(parameters, 1.0f, 0.2f);
        Expect(top.x > ground.x && top.y > ground.y && top.z > ground.z,
               "thinner air at altitude transmits more light");
        const NorvesLib::Math::Vector3 horizontal =
            ComputeAtmosphereTransmittance(parameters, 0.0f, 0.0f);
        Expect(horizontal.x > 0.0f && horizontal.x < ground.x,
               "球殻の大気では地表の水平の光路も有限で、斜めの光路より暗い");
        ExpectFiniteVector(ComputeAtmosphereTransmittance(
                               parameters,
                               std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()),
                           "non-finite altitude and cosine stay finite");

        // 地表照度は太陽円盤の照度×透過率。
        const NorvesLib::Math::Vector3 illuminance = ComputeSunGroundIlluminance(parameters);
        const float disk = ComputeSunDiskIrradiance(parameters);
        Expect(RelativeNearlyEqual(illuminance.x, disk * low.x, 1.0e-6f) &&
                   RelativeNearlyEqual(illuminance.y, disk * low.y, 1.0e-6f) &&
                   RelativeNearlyEqual(illuminance.z, disk * low.z, 1.0e-6f),
               "ground illuminance is the disk irradiance times the ground transmittance");

        parameters.bEnabled = false;
        Expect(SameVector(ComputeSunGroundTransmittance(parameters),
                          NorvesLib::Math::Vector3::Zero) &&
                   SameVector(ComputeSunGroundIlluminance(parameters),
                              NorvesLib::Math::Vector3::Zero),
               "a disabled sky has no ground sun");
    }

    void TestSkyViewRadianceContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 20.0f;
        const SkyAtmosphereModel model(parameters);
        // 地表から見た空（LUT・IBL・背景・PTが共有する値）は、評価器の値そのもの。
        const NorvesLib::Math::Vector3 directions[] = {
            NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f),
            NorvesLib::Math::VectorUtils::Normalize(NorvesLib::Math::Vector3(1.0f, 0.1f, 0.0f)),
            NorvesLib::Math::VectorUtils::Normalize(NorvesLib::Math::Vector3(0.3f, 0.5f, -0.8f)),
            NorvesLib::Math::VectorUtils::Normalize(NorvesLib::Math::Vector3(0.3f, -0.5f, -0.8f))};
        for (const NorvesLib::Math::Vector3& direction : directions)
        {
            const SkyRadianceSample reference = EvaluateHillaireSkyReference(parameters, direction);
            const SkyRadianceSample viewed = EvaluateSkyViewRadiance(parameters, direction);
            const SkyRadianceSample batched = model.EvaluateViewRadiance(direction);
            Expect(viewed.bValid && reference.bValid && batched.bValid &&
                       viewed.MeanSunTransmittance == reference.MeanSunTransmittance,
                   "地表から見た空は参照と同じ有効性と太陽の透過率を持つ");
            Expect(SameVector(viewed.Radiance, reference.Radiance) &&
                       SameVector(batched.Radiance, reference.Radiance),
                   "地表から見た空・参照の入口・評価器が同じ値を返す");
        }

        parameters.bEnabled = false;
        Expect(!EvaluateSkyViewRadiance(parameters, NorvesLib::Math::Vector3::UnitY).bValid,
               "無効な空は評価しない");
    }

    void TestSkySunLightContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 30.0f;
        parameters.SunAzimuthDegrees = 45.0f;

        LightProxy light;
        Expect(MakeSkySunLightProxy(parameters, light), "an enabled sky builds a sun light");
        Expect(IsSkySunLight(light) && light.LightId == SkySunLightId &&
                   light.Type == LightType::Directional && light.bCastShadows &&
                   light.IsValid(),
               "the sky sun is a valid shadow-casting directional light with the reserved id");
        const NorvesLib::Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(30.0f, 45.0f);
        Expect(NearlyEqual(light.DirectionX, -sunDirection.x, 1.0e-6f) &&
                   NearlyEqual(light.DirectionY, -sunDirection.y, 1.0e-6f) &&
                   NearlyEqual(light.DirectionZ, -sunDirection.z, 1.0e-6f),
               "the sky sun light travels away from the sun");
        const NorvesLib::Math::Vector3 illuminance = ComputeSunGroundIlluminance(parameters);
        Expect(RelativeNearlyEqual(light.ColorR * light.CanonicalIntensity, illuminance.x, 1.0e-5f) &&
                   RelativeNearlyEqual(light.ColorG * light.CanonicalIntensity, illuminance.y, 1.0e-5f) &&
                   RelativeNearlyEqual(light.ColorB * light.CanonicalIntensity, illuminance.z, 1.0e-5f),
               "the sky sun light carries the ground illuminance");
        Expect(NearlyEqual(0.2126f * light.ColorR + 0.7152f * light.ColorG + 0.0722f * light.ColorB,
                           1.0f, 1.0e-5f),
               "the sky sun light color has unit luminance");

        // 差し替えはシーンの灯を残し、空の太陽を1つだけ持つ。
        NorvesLib::Core::Container::VariableArray<LightProxy> lights;
        LightProxy sceneLight;
        sceneLight.LightId = 7u;
        lights.push_back(sceneLight);
        ReplaceSkySunLight(parameters, lights);
        ReplaceSkySunLight(parameters, lights);
        Expect(lights.size() == 2u && lights[0].LightId == 7u && IsSkySunLight(lights[1]),
               "replacing the sky sun keeps scene lights and never duplicates the sun");
        parameters.bEnabled = false;
        Expect(!MakeSkySunLightProxy(parameters, light), "a disabled sky builds no sun light");
        ReplaceSkySunLight(parameters, lights);
        Expect(lights.size() == 1u && lights[0].LightId == 7u,
               "disabling the sky removes the sky sun light");
    }
} // namespace

int main()
{
    TestSunDirectionCoordinateContract();
    TestSanitizationKeepsSnapshotFinite();
    TestSkyRadianceMatchesIndependentIntegral();
    TestSkyViewTableMatchesDirectEvaluation();
    TestClearSkyPhysicalRanges();
    TestLowSunIsOrange();
    TestZeroExtinctionSegmentsStayFinite();
    TestSunDiskPreExposureContract();
    TestSunGroundIlluminanceContract();
    TestSkyViewRadianceContract();
    TestSkySunLightContract();

    if (g_FailureCount != 0)
    {
        std::cout << "SkyAtmosphereModelTest failures: " << g_FailureCount << "\n";
        return 1;
    }

    std::cout << "SkyAtmosphereModelTest: PASS\n";
    return 0;
}
