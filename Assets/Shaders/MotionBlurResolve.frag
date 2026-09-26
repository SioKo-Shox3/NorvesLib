#version 450

// 動きぼけのgatherの結果をSceneColorへそのまま書き戻す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gatherTexture;

layout(std140, set = 0, binding = 4) uniform MotionBlurParams
{
    mat4 inverseViewProjection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPositionAndHistory;
    vec4 imageSizeAndShutter;
    vec4 tileInfo;
} params;

void main()
{
    ivec2 size = ivec2(params.imageSizeAndShutter.xy);
    outColor = texelFetch(gatherTexture, clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1), 0);
}
