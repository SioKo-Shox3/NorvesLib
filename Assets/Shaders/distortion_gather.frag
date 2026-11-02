#version 450

// 歪みのバッファを使ってシーンの色を集め、アルファもそのまま渡す。
// ずらしが無い画素は補間を通さず、入力と同じ画素値を保つ。

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D distortionOffsets;

layout(location = 0) out vec4 outColor;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 offset = texelFetch(distortionOffsets, pixel, 0).rg;
    if (all(equal(offset, vec2(0.0))))
    {
        outColor = texelFetch(sceneColor, pixel, 0);
        return;
    }

    // 両軸に画面の高さを掛け、同じ強さで同じ画素数だけ動かす。画面のyは下向き。
    vec2 size = vec2(textureSize(sceneColor, 0));
    vec2 uv = (gl_FragCoord.xy + offset * size.y) / size;
    outColor = textureLod(sceneColor, uv, 0.0);
}
