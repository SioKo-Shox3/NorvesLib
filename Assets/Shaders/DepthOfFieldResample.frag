#version 450

// 被写界深度のgatherの結果をSceneColorへそのまま書き戻す。PTと同じピントの倍率（画角を
// 1 - f/ピント距離倍に狭める）はgatherが部分画素の位置で掛けるため、ここでは補間しない。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gatherTexture;

layout(std140, set = 0, binding = 2) uniform DepthOfFieldParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndFocusDistance;
    vec4 cameraForwardAndCocScale;
    vec4 imageSizeAndLimits;
} params;

void main()
{
    ivec2 size = ivec2(params.imageSizeAndLimits.xy);
    outColor = texelFetch(gatherTexture, clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1), 0);
}
