#version 450

layout(set = 0, binding = 0) uniform WorldBoardForwardData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
} worldBoard;

struct InstanceData
{
    mat4 world;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragQuadUV;
layout(location = 2) flat out vec3 fragViewDirection;
layout(location = 3) flat out vec4 fragAtlasInfo0;
layout(location = 4) flat out vec4 fragAtlasInfo1;

void main()
{
    vec2 quad[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0));

    InstanceData instanceData = instances[gl_InstanceIndex];
    vec3 origin = instanceData.world[3].xyz;
    vec2 sizeWorld = instanceData.normalRows[0].xy;
    vec2 pivot = instanceData.normalRows[0].zw;
    vec2 flipFlags = instanceData.normalRows[1].xy;
    vec2 geometryUV = quad[gl_VertexIndex];
    vec2 sampleUV = quad[gl_VertexIndex];

    if (flipFlags.x > 0.5)
    {
        geometryUV.x = 1.0 - geometryUV.x;
    }

    if (flipFlags.y > 0.5)
    {
        geometryUV.y = 1.0 - geometryUV.y;
    }

    vec2 local = geometryUV - pivot;
    vec3 worldPos = origin +
                    worldBoard.cameraRight.xyz * (local.x * sizeWorld.x) +
                    worldBoard.cameraUp.xyz * (local.y * sizeWorld.y);

    gl_Position = worldBoard.projection * worldBoard.view * vec4(worldPos, 1.0);
    fragColor = instanceData.objectColor;
    fragQuadUV = sampleUV;
    fragViewDirection = worldBoard.cameraPosition.xyz - origin;
    fragAtlasInfo0 = vec4(instanceData.normalRows[2].zw, instanceData.customData.yz);
    fragAtlasInfo1 = vec4(instanceData.customData.x, instanceData.customData.w, 0.0, 0.0);
}
