#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

// ========================================
// 太陽の仮想シャドウマップ（VSM）の描画: 影の塊 × ページのインスタンスを、物理ページ 1 枚ぶんの 128×128 のビューポートへ描く
//
// 展開（vsm_expand.comp）が作った間接描画の 1 回 = 1 つの塊（塊の番号が counts.x 以上なら MegaGeometry のクラスタの記録 = binding 4）。頂点の番号（0 .. 三角形の数 × 3 - 1）から塊の中の三角形とその頂点を決め、
// インスタンスの番号（展開が書いた範囲の中の位置）から（スライス・絶対のページ・物理ページ）を引く。インスタンスの y は スライス（下位 8 ビット）| 物理ページ << 8。
// 頂点の読み方は VisLoadTrianglePositions と同じ（記録の BDA。位置だけを読む）。草・低木の株の塊（VIS_KIND_FOLIAGE_CHUNK）は
// 頂点が FoliageVertex で、World に置いた株の置き方と params の風で foliage.vert と同じ式で置く（VsmPlaceFoliageVertex）。
//
// ライト空間の位置（lx, ly）を、そのページの局所の texel 座標 ((lx - ページの x × ページの幅) / texel の一辺) へ写し、
// 128 texel = NDC の [-1, 1] とする。ページの外はビューポートの外になり、ラスタライザが捨てる。
// Vulkan のフレームバッファは y が下向きなので、局所の y をそのまま NDC y = 局所 / 64 - 1 とすると、フレームバッファの行が局所の y と一致する。
// 深度は [0, 1]（0 が光源に近い）で、w = 1 なので画面空間で線形に補間される（正射影）。
//
// 点光源の面のスライス（投影の種類が透視）は、ワールドの位置を面の座標（x, y = 面の接線方向、z = 面の軸の向きの距離）へ写し、
// 面の NDC（x / z, y / z）をページの局所の NDC へ移す。クリップ座標の w は面の軸の距離 z のままにする（xy は NDC × z を線形の式で書く）ので、
// ラスタライザの透視補間が正しい。z ≤ near の部分は近い平面のクリップで落ち、光源の後ろの頂点を持つ三角形も壊れない。
// 深度は面の軸の向きの線形の距離 ÷ Range（varying として透視補間される。z はワールドの位置について線形なので補間の誤差が無い）。
// ========================================

#include "Common/VisibilityBuffer.glsl"
#include "Common/VisibilityTriangleFetch.glsl"
#include "Common/VirtualShadowMapChunk.glsl"
#include "Common/FoliagePlacement.glsl"
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
    // 株の塊の風: xy = 吹いていく向き（XZ。長さ 1）、z = 風の時計[s]
    vec4 foliageWind;
    // x = 突風の塊の流れる速さ[m/s]、y = 塊の大きさ[m]
    vec4 foliageWindParams;
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

// 草・低木の株の頂点（FoliageVertex の 13 個の float: 位置 3・法線 3・横 3・しなりの重み・色の混ぜ・遮蔽・揺れの位相）
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VsmFoliageVertexWords
{
    float words[];
};
const uint VSM_FOLIAGE_VERTEX_FLOATS = 13u;

// 株の塊の頂点 vertexIndex を、foliage.vert と同じ置き方でワールドへ置く（帯を画素の幅まで広げる処理は画面の描画だけのもので、影には使わない）。
// 置き方は塊の World（VirtualShadowMapFoliageCasters.h）: world0 = (根元 xyz, 向き)、world1 = (横の倍率, 高さの倍率, 揺れの位相,
// 距離で縮める倍率)、world2 = (風の強さ, 揺れの周波数, メッシュの高さ, 0)
vec3 VsmPlaceFoliageVertex(VisibilityDrawRecord record, uint vertexIndex, vec4 world0, vec4 world1, vec4 world2)
{
    VsmFoliageVertexWords foliageVertices = VsmFoliageVertexWords(VisRecordVertexAddress(record));
    const uint base = vertexIndex * VSM_FOLIAGE_VERTEX_FLOATS;
    const vec3 position = vec3(foliageVertices.words[base], foliageVertices.words[base + 1u], foliageVertices.words[base + 2u]);
    const float flex = foliageVertices.words[base + 9u];
    const float partPhase = foliageVertices.words[base + 12u];

    const vec3 root = world0.xyz;
    const float c = cos(world0.w);
    const float s = sin(world0.w);
    const float widthScale = world1.x * world1.w;
    const float heightScale = world1.y * world1.w;
    const vec3 local = position * vec3(widthScale, heightScale, widthScale);
    const vec3 placed = root + FoliageRotateYaw(local, c, s);
    const float plantHeight = world2.z * heightScale;
    const vec2 offset = FoliageWindOffsetFor(root.xz, flex, plantHeight, world1.z, partPhase, params.foliageWind.z,
                                             params.foliageWind.xy, params.foliageWindParams.xy, world2.x, world2.y);
    return ApplyFoliageWind(placed, offset, max(local.y, 0.0));
}

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
    const vec4 world0 = bMega ? megaChunks[megaIndex].world0 : chunks[instance.x].world0;
    const vec4 world1 = bMega ? megaChunks[megaIndex].world1 : chunks[instance.x].world1;
    const vec4 world2 = bMega ? megaChunks[megaIndex].world2 : chunks[instance.x].world2;
    vec3 world;
    if (!bMega && VisRecordKind(record) == VIS_KIND_FOLIAGE_CHUNK)
    {
        // 草・低木の株: 頂点は FoliageVertex、World は株の置き方
        world = VsmPlaceFoliageVertex(record, vertexIndex, world0, world1, world2);
    }
    else
    {
        const vec4 local = vec4(vertices.vertices[vertexIndex].px,
                                vertices.vertices[vertexIndex].py,
                                vertices.vertices[vertexIndex].pz,
                                1.0);
        world = vec3(dot(world0, local), dot(world1, local), dot(world2, local));
    }

    const uint level = instance.y & 255u;
    const uint physical = instance.y >> 8u;
    const vec2 page = vec2(float(int(instance.z)), float(int(instance.w)));
    // ページの一辺・texel・投影の行列はスライスの表から引く（正射影の段の行列は、ライト空間の基底）
    const VsmSlice slice = vsmSlices[level];
    const float pageMeters = slice.info.x;
    const float texelMeters = slice.info.y;

    if (uint(slice.extra.z) == VSM_SLICE_PROJECTION_PERSPECTIVE)
    {
        // 点光源の面。info.z = Range（m）、info.w = 近い平面の距離（m）。pageMeters・texelMeters は面の NDC の幅（ページ・texel）
        const float range = slice.info.z;
        const float nearPlane = slice.info.w;
        if (!(range > nearPlane) || !(nearPlane >= 0.0))
        {
            return;
        }
        const vec3 face = vec3(dot(world, slice.axisX.xyz) + slice.axisX.w,
                               dot(world, slice.axisY.xyz) + slice.axisY.w,
                               dot(world, slice.axisZ.xyz) + slice.axisZ.w);
        // ページの局所の NDC = ((面の NDC + 1) - ページ × ページの NDC の幅) / texel の NDC の幅 / 64 - 1。これに w = face.z を掛けた線形の式
        const vec2 localClip = (face.xy + vec2(face.z) * (1.0 - page * pageMeters)) / (texelMeters * (0.5 * float(VSM_PAGE_RESOLUTION))) - vec2(face.z);
        // 近い平面 face.z = near で 0、遠い平面 face.z = Range で w と等しくなる線形の深度のクリップ（透視の標準の形）
        const float clipZ = (face.z - nearPlane) * (range / (range - nearPlane));
        gl_Position = vec4(localClip, clipZ, face.z);
        outPhysicalPage = physical;
        outDepth = face.z / range;
        return;
    }

    const vec2 lightXY = vec2(dot(world, slice.axisX.xyz), dot(world, slice.axisY.xyz));
    const vec2 localTexel = (lightXY - page * pageMeters) / texelMeters;
    const float lightDepth = dot(world, slice.axisZ.xyz);
    const float depth01 = (lightDepth - params.depth.x) * params.depth.y + 0.5;

    gl_Position = vec4(localTexel / (0.5 * float(VSM_PAGE_RESOLUTION)) - vec2(1.0), depth01, 1.0);
    outPhysicalPage = physical;
    outDepth = depth01;
}
