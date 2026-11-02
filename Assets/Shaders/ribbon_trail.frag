#version 450

layout(set = 0, binding = 0) uniform RibbonTrailData
{
    mat4 view;
    mat4 projection;
    vec4 sceneColorParams;
} ribbon;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main()
{
    // 加算だけ RGB に A を掛け、One + One の混合でも年齢とともに薄れるようにする。
    // アルファ混合では SrcAlpha が A を掛けるので、RGB は事前乗算しない。
    float opacityScale = ribbon.sceneColorParams.y > 0.5 ? fragColor.a : 1.0;
    outColor = vec4(fragColor.rgb * ribbon.sceneColorParams.x * opacityScale, fragColor.a);
}
