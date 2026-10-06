#version 450

// ========================================
// ビジビリティバッファの検証表示: 画素の ID を色にして出す
//
// 記録の番号（描画 = クラスタ・塊）ごとに色相を散らし、記録の中の三角形の番号で明るさを散らす。
// 色相の帯は記録の種類で分ける（MegaGeometry のクラスタ: 赤〜黄、手続きメッシュの塊: 緑、スキニングの塊: 青）。
// 記録の表から種類を引けなかった画素（表に無い・種類が不正）はマゼンタ。何も描かれていない画素（ID = 0）は暗い灰色。
// ========================================

#include "Common/VisibilityBuffer.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform usampler2D idTexture;

layout(std430, set = 0, binding = 1) readonly buffer RecordTable
{
    VisibilityDrawRecord records[];
};

layout(std140, set = 0, binding = 2) uniform DebugParams
{
    // x: 色に掛ける倍率（HDR のシーンの色の明るさに合わせる）
    vec4 params;
} debugParams;

float Hash11(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return float(value & 0xFFFFFFu) / 16777215.0;
}

vec3 HsvToRgb(float h, float s, float v)
{
    vec3 k = clamp(abs(fract(vec3(h) + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

void main()
{
    const uvec2 idSize = uvec2(textureSize(idTexture, 0));
    const ivec2 pixel = ivec2(min(uvec2(gl_FragCoord.xy), idSize - uvec2(1u)));
    const uint id = texelFetch(idTexture, pixel, 0).x;

    vec3 color = vec3(0.03);
    if (!VisIsEmpty(id))
    {
        const uint recordNumber = VisRecordNumber(id);
        const uint triangle = VisTriangleIndex(id);
        float hueBase = -1.0;
        if (recordNumber < uint(records.length()))
        {
            const uint kind = VisRecordKind(records[recordNumber]);
            if (kind == VIS_KIND_MEGA_CLUSTER)
            {
                hueBase = 0.0;
            }
            else if (kind == VIS_KIND_PROCEDURAL_CHUNK)
            {
                hueBase = 0.30;
            }
            else if (kind == VIS_KIND_SKINNED_CHUNK)
            {
                hueBase = 0.58;
            }
        }

        if (hueBase < 0.0)
        {
            color = vec3(1.0, 0.0, 1.0);
        }
        else
        {
            const float hue = hueBase + 0.17 * Hash11(recordNumber);
            const float value = 0.45 + 0.55 * Hash11(triangle * 7919u + recordNumber);
            color = HsvToRgb(hue, 0.85, value);
        }
    }

    outColor = vec4(color * debugParams.params.x, 1.0);
}
