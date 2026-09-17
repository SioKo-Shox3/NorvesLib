#version 450

layout(location = 0) in vec2 fragUV;

layout(std140, set = 0, binding = 0) uniform SkyAtmosphereParams
{
    vec4 sunDirection;
    vec4 sunDiskPreExposed;
    vec4 preExposure;
} params;

layout(set = 0, binding = 1) uniform sampler2D transmittanceLut;
layout(set = 0, binding = 2) uniform sampler2D skyRadianceLut;

layout(location = 0) out vec4 outColor;

vec3 DirectionFromEquirectangular(vec2 uv)
{
    const float PI = 3.14159265359;
    float longitude = (uv.x - 0.5) * 2.0 * PI;
    float latitude = (uv.y - 0.5) * PI;
    float horizontal = cos(latitude);
    return vec3(horizontal * cos(longitude),
                -sin(latitude),
                horizontal * sin(longitude));
}

void main()
{
    vec3 direction = DirectionFromEquirectangular(fragUV);
    vec2 transmittanceUV = vec2(clamp(0.5 + 0.5 * dot(direction,
                                                        normalize(params.sunDirection.xyz)),
                                      0.0,
                                      1.0),
                                clamp(0.5 + 0.5 * direction.y,
                                      0.0,
                                      1.0));
    vec3 transmittance = textureLod(transmittanceLut, transmittanceUV, 0.0).rgb;
    vec4 radiance = textureLod(skyRadianceLut, fragUV, 0.0);
    vec3 safeRadiance = max(radiance.rgb, vec3(0.0));
    vec3 sunDisk = params.sunDiskPreExposed.rgb * radiance.a;
    outColor = vec4(safeRadiance * max(transmittance, vec3(0.0)) + sunDisk, 1.0);
}
