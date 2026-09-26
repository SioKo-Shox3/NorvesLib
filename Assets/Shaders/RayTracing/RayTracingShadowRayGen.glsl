// 不透明な方向光遮蔽をピクセルごとに生成する。
#version 460
#extension GL_EXT_ray_tracing : require

#include "Common/RayTracingInstanceMask.glsl"

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1) uniform sampler2D gbufferDepth;
layout(set = 0, binding = 2) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 3, std140) uniform RayTracingShadowParameters
{
    mat4 inverseViewProjection;
    vec4 lightDirectionAndBias;
} params;
layout(set = 0, binding = 4, rgba8) uniform writeonly image2D visibilityImage;

layout(location = 0) rayPayloadEXT uint visibility;

void StoreVisibility(ivec2 pixel, float value)
{
    imageStore(visibilityImage, pixel, vec4(value, value, value, 1.0));
}

void main()
{
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    ivec2 extent = imageSize(visibilityImage);
    if (any(greaterThanEqual(pixel, extent)))
    {
        return;
    }

    float depth = texelFetch(gbufferDepth, pixel, 0).r;
    if (depth >= 0.999999)
    {
        StoreVisibility(pixel, 1.0);
        return;
    }

    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(extent);
    vec4 worldPosition = params.inverseViewProjection *
                         vec4(uv * 2.0 - 1.0, depth, 1.0);
    if (isnan(worldPosition.w) || isinf(worldPosition.w) ||
        abs(worldPosition.w) < 0.000001)
    {
        StoreVisibility(pixel, 1.0);
        return;
    }

    vec3 normal = normalize(texelFetch(gbufferNormal, pixel, 0).xyz);
    vec3 lightDirection = normalize(params.lightDirectionAndBias.xyz);
    if (dot(normal, lightDirection) <= 0.00001)
    {
        StoreVisibility(pixel, 1.0);
        return;
    }

    vec3 origin = worldPosition.xyz / worldPosition.w +
                  normal * params.lightDirectionAndBias.w;
    visibility = 1u;
    traceRayEXT(scene,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                RayTracingInstanceMaskShadowCaster,
                0u,
                0u,
                0u,
                origin,
                0.001,
                lightDirection,
                100000.0,
                0);
    StoreVisibility(pixel, visibility != 0u ? 1.0 : 0.0);
}
