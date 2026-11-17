// インポスターの板をカメラの右・上へ展開し、GBuffer の描画へ個体とアトラスの情報を渡す。
#version 450
#extension GL_GOOGLE_include_directive : require

layout(std140, set = 0, binding = 0) uniform ImpostorGBufferData
{
    mat4 view;
    mat4 projection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 frameParams;
    vec4 atlasParams;
} impostor;

#include "Common/SceneInstanceData.glsl"

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragQuadUV;
layout(location = 2) flat out mat3 fragObjectRotation;
layout(location = 5) flat out vec3 fragObjectViewDirection;
layout(location = 6) flat out vec3 fragPreviousOriginDelta;
layout(location = 7) flat out vec2 fragSizeWorld;
layout(location = 8) flat out vec4 fragAtlasInfo;
layout(location = 9) flat out float fragCellResolution;
layout(location = 10) flat out vec3 fragTint;

vec3 NormalizeAxisOrDefault(vec3 axis, vec3 defaultAxis)
{
    float axisLength = length(axis);
    return axisLength > 0.0 ? axis / axisLength : defaultAxis;
}

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

    // 反転は板の形にだけ掛け、アトラスを読む UV は元のまま渡す。
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
                    impostor.cameraRight.xyz * (local.x * sizeWorld.x) +
                    impostor.cameraUp.xyz * (local.y * sizeWorld.y);

    // 各列から拡大を取り除き、長さが 0 の軸は単位行列の対応する列へ戻す。
    mat3 objectAxes = mat3(instanceData.world);
    mat3 objectRotation = mat3(
        NormalizeAxisOrDefault(objectAxes[0], vec3(1.0, 0.0, 0.0)),
        NormalizeAxisOrDefault(objectAxes[1], vec3(0.0, 1.0, 0.0)),
        NormalizeAxisOrDefault(objectAxes[2], vec3(0.0, 0.0, 1.0)));

    gl_Position = impostor.projection * impostor.view * vec4(worldPos, 1.0);
    fragWorldPos = worldPos;
    fragQuadUV = sampleUV;
    fragObjectRotation = objectRotation;
    fragObjectViewDirection = transpose(objectRotation) * (impostor.cameraPosition.xyz - origin);
    fragPreviousOriginDelta = instanceData.previousWorld[3].xyz - origin;
    fragSizeWorld = sizeWorld;
    fragAtlasInfo = vec4(instanceData.normalRows[2].zw, instanceData.customData.yz);
    fragCellResolution = instanceData.customData.x;
    fragTint = instanceData.objectColor.rgb;
}
