#version 450

// 被写界深度のgatherの結果を、PTと同じピントの倍率（画角を1 - f/ピント距離倍に狭める）で拡大して
// SceneColorへ書き戻す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gatherTexture;

layout(std140, set = 0, binding = 2) uniform DepthOfFieldParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndFocusDistance;
    vec4 cameraForwardAndCocScale;
    vec4 imageSizeAndLimits;         // w: 拡大の倍率
} params;

void main()
{
    vec2 uv = 0.5 + (fragUV - 0.5) * params.imageSizeAndLimits.w;
    outColor = textureLod(gatherTexture, uv, 0.0);
}
