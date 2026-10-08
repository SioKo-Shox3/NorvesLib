// 仮想シャドウマップ（VSM）の「スライス」の表。太陽のクリップマップの段も、将来の点光源の面も、同じ形のスライスとして並べる。
// Rendering/VirtualShadowMapSample.h の GPUVsmSlice（std430）と同じ並び。
//
// スライスごとの値（ページの一辺・texel の一辺・範囲の原点・ページの表の先頭・投影の行列）は、固定長の uniform ではなくこの表に持つ。
// 取り込む側が VSM_SLICE_BINDING（descriptor set 0 の binding の番号）を先に定義すると、読み取り専用の storage buffer vsmSlices を宣言する。
// 定義しない側（vsm_allocate.comp のように別の宣言が要るもの）は、構造体だけを使う。
//
// ページの表の番地は「スライスの先頭 + ページの番号」。ページの番号はトーラスの番地（絶対のページの座標 mod 一辺）で、
// 一辺は 2 の冪。太陽の段は 128 × 128 で、先頭から段の順に並べる（先頭 = 段 × 16384）。

#ifndef VIRTUAL_SHADOW_MAP_SLICE_GLSL
#define VIRTUAL_SHADOW_MAP_SLICE_GLSL

// 投影の種類（extra.z）
const uint VSM_SLICE_PROJECTION_ORTHO = 0u;
const uint VSM_SLICE_PROJECTION_PERSPECTIVE = 1u;

struct VsmSlice
{
    // 投影の行列の上 3 行（ワールドの位置 p について、x = dot(axisX, vec4(p, 1))、y = axisY、z = axisZ。w は原点のずれ）。
    // 正射影の段はライト空間の基底（右・上・光の進む向き）で、深度は z
    vec4 axisX;
    vec4 axisY;
    vec4 axisZ;
    // x: ページの一辺（m）、y: texel の一辺（m）、z, w: 予約（0）
    vec4 info;
    // x, y: 範囲の最小の絶対のページの番号、z: ページの表の先頭（要素の番号）、w: ページの表の一辺（ページの数。2 の冪）
    ivec4 origin;
    // x, y: 前フレームの範囲の最小の絶対のページの番号（vsm_allocate.comp だけが読む）、z: 投影の種類、w: 予約（0）
    ivec4 extra;
};

#ifdef VSM_SLICE_BINDING
layout(std430, set = 0, binding = VSM_SLICE_BINDING) readonly buffer VsmSliceBuffer
{
    VsmSlice vsmSlices[];
};
#endif

// ページ (pageX, pageY)（絶対のページの座標）の、ページの表の番地（スライスの先頭 + トーラスの番地）
uint VsmSliceEntryIndex(VsmSlice slice, int pageX, int pageY)
{
    const uint mask = uint(slice.origin.w) - 1u;
    return uint(slice.origin.z) + (uint(pageY) & mask) * uint(slice.origin.w) + (uint(pageX) & mask);
}

#endif // VIRTUAL_SHADOW_MAP_SLICE_GLSL
