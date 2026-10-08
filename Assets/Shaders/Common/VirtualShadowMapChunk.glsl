// ========================================
// 太陽の仮想シャドウマップ（VSM）の影の塊の記録と、展開・描画が共有する定数（Rendering/VirtualShadowMapRaster.h と一致）
//
// 塊は三角形 128 個以下の投影物の単位で、ビジビリティバッファの描画の記録（VisibilityDrawRecord。種類・三角形の数・
// インデックスの先頭・頂点の基点・頂点とインデックスのデバイスアドレス）に、ワールドの境界とワールドへの変換を足した 144 バイト。
// 頂点の読み方はビジビリティバッファの幾何の解決・ソフトウェアラスタと同じ（VisibilityTriangleFetch.glsl）で、
// 位置はローカル空間のまま読み、world（3 行の 3×4 行列）でワールドへ変換する。スキニングのようにワールド空間の頂点は単位行列にする。
//
// 取り込む側が先に Common/VisibilityBuffer.glsl を取り込む（VisibilityDrawRecord が要る）。
// ========================================

#ifndef NORVES_VIRTUAL_SHADOW_MAP_CHUNK_GLSL
#define NORVES_VIRTUAL_SHADOW_MAP_CHUNK_GLSL

struct VsmShadowChunk
{
    VisibilityDrawRecord record;
    // ワールドの境界と、展開する段の集合（CPU が境界から決めた段。ビット L が段 L）。std430 で vec3 の後ろの uint は同じ 16 バイトに詰まる
    vec3 boundsMin;
    uint levelMask;
    vec3 boundsMax;
    uint reserved;
    // ワールドへの変換: ワールドの位置 = (dot(world0, p), dot(world1, p), dot(world2, p))、p = (ローカルの位置, 1)
    vec4 world0;
    vec4 world1;
    vec4 world2;
};

// 1 ページの一辺（texel。VirtualShadowMap::PAGE_RESOLUTION と一致）。ページの表の一辺・先頭はスライスの表（Common/VirtualShadowMapSlice.glsl）が持つ
const uint VSM_PAGE_RESOLUTION = 128u;
const uint VSM_PAGE_WORDS = VSM_PAGE_RESOLUTION * VSM_PAGE_RESOLUTION;

// ページの表の 1 要素の印（VirtualShadowMap::PAGE_ENTRY_* と一致）
const uint VSM_PAGE_ENTRY_ALLOCATED = 1u << 31;
const uint VSM_PAGE_ENTRY_DIRTY = 1u << 30;
const uint VSM_PAGE_ENTRY_RETRY = 1u << 29;
const uint VSM_PAGE_INDEX_MASK = (1u << 20) - 1u;

// 展開が書く、間接描画の引数の配列 draws の並び: 先頭の 4 語が頭（0 = インスタンスの確保の数）、続いて塊ごとに 5 語
// （VkDrawIndexedIndirectCommand: indexCount, instanceCount, firstIndex, vertexOffset, firstInstance）
const uint VSM_DRAWS_HEADER_WORDS = 4u;
const uint VSM_DRAW_COMMAND_WORDS = 5u;

// 展開が書くインスタンスは uvec4: x = 塊の番号、y = 段 | (物理ページの番号 << 4)、z = 絶対のページの x（int のビット）、w = 絶対のページの y

#endif // NORVES_VIRTUAL_SHADOW_MAP_CHUNK_GLSL
