#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common/HeightFog.glsl"

layout(set = 0, binding = 0) uniform WorldBoardForwardData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 sceneColorParams;
    HeightFogParams heightFog; // 高さフォグ（C++ の GPUHeightFogParams。offset 192）
    LocalFogParams localFog; // 局所霧（C++ の GPULocalFogParams。offset 288）
} worldBoard;

layout(set = 0, binding = 1) uniform sampler2D boardTexture;
layout(set = 0, binding = 2) uniform sampler2D heightFogSkyRadiance; // 空の放射輝度 LUT（無ければ黒の 1×1）

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = texture(boardTexture, fragUV) * fragColor;
    vec4 premultipliedColor = vec4(color.rgb * color.a * worldBoard.sceneColorParams.x, color.a);
    // One・InvSrcAlpha の事前乗算の合成なので、霧の光にも被覆率を掛ける。
    outColor = ApplyHeightFogPremultiplied(premultipliedColor,
                                           EvaluateHeightFogForward(worldBoard.heightFog, heightFogSkyRadiance, fragWorldPos));
    outColor = ApplyLocalFogPremultiplied(outColor, EvaluateLocalFogForward(worldBoard.localFog, worldBoard.heightFog, heightFogSkyRadiance, fragWorldPos));
}
