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
    // x: ページの一辺（m）、y: texel の一辺（m）。透視（点光源の面）は ページ・texel の面の NDC の幅で、z: Range（m）、w: 近い平面の距離（m）。正射影の z, w は予約（0）
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

// 透視のスライス（点光源の面）の、境界球が覆うページの範囲（面の全体を [0, 一辺のページ数) としたページの座標）。覆わなければ幅・高さが 0。
// 展開（vsm_expand.comp）とカリング（vsm_mega_cull.comp）が同じ判定を使う。
// CPU の SliceMasksForBounds（VirtualShadowMapCasters.h）の判定と同じ手順（CPU は倍精度で、わずかに広く取る）
void VsmPerspectivePageRange(VsmSlice slice, vec3 center, float radius, out ivec2 pageMin, out uvec2 size)
{
    pageMin = ivec2(0);
    size = uvec2(0);
    const float range = slice.info.z;
    const float nearPlane = slice.info.w;
    const int pages = slice.origin.w;
    if (!(range > nearPlane) || !(nearPlane >= 0.0) || pages <= 0 || !(radius >= 0.0))
    {
        return;
    }
    // 球の中心の面の座標（x, y = 面の接線方向、z = 面の軸の向きの距離）
    const vec3 c = vec3(dot(center, slice.axisX.xyz) + slice.axisX.w,
                        dot(center, slice.axisY.xyz) + slice.axisY.w,
                        dot(center, slice.axisZ.xyz) + slice.axisZ.w);
    // Range の外
    const float reach = range + radius;
    if (!(dot(c, c) <= reach * reach))
    {
        return;
    }
    // 近い平面より全部手前（光源の後ろ側）・遠い平面の外
    if (c.z + radius < nearPlane || c.z - radius > range)
    {
        return;
    }
    // 面の錐台の 4 つの側面（|x| ≤ z, |y| ≤ z。法線 (±1, 0, -1) / √2 など）の外
    const float side = radius * 1.4142136;
    if (c.x - c.z > side || -c.x - c.z > side || c.y - c.z > side || -c.y - c.z > side)
    {
        return;
    }
    // 近い平面をまたぐ球は、透視で写せないので面全体
    if (c.z - radius <= nearPlane)
    {
        size = uvec2(uint(pages));
        return;
    }
    // 球を面の NDC へ写した矩形。原点から球へ引いた接線 x = t z の傾き t が範囲の端
    // （t² (cz² - r²) - 2 t cx cz + (cx² - r²) = 0 の 2 根。cz > r + near > 0 なので分母は正）
    const float r2 = radius * radius;
    const vec2 denominator = vec2(c.z * c.z - r2);
    const vec2 centerNdc = c.xy * c.z;
    const vec2 spread = radius * sqrt(max(c.xy * c.xy + vec2(c.z * c.z - r2), vec2(0.0)));
    const vec2 ndcLow = (centerNdc - spread) / denominator;
    const vec2 ndcHigh = (centerNdc + spread) / denominator;
    const float margin = 1.0e-4;
    const ivec2 low = max(ivec2(floor((ndcLow - vec2(margin)) * (0.5 * float(pages)) + vec2(0.5 * float(pages)))), ivec2(0));
    const ivec2 high = min(ivec2(floor((ndcHigh + vec2(margin)) * (0.5 * float(pages)) + vec2(0.5 * float(pages)))), ivec2(pages - 1));
    pageMin = low;
    size = uvec2(max(high - low + ivec2(1), ivec2(0)));
}

#endif // VIRTUAL_SHADOW_MAP_SLICE_GLSL
