// 不交差をレイ生成段へ返す。
#version 460
#extension GL_EXT_ray_tracing : require

struct PathPayload
{
    vec3 Position;
    vec3 Normal;
    vec3 BaseColor;
    vec3 Emission;
    uint Hit;
};
layout(location = 0) rayPayloadInEXT PathPayload payload;

void main()
{
    payload.Hit = 0u;
}
