// CPU の Rendering/Wind.h と同じ風の式。式を変えるときは CPU と GLSL の両方を同時に変える。
#ifndef NORVES_WIND_GLSL
#define NORVES_WIND_GLSL

uint WindHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float WindHashToUnit(int cell, uint seed)
{
    return float(WindHash(uint(cell) ^ seed) >> 8) * (1.0 / 16777216.0);
}

float WindValueNoise1D(float x, uint seed)
{
    float c = floor(x);
    float f = x - c;
    int i = int(c);
    float a = WindHashToUnit(i, seed);
    float b = WindHashToUnit(int(uint(i) + 1u), seed);
    float u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return a + (b - a) * u;
}

float WindGustArgument(vec2 positionXZ, float t, vec2 direction, float gustScaleMeters, float gustFrequencyHz)
{
    return (positionXZ.x * direction.x + positionXZ.y * direction.y) / gustScaleMeters - t * gustFrequencyHz;
}

float WindGustNoise(vec2 positionXZ, float t, vec2 direction, float gustScaleMeters, float gustFrequencyHz)
{
    float arg = WindGustArgument(positionXZ, t, direction, gustScaleMeters, gustFrequencyHz);
    return WindValueNoise1D(arg, 0u);
}

vec3 WindVelocity(vec3 position, float t, vec2 direction, float baseSpeed, float gustStrength,
                  float gustFrequencyHz, float gustScaleMeters, float turbulence)
{
    float arg = WindGustArgument(position.xz, t, direction, gustScaleMeters, gustFrequencyHz);
    float gust = WindValueNoise1D(arg, 0u);
    float turb = WindValueNoise1D(arg * 3.1, 0x68bc21ebu);
    float speed = baseSpeed * (1.0 + gustStrength * (gust * 2.0 - 1.0));
    float side = baseSpeed * turbulence * (turb * 2.0 - 1.0);
    return vec3(direction.x * speed - direction.y * side, 0.0, direction.y * speed + direction.x * side);
}

#endif // NORVES_WIND_GLSL
