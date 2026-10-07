#version 450

// ========================================
// 太陽の仮想シャドウマップ（VSM）の描画: 物理ページの texel へ深度を書く
//
// 色・深度の添付は無い。128×128 のビューポートの画素 (x, y) が、頂点シェーダーが渡した物理ページの texel (x, y) に対応する。
// 深度 [0, 1] は符号ビットが 0 の float なので、ビットの整数の大小が深度の大小と一致し、atomicMin で手前（小さい値）が残る。
// 物理ページは何も無い texel が 1.0 のビット（0x3F800000）で、消去（vsm_clear.comp）が先に埋める。
// ========================================

#include "Common/VisibilityBuffer.glsl"
#include "Common/VirtualShadowMapChunk.glsl"

layout(std430, set = 0, binding = 3) buffer VsmPhysicalPool
{
    uint pool[];
};

layout(location = 0) flat in uint inPhysicalPage;
layout(location = 1) in float inDepth;

void main()
{
    const uvec2 texel = uvec2(gl_FragCoord.xy);
    if (texel.x >= VSM_PAGE_RESOLUTION || texel.y >= VSM_PAGE_RESOLUTION)
    {
        return;
    }
    // 補間の丸めで負になる値を 0 に寄せ、-0.0 の符号ビットも落とす（ビットの大小が深度の大小と一致する条件）
    const float depth = clamp(inDepth, 0.0, 1.0);
    const uint index = inPhysicalPage * VSM_PAGE_WORDS + texel.y * VSM_PAGE_RESOLUTION + texel.x;
    atomicMin(pool[index], floatBitsToUint(depth) & 0x7FFFFFFFu);
}
