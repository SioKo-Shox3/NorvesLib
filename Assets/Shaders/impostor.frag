#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common/HeightFog.glsl"
#include "Common/ImpostorOctahedral.glsl"

layout(set = 0, binding = 0) uniform WorldBoardForwardData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 sceneColorParams;
    HeightFogParams heightFog; // 高さフォグ（C++ の GPUHeightFogParams。offset 192）
} worldBoard;

layout(set = 0, binding = 1) uniform sampler2D impostorAtlas;
layout(set = 0, binding = 2) uniform sampler2D heightFogSkyRadiance; // 空の放射輝度 LUT（無ければ黒の 1×1）

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragQuadUV;
layout(location = 2) flat in vec3 fragViewDirection;
layout(location = 3) flat in vec4 fragAtlasInfo0;
layout(location = 4) flat in vec4 fragAtlasInfo1;
layout(location = 5) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

vec4 SampleAtlasCell(vec2 cell, vec2 grid, vec2 atlasSize, vec2 quadUV)
{
    vec2 cellSize = 1.0 / grid;
    vec2 cellMin = cell * cellSize;
    vec2 cellUV = clamp(vec2(quadUV.x, 1.0 - quadUV.y), 0.0, 1.0);
    vec2 uv = cellMin + cellUV * cellSize;
    vec2 texelInset = 0.5 / atlasSize;
    return texture(impostorAtlas, clamp(uv, cellMin + texelInset, cellMin + cellSize - texelInset));
}

void main()
{
    vec2 grid = max(fragAtlasInfo0.xy, vec2(1.0));
    float cellResolution = max(fragAtlasInfo1.x, 1.0);
    vec2 atlasSize = max(fragAtlasInfo0.zw, grid * cellResolution);
    // 前方描画ではワールドの視線を個体の回転なしとして扱い、上半球のアトラスを使う。
    // 全球のアトラスは、この前方描画では選択できない。
    vec2 encoded = EncodeImpostorOctahedral(fragViewDirection, IMPOSTOR_LAYOUT_HEMI_OCTAHEDRAL);
    vec2 cellSpace = encoded * max(grid - vec2(1.0), vec2(0.0));
    vec2 cell0 = floor(cellSpace);
    vec2 cell1 = min(cell0 + vec2(1.0), grid - vec2(1.0));
    vec2 blendWeight = clamp(cellSpace - cell0, vec2(0.0), vec2(1.0));

    vec4 c00 = SampleAtlasCell(cell0, grid, atlasSize, fragQuadUV);
    vec4 c10 = SampleAtlasCell(vec2(cell1.x, cell0.y), grid, atlasSize, fragQuadUV);
    vec4 c01 = SampleAtlasCell(vec2(cell0.x, cell1.y), grid, atlasSize, fragQuadUV);
    vec4 c11 = SampleAtlasCell(cell1, grid, atlasSize, fragQuadUV);
    vec4 atlasColor = mix(mix(c00, c10, blendWeight.x),
                          mix(c01, c11, blendWeight.x),
                          blendWeight.y);
    vec4 color = atlasColor * fragColor;
    vec4 premultipliedColor = vec4(color.rgb * color.a * worldBoard.sceneColorParams.x, color.a);
    // One・InvSrcAlpha の事前乗算の合成なので、霧の光にも被覆率を掛ける。
    outColor = ApplyHeightFogPremultiplied(premultipliedColor,
                                           EvaluateHeightFogForward(worldBoard.heightFog, heightFogSkyRadiance, fragWorldPos));
}
