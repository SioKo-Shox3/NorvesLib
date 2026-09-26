#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1, std430) buffer VisibilityResults
{
    uint values[];
} visibilityResults;

layout(location = 0) rayPayloadEXT uint visibility;

void main()
{
    uint rayIndex = gl_LaunchIDEXT.x;
    vec3 rayOrigin = rayIndex == 0u ? vec3(0.0, 0.0, -2.0) : vec3(4.0, 0.0, -2.0);
    visibility = 0xffffffffu;
    traceRayEXT(scene,
                gl_RayFlagsOpaqueEXT,
                0xffu,
                0u,
                0u,
                0u,
                rayOrigin,
                0.001,
                vec3(0.0, 0.0, 1.0),
                10.0,
                0);
    visibilityResults.values[rayIndex] = visibility;
}
