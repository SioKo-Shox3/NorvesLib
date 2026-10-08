#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

// ========================================
// 太陽の仮想シャドウマップ（VSM）の描画: 影の塊 × ページのインスタンスを、物理ページ 1 枚ぶんの 128×128 のビューポートへ描く
//
// 展開（vsm_expand.comp）が作った間接描画の 1 回 = 1 つの塊（塊の番号が counts.x 以上なら MegaGeometry のクラスタの記録 = binding 4）。頂点の番号（0 .. 三角形の数 × 3 - 1）から塊の中の三角形とその頂点を決め、
// インスタンスの番号（展開が書いた範囲の中の位置）から（スライス・絶対のページ・物理ページ）を引く。インスタンスの y は スライス（下位 8 ビット）| 物理ページ << 8。
// 頂点の読み方は VisLoadTrianglePositions と同じ（記録の BDA。位置だけを読む）。
//
// ライト空間の位置（lx, ly）を、そのページの局所の texel 座標 ((lx - ページの x × ページの幅) / texel の一辺) へ写し、
// 128 texel = NDC の [-1, 1] とする。ページの外はビューポートの外になり、ラスタライザが捨てる。
// Vulkan のフレームバッファは y が下向きなので、局所の y をそのまま NDC y = 局所 / 64 - 1 とすると、フレームバッファの行が局所の y と一致する。
// 深度は [0, 1]（0 が光源に近い）で、w = 1 なので画面空間で線形に補間される（正射影）。
// ========================================

#include "Common/VisibilityBuffer.glsl"
#include "Common/VisibilityTriangleFetch.glsl"
#include "Common/VirtualShadowMapChunk.glsl"
// binding 5: スライスの表（ページの一辺・texel・投影の行列）
#define VSM_SLICE_BINDING 5
#include "Common/VirtualShadowMapSlice.glsl"

// std140。VirtualShadowMapRaster.cpp の GPURasterParams と同じ並び
layout(std140, set = 0, binding = 0) uniform VsmRasterParams
{
    vec4 lightRight;
    vec4 lightUp;
    vec4 lightDirection;
    // x: 深度の原点（ライト空間の深度）、y: 1 / (2 × 深度の範囲)
    vec4 depth;
    // x: ホストが書いた塊の数（これ以降の塊の番号は MegaGeometry のクラスタの記録）
    uvec4 counts;
} params;

layout(std430, set = 0, binding = 1) readonly buffer VsmInstances
{
    uvec4 instances[];
};

layout(std430, set = 0, binding = 2) readonly buffer VsmChunks
{
    VsmShadowChunk chunks[];
};

// MegaGeometry のクラスタの影の塊の記録（vsm_mega_chunks.comp が書く）
layout(std430, set = 0, binding = 4) readonly buffer VsmMegaChunks
{
    VsmShadowChunk megaChunks[];
};

layout(location = 0) flat out uint outPhysicalPage;
layout(location = 1) out float outDepth;

void main()
{
    const uvec4 instance = instances[gl_InstanceIndex];
    const uint triangle = uint(gl_VertexIndex) / 3u;
    const uint corner = uint(gl_VertexIndex) % 3u;

    // 読めない・不正なときは、ビューポートの外へ出して何も描かない
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    outPhysicalPage = 0u;
    outDepth = 1.0;

    // 塊の記録。頂点・インデックスの読み方は、種類によらず記録のアドレスと基点で決まる（手続き・スキニング・MegaGeometry のクラスタで共通）
    const bool bMega = instance.x >= params.counts.x;
    const uint megaIndex = instance.x - params.counts.x;
    const VisibilityDrawRecord record = bMega ? megaChunks[megaIndex].record : chunks[instance.x].record;
    if (!VisRecordHasAddresses(record))
    {
        return;
    }

    // reference 型の変数には const を付けられない
    VisIndexWords indices = VisIndexWords(VisRecordIndexAddress(record));
    VisVertexArray vertices = VisVertexArray(VisRecordVertexAddress(record));
    const uint vertexIndex = VisLoadTriangleVertexIndex(record, indices, triangle, corner);
    const vec4 local = vec4(vertices.vertices[vertexIndex].px,
                            vertices.vertices[vertexIndex].py,
                            vertices.vertices[vertexIndex].pz,
                            1.0);
    const vec4 world0 = bMega ? megaChunks[megaIndex].world0 : chunks[instance.x].world0;
    const vec4 world1 = bMega ? megaChunks[megaIndex].world1 : chunks[instance.x].world1;
    const vec4 world2 = bMega ? megaChunks[megaIndex].world2 : chunks[instance.x].world2;
    const vec3 world = vec3(dot(world0, local), dot(world1, local), dot(world2, local));

    const uint level = instance.y & 255u;
    const uint physical = instance.y >> 8u;
    const vec2 page = vec2(float(int(instance.z)), float(int(instance.w)));
    // ページの一辺・texel・投影の行列はスライスの表から引く（正射影の段の行列は、ライト空間の基底）
    const VsmSlice slice = vsmSlices[level];
    const float pageMeters = slice.info.x;
    const float texelMeters = slice.info.y;

    const vec2 lightXY = vec2(dot(world, slice.axisX.xyz), dot(world, slice.axisY.xyz));
    const vec2 localTexel = (lightXY - page * pageMeters) / texelMeters;
    const float lightDepth = dot(world, slice.axisZ.xyz);
    const float depth01 = (lightDepth - params.depth.x) * params.depth.y + 0.5;

    gl_Position = vec4(localTexel / (0.5 * float(VSM_PAGE_RESOLUTION)) - vec2(1.0), depth01, 1.0);
    outPhysicalPage = physical;
    outDepth = depth01;
}
