// ========================================
// 太陽の仮想シャドウマップ（VSM）の描画: 物理ページの texel へ深度を書く
//
// 色・深度の添付は無い。128×128 のビューポートの画素 (x, y) が、頂点シェーダーが渡した物理ページの texel (x, y) に対応する。
// 深度 [0, 1] は符号ビットが 0 の float なので、ビットの整数の大小が深度の大小と一致し、atomicMin で手前（小さい値）が残る。
// 物理ページは何も無い texel が 1.0 のビット（0x3F800000）で、消去（vsm_clear.comp）が先に埋める。
// ========================================

#ifndef NORVES_VSM_DRAW_FRAGMENT_GLSL
#define NORVES_VSM_DRAW_FRAGMENT_GLSL

#include "Common/VisibilityBuffer.glsl"
#include "Common/VirtualShadowMapChunk.glsl"
#ifdef VSM_ALPHA_CLIP
#include "Common/ShadowAlphaClip.glsl"
layout(set = 0, binding = 7) uniform sampler2D maskAlbedoTexture;
#endif

layout(std430, set = 0, binding = 3) buffer VsmPhysicalPool
{
    uint pool[];
};

layout(location = 0) flat in uint inPhysicalPage;
layout(location = 1) in float inDepth;
#ifdef VSM_ALPHA_CLIP
layout(location = 2) in vec2 inTexCoord;
// x = 閾値、y = 被覆率の乗数、z = 切り抜きの印（0 = なし、1 = 通常、3 = sparse）。
layout(location = 3) flat in vec3 inAlphaClip;
#endif

void main()
{
#ifdef VSM_ALPHA_CLIP
    // 印は描画ごとに一様。画素ごとの範囲検査で戻る前に、画面微分を使う切り抜き判定を済ませる。
    if (inAlphaClip.z > 0.5)
    {
        if (ShadowAlphaClipShouldDiscard(maskAlbedoTexture, inTexCoord, inAlphaClip.y, inAlphaClip.x, inAlphaClip.z > 2.5))
        {
            discard;
        }
    }
#endif
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

#endif // NORVES_VSM_DRAW_FRAGMENT_GLSL
