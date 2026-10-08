// ========================================
// 太陽の仮想シャドウマップ（VSM）の MegaGeometry の投影物のカリングと、dirty のページの階層が共有する定数・関数
// （Rendering/VirtualShadowMapRaster.h の GPUMegaCullParams・VirtualShadowMap::MegaCull* と一致）
//
// 取り込む側が、binding 14 の定数（megaParams）をここで受け取る。vsm_dirty_mips.comp（dirty の階層を作る）と
// vsm_mega_cull.comp（階層を引いてクラスタを選ぶ）が取り込む。ページの一辺・texel・範囲の原点・ページの表の先頭は、
// スライスの表（Common/VirtualShadowMapSlice.glsl）から引く。取り込む側がこのファイルより先に VSM_SLICE_BINDING を定義すると、その binding に表を宣言する。
//
// dirty のページの階層: スライス（太陽では段）ごとに、128×128 のページの格子（mip 0）から 1×1（mip 7）までの「その範囲に dirty で割り当て済みの
// ページがあるか」のビット列。ビットの座標は、範囲の最小のページ（スライスの範囲の原点）からの相対の座標（トーラスの番地ではない）。
// スライスの範囲が動いてもトーラスの番地は変わらないが、空間の近さ（階層の範囲）は相対の座標で決まる。階層の形は 128×128 のスライスを前提にする。
// ========================================

#ifndef NORVES_VSM_MEGA_CULL_GLSL
#define NORVES_VSM_MEGA_CULL_GLSL

#include "Common/VirtualShadowMapSlice.glsl"

// dirty の階層の一辺（ページ。128×128 のスライスを前提にする）と、ページの表の 1 要素の印（Common/VirtualShadowMapChunk.glsl の VSM_* と同じ値）
const uint VSM_MEGA_TABLE_DIMENSION = 128u;
const uint VSM_MEGA_PAGE_ENTRY_ALLOCATED = 1u << 31;
const uint VSM_MEGA_PAGE_ENTRY_DIRTY = 1u << 30;
const uint VSM_MEGA_PAGE_ENTRY_RETRY = 1u << 29;

// dirty の階層の mip の数（128² → 1 = 8 段）と、1 段あたりの語（uint32）の数（21845 ビット = 683 語を 16 語の境へ切り上げ）
const uint VSM_MEGA_DIRTY_MIP_COUNT = 8u;
const uint VSM_MEGA_DIRTY_WORDS_PER_LEVEL = 688u;

// 1 段の中での mip ごとのビットの先頭（128², 64², 32², 16², 8², 4², 2², 1² の累計）
const uint VSM_MEGA_DIRTY_MIP_OFFSETS[8] = uint[8](0u, 16384u, 20480u, 21504u, 21760u, 21824u, 21840u, 21844u);

/**
 * @brief dirty の階層のビットの番号（全段通し）。cell は mip の中の相対の座標（0 以上、(128 >> mip) 未満）
 */
uint VsmMegaDirtyBitIndex(uint level, uint mip, uvec2 cell)
{
    const uint width = VSM_MEGA_TABLE_DIMENSION >> mip;
    return level * VSM_MEGA_DIRTY_WORDS_PER_LEVEL * 32u + VSM_MEGA_DIRTY_MIP_OFFSETS[mip] + cell.y * width + cell.x;
}

// 影の表（binding 18。インスタンスの表と同じ並び・同じ添字。MegaGeometryPass の MegaGeometryShadowInstance と一致）
// boundsSphere: インスタンスの境界（WorldBounds。ワールド）の中心 xyz + 半径。info.x = 影の判定の最初のワークグループの通し番号、
// info.y = 印（SHADOW_FLAG_*）。addresses: 頂点（xy）・インデックス（zw）を持つプールの塊のバッファのデバイスアドレス（下位・上位。引けなければ 0）
struct ShadowInstance
{
    vec4 boundsSphere;
    uvec4 info;
    uvec4 addresses;
};
const uint SHADOW_FLAG_CASTER = 1u;
const uint SHADOW_FLAG_BOUNDS = 2u;

// std140。VirtualShadowMapRaster.cpp の GPUMegaCullParams と同じ並び
layout(std140, set = 0, binding = 14) uniform VsmMegaCullParams
{
    vec4 lightRight;
    vec4 lightUp;
    vec4 lightDirection;
    // x: 深度の原点（ライト空間の深度）、y: 深度の範囲の片側（m）
    vec4 depth;
    // x: スライス（段）の数、y: 出力の一覧の容量（クラスタの数）、z: 影のフラットな判定の全ワークグループの数
    uvec4 counts;
} megaParams;

#endif // NORVES_VSM_MEGA_CULL_GLSL
