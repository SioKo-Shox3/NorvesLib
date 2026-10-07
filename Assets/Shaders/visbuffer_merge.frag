#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// ========================================
// ビジビリティバッファ: 64bit のバッファ（深度 + ID）を ID・深度の添付へ合流させるフラグメントシェーダー
//
// 全画面の三角形 1 枚で走る。画素ごとに 64bit の値（上位 32bit = 深度のビット、下位 32bit = ID。Common/VisibilityBuffer.glsl）を読み、
// 空（すべてのビットが 1）なら discard、そうでなければ gl_FragDepth と ID を出す。
// 深度の比較（LessOrEqual）はパイプライン側で行うので、ハードのラスタが書いた深度より奥の値は捨てられ、
// 同じ深度の値は 64bit のバッファの ID で上書きされる。
// ========================================

#define VIS_ENABLE_KEY64
#include "Common/VisibilityBuffer.glsl"

layout(std430, set = 0, binding = 0) readonly buffer VisibilityKeys
{
    uint64_t keys[];
};

layout(std140, set = 0, binding = 1) uniform MergeParams
{
    // x: バッファの幅、y: バッファの高さ（画面の画素数）
    uvec4 size;
} params;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out uint outId;

void main()
{
    const uvec2 pixel = uvec2(gl_FragCoord.xy);
    if (pixel.x >= params.size.x || pixel.y >= params.size.y)
    {
        discard;
    }

    const uint64_t key = keys[pixel.y * params.size.x + pixel.x];
    if (VisKeyIsEmpty(key))
    {
        discard;
    }

    gl_FragDepth = VisKeyDepth(key);
    outId = VisKeyId(key);
}
