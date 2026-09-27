#version 450

// TAA の解決の結果（次のフレームの履歴）を SceneColor へそのまま書き戻す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D resolvedTexture;

layout(std140, set = 0, binding = 4) uniform TemporalAAParams
{
    mat4 inverseViewProjection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPositionAndHistory;
    vec4 imageSize;
    vec4 blend;
    vec4 jitter;
} params;

void main()
{
    ivec2 size = ivec2(params.imageSize.xy);
    outColor = texelFetch(resolvedTexture, clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1), 0);
}
