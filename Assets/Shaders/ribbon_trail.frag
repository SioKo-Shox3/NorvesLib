#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common/HeightFog.glsl"

layout(set = 0, binding = 0) uniform RibbonTrailData
{
    mat4 view;
    mat4 projection;
    vec4 sceneColorParams;
    HeightFogParams heightFog; // 高さフォグ（C++ の GPUHeightFogParams。offset 144）
} ribbon;

layout(set = 0, binding = 1) uniform sampler2D heightFogSkyRadiance; // 空の放射輝度 LUT（無いときは黒の 1×1）

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

void main()
{
    // 加算だけ RGB に A を掛け、One + One の混合でも年齢とともに薄れるようにする。
    // アルファ混合では SrcAlpha が A を掛けるので、RGB は事前乗算しない。
    bool bAdditive = ribbon.sceneColorParams.y > 0.5;
    float opacityScale = bAdditive ? fragColor.a : 1.0;
    vec4 color = vec4(fragColor.rgb * ribbon.sceneColorParams.x * opacityScale, fragColor.a);
    HeightFogForwardResult fog = EvaluateHeightFogForward(ribbon.heightFog, heightFogSkyRadiance, fragWorldPos);
    // 加算は背後の霧の光を足さず透過率だけ掛け、アルファは straight alpha の混合で霧の色を重ねる。
    outColor = bAdditive ? ApplyHeightFogAdditive(color, fog) : ApplyHeightFogStraightAlpha(color, fog);
}
