#include "Rendering/Wind.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float ValidRangeOrDefault(float value, float minimum, float maximum, float fallback)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum ? value : fallback;
        }

        float WindHashToUnit(int32_t cell, uint32_t seed)
        {
            return static_cast<float>(WindHash(static_cast<uint32_t>(cell) ^ seed) >> 8) *
                   (1.0f / 16777216.0f);
        }

        float WindGustArgument(float dirX, float dirZ, float posX, float posZ, float time,
                               float scale, float frequency)
        {
            return (posX * dirX + posZ * dirZ) / scale - time * frequency;
        }
    } // namespace

    WindParameters MakeDefaultWindParameters()
    {
        return WindParameters{};
    }

    WindParameters SanitizeWindParameters(const WindParameters& in)
    {
        WindParameters result = MakeDefaultWindParameters();
        result.bEnabled = in.bEnabled;

        if (std::isfinite(in.DirectionYawDegrees))
        {
            result.DirectionYawDegrees = std::fmod(in.DirectionYawDegrees, 360.0f);
            if (result.DirectionYawDegrees < 0.0f)
            {
                result.DirectionYawDegrees += 360.0f;
            }
            if (result.DirectionYawDegrees >= 360.0f)
            {
                result.DirectionYawDegrees = 0.0f;
            }
        }

        result.BaseSpeed = ValidRangeOrDefault(in.BaseSpeed, 0.0f, 100.0f, result.BaseSpeed);
        result.GustStrength = ValidRangeOrDefault(in.GustStrength, 0.0f, 1.0f, result.GustStrength);
        if (std::isfinite(in.GustFrequencyHz) && in.GustFrequencyHz > 0.0f && in.GustFrequencyHz <= 10.0f)
        {
            result.GustFrequencyHz = std::round(in.GustFrequencyHz * 4096.0f) / 4096.0f;
            if (result.GustFrequencyHz == 0.0f)
            {
                result.GustFrequencyHz = 1.0f / 4096.0f;
            }
        }
        result.GustScaleMeters = ValidRangeOrDefault(in.GustScaleMeters, 0.1f, 10000.0f, result.GustScaleMeters);
        result.Turbulence = ValidRangeOrDefault(in.Turbulence, 0.0f, 1.0f, result.Turbulence);
        return result;
    }

    float WindClockSeconds(double gameTotalTime)
    {
        return static_cast<float>(std::fmod(gameTotalTime, WindClockWrapSeconds));
    }

    void WindDirectionXZ(const WindParameters& p, float& outX, float& outZ)
    {
        const float radians = p.DirectionYawDegrees * (Math::Constants::PI / 180.0f);
        outX = std::cos(radians);
        outZ = std::sin(radians);
    }

    uint32_t WindHash(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    float WindValueNoise1D(float x, uint32_t seed)
    {
        const float c = std::floor(x);
        const float f = x - c;
        const int32_t i = static_cast<int32_t>(c);
        const float a = WindHashToUnit(i, seed);
        const float b = WindHashToUnit(static_cast<int32_t>(static_cast<uint32_t>(i) + 1u), seed);
        const float u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
        return a + (b - a) * u;
    }

    float WindGustNoise(const WindParameters& p, float dirX, float dirZ, float posX, float posZ, float time)
    {
        const float arg = WindGustArgument(dirX, dirZ, posX, posZ, time, p.GustScaleMeters, p.GustFrequencyHz);
        return WindValueNoise1D(arg, 0u);
    }

    Math::Vector3 EvaluateWind(const WindParameters& p, const Math::Vector3& position, float time)
    {
        if (!p.bEnabled)
        {
            return Math::Vector3(0.0f, 0.0f, 0.0f);
        }

        float dirX = 0.0f;
        float dirZ = 0.0f;
        WindDirectionXZ(p, dirX, dirZ);
        const float arg = WindGustArgument(dirX, dirZ, position.x, position.z, time,
                                           p.GustScaleMeters, p.GustFrequencyHz);
        const float gust = WindValueNoise1D(arg, 0u);
        const float turb = WindValueNoise1D(arg * 3.1f, WindTurbulenceSeed);
        const float speed = p.BaseSpeed * (1.0f + p.GustStrength * (gust * 2.0f - 1.0f));
        const float side = p.BaseSpeed * p.Turbulence * (turb * 2.0f - 1.0f);
        return Math::Vector3(dirX * speed - dirZ * side, 0.0f, dirZ * speed + dirX * side);
    }
} // namespace NorvesLib::Core::Rendering
