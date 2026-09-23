// 不交差をレイ生成段へ返す。
#version 460
#extension GL_EXT_ray_tracing : require

#include "PathTracing/PathTracingCommon.glsl"

layout(location = 0) rayPayloadInEXT PathPayload payload;

void main()
{
    payload.Hit = 0u;
}
