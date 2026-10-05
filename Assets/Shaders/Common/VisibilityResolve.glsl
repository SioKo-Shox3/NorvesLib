// ========================================
// ビジビリティバッファの幾何の解決（画素の ID から三角形を引き、重心座標・微分・法線・速度を求める）
//
// 取り込む側（visbuffer_resolve.comp など）が先に #version 460 を書く。NORVES_VISRESOLVE_DUMP を定義して取り込むと、
// 画素ごとの中間の値（重心座標・UV・微分・接線の基底・前のクリップ座標）を検証用のバッファ（binding 11）へも書く
// （GPU のテストが CPU の参照と照合する。製品の経路は定義しない）。
//
// NORVES_VISRESOLVE_TILES を定義して取り込むと、画面全体の直接 dispatch の代わりに、材質ごとのタイルの一覧
// （MaterialTileClassifyPass が作る引数と一覧）から走る版になる。1 回の dispatch は 1 つの材質（定数 ResolveTileParams.tile.y）で、
//   グループの番号 g = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x（引数の x の上限によらず正しい）、
//   g >= その材質のタイルの数なら return、そうでなければ一覧[先頭位置 + g] のタイル（番号 = tileY * tilesX + tileX）を処理する。
// タイルには複数の材質が混じりうるので、各スレッドは自分の画素の材質がこの dispatch の材質と同じときだけ解決する
// （画素は自分の材質の dispatch でちょうど 1 回だけ解決される）。画面の外の画素は何もしない。
// 材質の番号が分類の上限以上の画素はどの材質の一覧にも入らないので、この版では解決されない（直接版は解決する）。
// 追加の束縛は、検証用の書き出しがあれば binding 12 から、無ければ binding 11 から（VIS_TILE_BINDING）。
// 材質ごとの形は、その材質のテクスチャ（アルベド・法線・金属度・粗さ・AO・高さの 6 枠。VIS_TILE_BINDING + 3 から）も束ねて、
// 材質のテクスチャで GBuffer の Albedo・Normal・Material を書く（直接 dispatch の版は、テクスチャを束ねられないので
// 材質の定数だけで書く）。
//
// 1 スレッドが 1 画素を受け持つ（8x8 のワークグループ = 画面のタイル）。
//   1. VisBuffer.Id の画素から描画の記録と三角形の番号を引く（空・引けない画素は何も書かない）。
//   2. 記録の頂点・インデックスのアドレスから 3 頂点（位置・法線・UV）を読み、ワールド空間にする。
//        MegaGeometry のクラスタ・手続きメッシュの塊: ローカルの頂点にインスタンスの変換を掛ける。
//        スキニングの塊: 計算シェーダーが変形したワールド空間の頂点で、変換を掛けない。
//   3. 画素の中心を通るカメラの光線と三角形の交点から、透視の補正つきの重心座標を求める（ハードウェアの補間と同じ値）。
//      隣の画素（x+1、y+1）の光線でも同じ三角形の平面と交わらせ、位置・UV の解析的な微分（前進差分）を得る。
//   4. 法線（補間して正規化）・接線の基底（三角形の辺と UV。ラスタの CalculateCotangentFrame と同じ向きの規約）・
//      速度（前のフレームの頂点から求めた前のクリップ座標）を求め、GBuffer へ書く。
//   5. （材質ごとの形だけ）材質のテクスチャを、画面微分の代わりに三角形から求めた解析的な微分（textureGrad）で標本する。
//      視差オクルージョンマッピング（POM）は三角形の接線の基底と解析的な UV の微分で行い、法線マップは POM の後の UV で標本して
//      元の UV の接線の基底で変換する（ラスタの gbuffer.frag と同じ順序）。VT（sparse）の材質は、非常駐のタイルを読まず
//      粗いミップへ逃げる（逃げ始めのミップは勾配から求める）。標本・復号・POM の式は、ラスタと共有する
//      Common/PbrMaterialTextureSamplingCore.glsl・Common/ParallaxOcclusionMappingCore.glsl の関数をそのまま呼ぶ。
//   6. （材質ごとの形だけ）VT の要求（フィードバック）を、ラスタの材質シェーダーと同じ規則で書く: アルベド・法線・ORM は POM の後の UV、
//      高さは元の UV で、4×4 の画素のうちフレームごとに巡回する 1 画素と、非常駐で粗いミップへ逃げた画素が書く。欲しいミップは
//      textureQueryLOD の代わりに、解析的な微分から求めた値（VisQueryLodFromGradient）。要求のバッファの束縛は材質ごとの形の
//      追加の束縛の最後（VIS_TILE_BINDING + 9。対応するデバイスだけ）。
//
// 規約:
//   - 光線はカメラ相対（原点がカメラの位置）で交わらせる。大きなワールド座標でも交点の精度が落ちない。
//   - 微分の y 方向は、画素の y が増える向き（Vulkan の dFdy と同じ向き）の差分。接線の基底は CalculateCotangentFrame と
//     同じく、位置と UV の dFdy を反転して作る。
//   - GBuffer の形式・意味（Albedo.a、法線の格納、Velocity の式）はラスタの経路と同じ。
//     Albedo = インスタンスの色 × アルベドのテクスチャ（α = テクスチャの α）、Normal = 法線マップを適用したワールド法線、
//     Material = 金属度・粗さ・AO、Velocity = (現在の NDC - 前の NDC) * 0.5、
//     Emissive = 材質の表の発光（色度 × 輝度 × フレームのプリエクスポージャ。65504 で頭打ち。α = 1。表の外の材質は 0）。
//     発光はテクスチャを引かず材質の表の件だけで決まるので、直接 dispatch・材質ごとの形のどちらも同じ式で書く。
//   - インスタンスの色は、MegaGeometry は材質の表の基本色（区間の材質の値）、手続きメッシュはインスタンスの表の色、
//     スキニングは 1。
//
// デバッグの表示（クラスタの色・LOD の段・ワイヤーフレーム）は、別の解決が受け持つ。
// ========================================

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#if defined(NORVES_VISRESOLVE_TILES) && defined(NORVES_SPARSE_RESIDENCY_SHADING)
#extension GL_ARB_sparse_texture2 : require
#endif

#ifndef NORVES_VISIBILITY_RESOLVE_GLSL
#define NORVES_VISIBILITY_RESOLVE_GLSL

#define VIS_RECORD_TABLE_SET 0
#define VIS_RECORD_TABLE_BINDING 1
#include "Common/VisibilityBuffer.glsl"
#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/PreExposedEmissive.glsl"
#include "Common/MegaGeometryDebugColor.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, set = 0, binding = 0) uniform ResolveParams
{
    mat4 invView;          // ビュー空間 → ワールド
    mat4 invProj;          // クリップ → ビュー空間（ラスタと同じ投影。ジッターを含む）
    mat4 previousViewProj; // 前のフレームのカメラの投影 * ビュー（前のカメラが無いときは使わない）
    vec4 cameraPosition;   // xyz = ワールドのカメラ位置
    vec4 viewport;         // x, y, 幅, 高さ（画素。ラスタの描画範囲）
    uvec4 screen;          // x = 画面の幅, y = 画面の高さ, z = フラグ, w = 材質の表の件数
    vec4 frame;            // x = 発光に掛けるプリエクスポージャ（ラスタの frameParams.y と同じ値）, y = MegaGeometry のデバッグの表示（RESOLVE_DEBUG_*）
} params;

// params.screen.z のビット
const uint RESOLVE_FLAG_PREVIOUS_VALID = 1u;

// params.frame.y の値（MegaGeometry のデバッグの表示。MegaGeometryPass の DEBUG_PAYLOAD_MODE_* と同じ並び）。
// 0 でなければ、MegaGeometry のクラスタの画素は材質を引かず、描画の記録の payload（クラスタの番号・LOD の段）から色を作る
const uint RESOLVE_DEBUG_NONE = 0u;
const uint RESOLVE_DEBUG_CLUSTERS = 1u;
const uint RESOLVE_DEBUG_LOD_LEVEL = 2u;

// binding 1: 描画の記録の表（Common/VisibilityBuffer.glsl が宣言する）

layout(set = 0, binding = 2) uniform usampler2D idTexture;

// フレームの材質の表の 1 件（VisibilityMaterialTable.h の MaterialEntry と同じ 128 バイト）
struct VisMaterialEntry
{
    vec4 baseColor;
    vec4 emissive;
    vec4 scalars;
    uvec4 header;
    uvec4 texturesA;
    uvec4 texturesB;
    uvec4 texturesC;
    uvec4 texturesD;
};

layout(std430, set = 0, binding = 3) readonly buffer MaterialTable
{
    VisMaterialEntry materials[];
};

// MegaGeometry のインスタンスの表（cluster_cull.comp の MegaInstance と一致）
struct VisMegaInstance
{
    mat4 world;
    mat4 previousWorld;
    vec4 lodSphere;
    uvec4 clusterInfo;
    uvec4 drawInfo;
    uvec4 bvhInfo;
};

layout(std430, set = 0, binding = 4) readonly buffer MegaInstanceBuffer
{
    VisMegaInstance megaInstances[];
};

// 描画のインスタンスの表（gbuffer.vert の InstanceData と一致）
struct VisDrawInstance
{
    mat4 world;
    mat4 previousWorld;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 5) readonly buffer DrawInstanceBuffer
{
    VisDrawInstance drawInstances[];
};

layout(set = 0, binding = 6, rgba8) writeonly uniform image2D albedoImage;
layout(set = 0, binding = 7, rgba16f) writeonly uniform image2D normalImage;
layout(set = 0, binding = 8, rg16f) writeonly uniform image2D velocityImage;
layout(set = 0, binding = 9, rgba8) writeonly uniform image2D materialImage;
layout(set = 0, binding = 10, rgba16f) writeonly uniform image2D emissiveImage;

#ifdef NORVES_VISRESOLVE_TILES
#ifdef NORVES_VISRESOLVE_DUMP
#define VIS_TILE_BINDING 12
#else
#define VIS_TILE_BINDING 11
#endif

// 1 回の dispatch ごとの定数（RHI に push constant が無いので UBO。dispatch ごとに別の UBO を束ねる）
layout(std140, set = 0, binding = VIS_TILE_BINDING) uniform ResolveTileParams
{
    // x = 横のタイル数、y = この dispatch が解決する材質の番号、z = VIS_TILE_FLAG_*、w = 予約
    uvec4 tile;
    // VT のフィードバックのパラメータ（VirtualTextureFeedbackMaterial.h の ResolveVirtualTextureFeedbackParam。0 は書かない）:
    // x = アルベド、y = 法線、z = ORM（金属度の枠）、w = 高さ
    uvec4 vt;
} tileParams;

// tileParams.tile.z のビット（VisibilityResolvePass.h の TILE_FLAG_* と同じ）
const uint VIS_TILE_FLAG_ORM = 1u;     // ORM の 1 枚が metallicTexture の枠に張られている
const uint VIS_TILE_FLAG_SPARSE = 2u;  // 張ったテクスチャに sparse（VT）が 1 枚でもある

// MaterialTileClassifyPass の引数（材質ごとに 8 語）と一覧（タイルの番号の並び）。MaterialTileClassifyPass.h と同じ並び
layout(std430, set = 0, binding = VIS_TILE_BINDING + 1) readonly buffer ResolveTileArgs
{
    uint tileArgWords[];
};

layout(std430, set = 0, binding = VIS_TILE_BINDING + 2) readonly buffer ResolveTileList
{
    uint tileList[];
};

const uint VIS_TILE_ARGS_STRIDE_WORDS = 8u;
const uint VIS_TILE_ARG_LIST_OFFSET = 3u;
const uint VIS_TILE_ARG_TILE_COUNT = 4u;

// この dispatch の材質のテクスチャ（GBufferPass の材質の descriptor の 1〜6 と同じ枠の並び。サンプラーは異方性 4）
layout(set = 0, binding = VIS_TILE_BINDING + 3) uniform sampler2D albedoTexture;
layout(set = 0, binding = VIS_TILE_BINDING + 4) uniform sampler2D normalTexture;
layout(set = 0, binding = VIS_TILE_BINDING + 5) uniform sampler2D metallicTexture;
layout(set = 0, binding = VIS_TILE_BINDING + 6) uniform sampler2D roughnessTexture;
layout(set = 0, binding = VIS_TILE_BINDING + 7) uniform sampler2D aoTexture;
layout(set = 0, binding = VIS_TILE_BINDING + 8) uniform sampler2D heightTexture;

#include "Common/SparseResidencySampling.glsl"
// 計算シェーダーには暗黙の勾配が無いので、VT でない標本も解析的な微分で textureGrad する
#define NORVES_MATERIAL_SAMPLING_EXPLICIT_GRADIENT 1
#include "Common/PbrMaterialTextureSamplingCore.glsl"
#include "Common/ParallaxOcclusionMappingCore.glsl"

// VT の要求のバッファ（対応するデバイスだけ NORVES_VT_FEEDBACK が定義される）。計算シェーダーなので、フラグメント専用の宣言と
// gl_FragCoord の関数を除き、画素の位置を引数に取る版を使う
#define VT_FEEDBACK_BINDING (VIS_TILE_BINDING + 9)
#define NORVES_VT_FEEDBACK_COMPUTE 1
#include "Common/VirtualTextureFeedback.glsl"
#endif

#ifdef NORVES_VISRESOLVE_DUMP
// 画素ごとに VIS_RESOLVE_DUMP_STRIDE 個の vec4 を書く（並びは VisibilityResolvePass.h の ResolveDump と同じ）
layout(std430, set = 0, binding = 11) writeonly buffer ResolveDump
{
    vec4 dump[];
};
const uint VIS_RESOLVE_DUMP_STRIDE = 12u;
#endif

// 材質の表の MaterialEntry::Header[0] のビット（VisibilityMaterialTable.h の MATERIAL_FLAG_* と同じ）
const uint VIS_MATERIAL_FLAG_NORMAL_TWO_CHANNEL = 1u;
const uint VIS_MATERIAL_FLAG_HAS_HEIGHT = 2u;
// MegaGeometry の区間の材質。ラスタの MegaGeometryPass は等方の Linear のサンプラーで標本し、粗さが無いときは白（1）を張る
const uint VIS_MATERIAL_FLAG_MEGA_GEOMETRY = 4u;

// ========================================
// 頂点の読み出し（デバイスアドレスから）
// ========================================

// 手続きメッシュ・MegaGeometry・スキニングの出力はどれも 1 頂点 8 個の float（位置 3・法線 3・UV 2）
struct VisVertex
{
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VisVertexArray
{
    VisVertex vertices[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VisIndexWords
{
    uint words[];
};

uint VisLoadIndex(VisIndexWords indices, bool bIndex16, uint position)
{
    if (bIndex16)
    {
        const uint word = indices.words[position >> 1];
        return (position & 1u) != 0u ? (word >> 16) : (word & 0xFFFFu);
    }
    return indices.words[position];
}

// 三角形の 3 頂点（ワールド空間）
struct VisTriangle
{
    vec3 position[3];  // ワールド位置（カメラ相対にする前）
    vec3 previous[3];  // 前のフレームのワールド位置
    vec3 normal[3];    // 頂点ごとに変換して正規化したワールド法線（ラスタの頂点シェーダーと同じ。補間は正規化しない）
    vec2 uv[3];
};

// 頂点の法線の正規化（ラスタの頂点シェーダーが、変換した頂点の法線を補間の前に正規化するのと同じ。退化した法線は 0）
vec3 VisNormalizeVertexNormal(vec3 normal)
{
    return dot(normal, normal) > 0.000001 ? normalize(normal) : vec3(0.0);
}

// 記録から三角形 triangleIndex の 3 頂点を読む。アドレスが無い（BufferDeviceAddress が無い）・インスタンスが表の外なら false
bool VisLoadTriangle(VisibilityDrawRecord record, uint triangleIndex, out VisTriangle triangle)
{
    triangle.position[0] = vec3(0.0);
    triangle.position[1] = vec3(0.0);
    triangle.position[2] = vec3(0.0);
    triangle.previous[0] = vec3(0.0);
    triangle.previous[1] = vec3(0.0);
    triangle.previous[2] = vec3(0.0);
    triangle.normal[0] = vec3(0.0, 1.0, 0.0);
    triangle.normal[1] = vec3(0.0, 1.0, 0.0);
    triangle.normal[2] = vec3(0.0, 1.0, 0.0);
    triangle.uv[0] = vec2(0.0);
    triangle.uv[1] = vec2(0.0);
    triangle.uv[2] = vec2(0.0);

    const uvec2 vertexAddress = VisRecordVertexAddress(record);
    const uvec2 indexAddress = VisRecordIndexAddress(record);
    if ((vertexAddress.x | vertexAddress.y) == 0u || (indexAddress.x | indexAddress.y) == 0u)
    {
        return false;
    }

    const uint kind = VisRecordKind(record);
    const uint instanceIndex = VisRecordInstanceIndex(record);
    const uint previousTransformIndex = VisRecordPreviousTransformIndex(record);
    const uvec2 previousAddress = VisRecordPreviousVertexAddress(record);
    const bool bHasPreviousVertices = (previousAddress.x | previousAddress.y) != 0u;

    if (kind == VIS_KIND_MEGA_CLUSTER && instanceIndex >= uint(megaInstances.length()))
    {
        return false;
    }
    if (kind == VIS_KIND_PROCEDURAL_CHUNK && instanceIndex >= uint(drawInstances.length()))
    {
        return false;
    }
    const bool bHasPreviousTransform = previousTransformIndex != VIS_NO_PREVIOUS_TRANSFORM &&
                                       previousTransformIndex < uint(drawInstances.length());

    // reference 型の変数には const を付けられない
    VisIndexWords indices = VisIndexWords(indexAddress);
    VisVertexArray vertices = VisVertexArray(vertexAddress);
    const bool bIndex16 = VisRecordHasIndex16(record);
    // 頂点の基点は符号つき（インデックスに足す）
    const int vertexBase = int(VisRecordVertexBase(record));

    for (uint k = 0u; k < 3u; ++k)
    {
        const uint index = VisLoadIndex(indices, bIndex16, VisTriangleIndexPosition(record, triangleIndex, k));
        const uint vertexIndex = uint(int(index) + vertexBase);
        const VisVertex vertex = vertices.vertices[vertexIndex];
        const vec3 local = vec3(vertex.px, vertex.py, vertex.pz);
        const vec3 localNormal = vec3(vertex.nx, vertex.ny, vertex.nz);

        if (kind == VIS_KIND_MEGA_CLUSTER)
        {
            // megageometry.vert と同じ式: 法線は変換の上 3x3 をそのまま掛ける
            const VisMegaInstance instance = megaInstances[instanceIndex];
            triangle.position[k] = (instance.world * vec4(local, 1.0)).xyz;
            triangle.previous[k] = (instance.previousWorld * vec4(local, 1.0)).xyz;
            triangle.normal[k] = VisNormalizeVertexNormal(mat3(instance.world) * localNormal);
        }
        else if (kind == VIS_KIND_PROCEDURAL_CHUNK)
        {
            // gbuffer.vert と同じ式: 法線は法線行列の行から作る
            const VisDrawInstance instance = drawInstances[instanceIndex];
            triangle.position[k] = (instance.world * vec4(local, 1.0)).xyz;
            triangle.normal[k] = VisNormalizeVertexNormal(instance.normalRows[0].xyz * localNormal.x +
                                                          instance.normalRows[1].xyz * localNormal.y +
                                                          instance.normalRows[2].xyz * localNormal.z);
            triangle.previous[k] = bHasPreviousTransform
                                       ? (drawInstances[previousTransformIndex].previousWorld * vec4(local, 1.0)).xyz
                                       : triangle.position[k];
        }
        else
        {
            // スキニング: 頂点は変形済みのワールド空間。前のフレームの頂点は別の列から同じ頂点番号で読む
            triangle.position[k] = local;
            triangle.normal[k] = VisNormalizeVertexNormal(localNormal);
            if (bHasPreviousVertices)
            {
                const VisVertex previousVertex = VisVertexArray(previousAddress).vertices[vertexIndex];
                triangle.previous[k] = vec3(previousVertex.px, previousVertex.py, previousVertex.pz);
            }
            else
            {
                triangle.previous[k] = local;
            }
        }
        triangle.uv[k] = vec2(vertex.u, vertex.v);
    }
    return true;
}

// ========================================
// 光線と三角形
// ========================================

// 画素の位置（中心なら +0.5 した値）を通る光線（カメラ相対のワールド空間。原点がカメラの位置）。
// 深度 0.25 と 0.75 の 2 点（標準・反転のどちらの深度でも有限）から作るので、直交投影（光線が平行）でも成り立つ。
// ビュー空間の位置をそのまま回すだけで、大きなワールド座標を引かない。
void VisPixelRay(vec2 pixel, out vec3 origin, out vec3 direction)
{
    const vec2 ndc = (pixel - params.viewport.xy) / params.viewport.zw * 2.0 - 1.0;
    const vec4 nearPoint = params.invProj * vec4(ndc, 0.25, 1.0);
    const vec4 farPoint = params.invProj * vec4(ndc, 0.75, 1.0);
    const mat3 rotation = mat3(params.invView);
    const vec3 viewNear = nearPoint.xyz / nearPoint.w;
    const vec3 viewFar = farPoint.xyz / farPoint.w;
    origin = rotation * viewNear;
    direction = rotation * (viewFar - viewNear);
}

// 光線（origin, direction）と、頂点 p0 と辺 e1・e2 の三角形の平面との交点の重心座標（Möller-Trumbore。すべてカメラ相対）。
// 三角形の外でも平面との交点の重心座標を返す（微分を作るために隣の画素の光線も同じ平面と交わらせる）。
// 光線が平面と平行（行列式が 0）のときは、頂点 0 の重心座標を返す。
vec3 VisIntersectBarycentric(vec3 p0, vec3 e1, vec3 e2, vec3 origin, vec3 direction)
{
    const vec3 pvec = cross(direction, e2);
    const float det = dot(e1, pvec);
    if (abs(det) < 1.0e-30)
    {
        return vec3(1.0, 0.0, 0.0);
    }
    const float invDet = 1.0 / det;
    const vec3 tvec = origin - p0;
    const float u = dot(tvec, pvec) * invDet;
    const vec3 qvec = cross(tvec, e1);
    const float v = dot(direction, qvec) * invDet;
    return vec3(1.0 - u - v, u, v);
}

// 画素 pixel を通る光線と三角形の平面の交点の重心座標
vec3 VisPixelBarycentric(vec3 p0, vec3 e1, vec3 e2, vec2 pixel)
{
    vec3 origin;
    vec3 direction;
    VisPixelRay(pixel, origin, direction);
    return VisIntersectBarycentric(p0, e1, e2, origin, direction);
}

vec3 VisInterpolate(vec3 a, vec3 b, vec3 c, vec3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

vec2 VisInterpolate(vec2 a, vec2 b, vec2 c, vec3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

// 画素の微分から接線の基底（T = +∇u、B = +∇v、N）を作る。CalculateCotangentFrame（ParallaxOcclusionMapping.glsl）と同じ
// 式・同じ向き。dpdy・duvdy は画素の y が増える向きの差分（Vulkan の dFdy と同じ）で、ここで反転して使う。
mat3 VisCotangentFrame(vec3 worldNormal, vec3 dpdx, vec3 dpdy, vec2 duvdx, vec2 duvdy)
{
    const vec3 dp1 = dpdx;
    const vec3 dp2 = -dpdy;
    const vec2 duv1 = duvdx;
    const vec2 duv2 = -duvdy;

    const vec3 N = normalize(worldNormal);
    const vec3 dp2perp = cross(dp2, N);
    const vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    const float maxLen2 = max(dot(T, T), dot(B, B));
    const float positionScale = max(dot(dp1, dp1), dot(dp2, dp2));
    const float uvScale = max(dot(duv1, duv1), dot(duv2, duv2));
    if (IsCotangentFrameDegenerate(maxLen2, positionScale, uvScale))
    {
        const vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
        B = cross(N, T);
        return mat3(T, B, N);
    }

    const float invmax = inversesqrt(maxLen2);
    return mat3(T * invmax, B * invmax, N);
}

// 画素の解決の結果
struct VisResolvedPixel
{
    vec3 barycentric;    // 透視の補正つきの重心座標
    vec2 uv;
    vec2 duvdx;          // 画素の x が 1 増えたときの UV の変化
    vec2 duvdy;          // 画素の y が 1 増えたときの UV の変化（Vulkan の dFdy と同じ向き）
    vec3 hitPosition;    // カメラ相対のワールド位置
    vec3 dposdx;
    vec3 dposdy;
    vec3 normal;         // 補間して正規化したワールド法線
    mat3 tangentFrame;   // T, B, N（CalculateCotangentFrame と同じ規約）
    vec4 previousClip;   // 前のフレームのカメラの、同じ点のクリップ座標
    vec3 viewDir;        // 表面からカメラへ向かう単位のワールド方向（ラスタの fragViewDir と同じ作り方）
};

// pixelCenter は画素の中心（整数の画素の位置 + 0.5）
VisResolvedPixel VisResolveTriangle(VisTriangle triangle, vec2 pixelCenter)
{
    const vec3 cameraPosition = params.cameraPosition.xyz;
    const vec3 p0 = triangle.position[0] - cameraPosition;
    const vec3 e1 = triangle.position[1] - triangle.position[0];
    const vec3 e2 = triangle.position[2] - triangle.position[0];

    const vec3 baryCenter = VisPixelBarycentric(p0, e1, e2, pixelCenter);
    const vec3 baryX = VisPixelBarycentric(p0, e1, e2, pixelCenter + vec2(1.0, 0.0));
    const vec3 baryY = VisPixelBarycentric(p0, e1, e2, pixelCenter + vec2(0.0, 1.0));

    VisResolvedPixel result;
    result.barycentric = baryCenter;

    const vec3 q0 = triangle.position[0] - cameraPosition;
    const vec3 q1 = triangle.position[1] - cameraPosition;
    const vec3 q2 = triangle.position[2] - cameraPosition;
    result.hitPosition = VisInterpolate(q0, q1, q2, baryCenter);
    result.dposdx = VisInterpolate(q0, q1, q2, baryX) - result.hitPosition;
    result.dposdy = VisInterpolate(q0, q1, q2, baryY) - result.hitPosition;

    result.uv = VisInterpolate(triangle.uv[0], triangle.uv[1], triangle.uv[2], baryCenter);
    result.duvdx = VisInterpolate(triangle.uv[0], triangle.uv[1], triangle.uv[2], baryX) - result.uv;
    result.duvdy = VisInterpolate(triangle.uv[0], triangle.uv[1], triangle.uv[2], baryY) - result.uv;

    const vec3 interpolatedNormal =
        VisInterpolate(triangle.normal[0], triangle.normal[1], triangle.normal[2], baryCenter);
    result.normal = normalize(interpolatedNormal);
    result.tangentFrame = VisCotangentFrame(interpolatedNormal, result.dposdx, result.dposdy, result.duvdx, result.duvdy);

    const vec3 previousPosition =
        VisInterpolate(triangle.previous[0], triangle.previous[1], triangle.previous[2], baryCenter);
    result.previousClip = params.previousViewProj * vec4(previousPosition, 1.0);

    // 頂点ごとに正規化した視線を補間する（ラスタの頂点シェーダーの fragViewDir と同じ。POM が受け取って再び正規化する）
    const vec3 view0 = normalize(cameraPosition - triangle.position[0]);
    const vec3 view1 = normalize(cameraPosition - triangle.position[1]);
    const vec3 view2 = normalize(cameraPosition - triangle.position[2]);
    result.viewDir = VisInterpolate(view0, view1, view2, baryCenter);
    return result;
}

// ラスタの経路（gbuffer.frag）と同じ式・同じ無効の扱いの速度（現在の NDC - 前の NDC の半分）
vec2 VisComputeVelocity(vec2 pixelCenter, vec4 previousClip)
{
    if ((params.screen.z & RESOLVE_FLAG_PREVIOUS_VALID) == 0u || abs(previousClip.w) <= 1.0e-6)
    {
        return vec2(0.0);
    }
    const vec2 currentNdc = (pixelCenter - params.viewport.xy) / params.viewport.zw * 2.0 - 1.0;
    const vec2 previousNdc = previousClip.xy / previousClip.w;
    const vec2 velocity = (currentNdc - previousNdc) * 0.5;
    if (any(notEqual(velocity, velocity)) || dot(velocity, velocity) >= 1.0e6)
    {
        return vec2(0.0);
    }
    return velocity;
}

// ========================================
// 材質の評価（GBuffer の Albedo・Normal・Material を作る）
// ========================================

// 材質の表の 1 件を引く。表の外なら false（entry は既定の値）
bool VisLoadMaterial(uint materialIndex, out VisMaterialEntry entry)
{
    entry = VisMaterialEntry(vec4(1.0), vec4(0.0), vec4(-1.0, -1.0, 0.05, 0.0), uvec4(0u), uvec4(0u), uvec4(0u),
                             uvec4(0u), uvec4(0u));
    if (materialIndex >= uint(materials.length()))
    {
        return false;
    }
    entry = materials[materialIndex];
    return true;
}

// ラスタの頂点シェーダーが fragObjectColor へ渡す色。MegaGeometry は区間の材質の基本色（材質の表の基本色）、
// 手続きメッシュはインスタンスの表の色、スキニングは 1
vec3 VisObjectColor(VisibilityDrawRecord record, VisMaterialEntry entry, bool bHasMaterial)
{
    const uint kind = VisRecordKind(record);
    if (kind == VIS_KIND_PROCEDURAL_CHUNK)
    {
        const uint instanceIndex = VisRecordInstanceIndex(record);
        return instanceIndex < uint(drawInstances.length()) ? drawInstances[instanceIndex].objectColor.rgb : vec3(1.0);
    }
    if (kind == VIS_KIND_SKINNED_CHUNK)
    {
        return vec3(1.0);
    }
    return bHasMaterial ? entry.baseColor.rgb : vec3(1.0);
}

// 画素の面の値（GBuffer の 3 枚に書くもの）
struct VisMaterialSurface
{
    vec4 albedo;     // rgb = インスタンスの色 × アルベド、a = アルベドのテクスチャの α
    vec3 normal;     // 法線マップを適用したワールド法線
    vec3 material;   // 金属度・粗さ・AO
};

// 8bit の 1x1 テクスチャの値（ラスタは材質のスカラー値を 1x1 の UNORM テクスチャにして張る）
float VisQuantize8(float value)
{
    return floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}

// 材質の定数だけで作る面（テクスチャを束ねられない直接 dispatch の版、表の外の材質）。
// 金属度・粗さは表のスカラー値（負は未指定）か、ラスタの既定のテクスチャ（金属度 0・粗さ 128/255・AO 1）の値
VisMaterialSurface VisFlatMaterialSurface(vec3 objectColor, VisMaterialEntry entry, bool bHasMaterial, VisResolvedPixel resolved)
{
    VisMaterialSurface surface;
    surface.albedo = vec4(objectColor, 1.0);
    surface.normal = resolved.normal;
    const float metallic = (bHasMaterial && entry.scalars.x >= 0.0) ? VisQuantize8(entry.scalars.x) : 0.0;
    // 粗さの既定は、手続き・スキニング（GBufferPass）が中間灰、MegaGeometry（MegaGeometryPass）が白
    const bool bMegaGeometry = bHasMaterial && (entry.header.x & VIS_MATERIAL_FLAG_MEGA_GEOMETRY) != 0u;
    const float roughness = (bHasMaterial && entry.scalars.y >= 0.0) ? VisQuantize8(entry.scalars.y)
                                                                      : (bMegaGeometry ? 1.0 : 128.0 / 255.0);
    surface.material = vec3(metallic, roughness, 1.0);
    return surface;
}

#ifdef NORVES_VISRESOLVE_TILES
// 材質のサンプラーの異方性の上限（手続き・スキニングの GBufferPass の既定のサンプラー maxAnisotropy = 4 と同じ）。
// MegaGeometry の材質は等方の Linear（異方性なし）なので、bIsotropic で標本の数を 1 にする
const float VIS_MATERIAL_MAX_ANISOTROPY = 4.0;

// 勾配から、textureQueryLOD(tex, uv).y 相当のミップ（λ = log2(Pmax / N)）を求める。計算シェーダーは textureQueryLOD の
// 暗黙の微分を使えない。VT の非常駐の逃げ始めのミップと、VT のフィードバックの欲しいミップに使う。
// bIsotropic（MegaGeometry の等方のサンプラー）のときは N = 1（λ = log2(Pmax)。Vulkan の異方性なしの式）。
// N（異方性の標本の数）は、Vulkan の仕様の式 min(ceil(Pmax / Pmin), 上限) でなく clamp(floor(Pmax / Pmin), 1, 上限)。
// 実測（NVIDIA 610.88、起動画面の 3 視点、常駐したタイルの数のラスタの経路との比）: ceil は 1.5〜2.0 倍、連続値は 1.22 倍、
// 等方（N = 1）は 0.65 倍、floor は 1.02〜1.11 倍で、ラスタの textureQueryLOD に最も近い（MegaGeometry の材質も異方性の式だったときの測定。
// MegaGeometry を等方にした後は 0.73・0.66・0.95 倍で、on の画像はぼけない）。別の装置では一致を前提にしない
float VisQueryLodFromGradient(sampler2D tex, vec2 uvDx, vec2 uvDy, bool bIsotropic)
{
    const vec2 size = vec2(textureSize(tex, 0));
    const float lengthX = length(uvDx * size);
    const float lengthY = length(uvDy * size);
    const float pMax = max(lengthX, lengthY);
    const float pMin = min(lengthX, lengthY);
    if (!(pMax > 0.0))
    {
        return 0.0;
    }
    const float ratio = bIsotropic ? 1.0
                                   : (pMin > 0.0 ? clamp(floor(pMax / pMin), 1.0, VIS_MATERIAL_MAX_ANISOTROPY)
                                                 : VIS_MATERIAL_MAX_ANISOTROPY);
    return clamp(log2(pMax / ratio), 0.0, float(textureQueryLevels(tex) - 1));
}

// 解析的な微分と、各テクスチャの標本ミップ（bQueryLods のとき）から標本の入力を作る（ラスタの QueryMaterialTextureFootprint の代わり）
MaterialTextureFootprint VisMakeFootprint(vec2 uvDx, vec2 uvDy, bool bHasORM, bool bQueryLods, bool bIsotropic)
{
    MaterialTextureFootprint footprint;
    footprint.UvDx = uvDx;
    footprint.UvDy = uvDy;
    footprint.AlbedoLod = 0.0;
    footprint.NormalLod = 0.0;
    footprint.MetallicLod = 0.0;
    footprint.RoughnessLod = 0.0;
    footprint.AoLod = 0.0;
    if (bQueryLods)
    {
        footprint.AlbedoLod = VisQueryLodFromGradient(albedoTexture, uvDx, uvDy, bIsotropic);
        footprint.NormalLod = VisQueryLodFromGradient(normalTexture, uvDx, uvDy, bIsotropic);
        footprint.MetallicLod = VisQueryLodFromGradient(metallicTexture, uvDx, uvDy, bIsotropic);
        if (!bHasORM)
        {
            footprint.RoughnessLod = VisQueryLodFromGradient(roughnessTexture, uvDx, uvDy, bIsotropic);
            footprint.AoLod = VisQueryLodFromGradient(aoTexture, uvDx, uvDy, bIsotropic);
        }
    }
    return footprint;
}

// 高さの場で変位させたメッシュ（MegaGeometry）の法線マップの補正（megageometry.frag の RemoveDisplacedNormalSlope と同じ式）。
// lodLevel は描いているクラスタの LOD の段
vec3 VisRemoveDisplacedNormalSlope(vec3 tangentNormal,
                                   vec2 texCoord,
                                   float displacementUVSpacing,
                                   float normalLod,
                                   float lodLevel,
                                   bool bNormalTwoChannel,
                                   bool bVirtualTexture)
{
    const float vertexMip = log2(max(displacementUVSpacing * float(textureSize(normalTexture, 0).x), 1.0)) + lodLevel;
    const float coarseMip = max(vertexMip, normalLod);
    const vec3 coarseNormal = DecodePbrTangentNormal(
        SampleMaterialTextureLod(normalTexture, texCoord, coarseMip, bVirtualTexture), bNormalTwoChannel);
    const vec2 detailSlope = tangentNormal.xy / max(tangentNormal.z, 0.1) - coarseNormal.xy / max(coarseNormal.z, 0.1);
    return normalize(vec3(detailSlope, 1.0));
}

// 材質のテクスチャで画素の面を作る（gbuffer.frag・megageometry.frag の main と同じ順序: POM → 標本 → 法線マップ）。
// 勾配は三角形から求めた解析的な微分（POM の前の UV の微分。ラスタは POM の後の UV の画面微分を使う）。
// 標本のあとで、VT の要求（フィードバック）も書く。pixel は画面の整数の画素の位置（巡回の位相に使う）
VisMaterialSurface VisEvaluateMaterialSurface(vec3 objectColor, VisMaterialEntry entry, VisibilityDrawRecord record,
                                              VisResolvedPixel resolved, uvec2 pixel)
{
    const bool bHasORM = (tileParams.tile.z & VIS_TILE_FLAG_ORM) != 0u;
    const bool bVirtualTexture = (tileParams.tile.z & VIS_TILE_FLAG_SPARSE) != 0u;
    const bool bNormalTwoChannel = (entry.header.x & VIS_MATERIAL_FLAG_NORMAL_TWO_CHANNEL) != 0u;
    const bool bHasHeight = (entry.header.x & VIS_MATERIAL_FLAG_HAS_HEIGHT) != 0u;
    // MegaGeometry の材質は、ラスタが等方の Linear のサンプラーで標本する（欲しいミップも等方の式）
    const bool bIsotropic = (entry.header.x & VIS_MATERIAL_FLAG_MEGA_GEOMETRY) != 0u;
    const float displacementUVSpacing = entry.scalars.w;
    const bool bDisplaced = VisRecordKind(record) == VIS_KIND_MEGA_CLUSTER && displacementUVSpacing > 0.0;

    vec2 texCoord = resolved.uv;
    // 高さのフィードバックの標本ミップ（POM の前の元の UV の勾配から。ラスタの textureQueryLOD(heightTexture, 元の UV) の代わり）
    float heightLod = 0.0;
    if (bHasHeight)
    {
        heightLod = bVirtualTexture ? VisQueryLodFromGradient(heightTexture, resolved.duvdx, resolved.duvdy, bIsotropic) : 0.0;
        texCoord = ApplyParallaxOcclusionMappingGrad(heightTexture, resolved.uv, resolved.tangentFrame, resolved.viewDir,
                                                     entry.scalars.z, bVirtualTexture, resolved.duvdx, resolved.duvdy,
                                                     heightLod);
    }

    const MaterialTextureFootprint footprint =
        VisMakeFootprint(resolved.duvdx, resolved.duvdy, bHasORM, bVirtualTexture || bDisplaced, bIsotropic);
    const PbrMaterialTextureSamples samples =
        SamplePbrMaterialTextures(albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord,
                                  footprint, bHasORM, bNormalTwoChannel, bVirtualTexture);

    // VT のフィードバック（ラスタの gbuffer.frag と同じ: アルベド・法線・ORM は POM の後の UV、高さは元の UV）
    WriteVirtualTextureFeedbackAtPixel(albedoTexture, texCoord, tileParams.vt.x,
                                       g_VirtualTextureAlbedoEscaped, footprint.AlbedoLod, pixel);
    WriteVirtualTextureFeedbackAtPixel(normalTexture, texCoord, tileParams.vt.y,
                                       g_VirtualTextureNormalEscaped, footprint.NormalLod, pixel);
    WriteVirtualTextureFeedbackAtPixel(metallicTexture, texCoord, tileParams.vt.z,
                                       g_VirtualTextureOrmEscaped, footprint.MetallicLod, pixel);
    if (bHasHeight)
    {
        WriteVirtualTextureHeightFeedbackAtPixel(heightTexture, resolved.uv,
                                                 tileParams.vt.w, heightLod, pixel);
    }

    vec3 tangentNormal = samples.TangentNormal;
    if (bDisplaced)
    {
        // 描画番号の payload が LOD の段でない（デバッグの表示用の番号を求められている）ときの大きな値は 31 で頭打ちにする
        const float lodLevel = float(min(VisRecordLodPayload(record), 31u));
        tangentNormal = VisRemoveDisplacedNormalSlope(tangentNormal, texCoord, displacementUVSpacing, footprint.NormalLod,
                                                      lodLevel, bNormalTwoChannel, bVirtualTexture);
    }

    VisMaterialSurface surface;
    surface.albedo = vec4(ComposePbrSurfaceAlbedo(objectColor, samples), samples.Albedo.a);
    surface.normal = ApplyTangentSpaceNormal(resolved.tangentFrame, tangentNormal);
    surface.material = samples.Material;
    return surface;
}
#endif

void main()
{
#ifdef NORVES_VISRESOLVE_TILES
    const uint groupIndex = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    const uint argBase = tileParams.tile.y * VIS_TILE_ARGS_STRIDE_WORDS;
    if (argBase + VIS_TILE_ARG_TILE_COUNT >= uint(tileArgWords.length()) ||
        groupIndex >= tileArgWords[argBase + VIS_TILE_ARG_TILE_COUNT])
    {
        return;
    }
    const uint listIndex = tileArgWords[argBase + VIS_TILE_ARG_LIST_OFFSET] + groupIndex;
    if (listIndex >= uint(tileList.length()))
    {
        return;
    }
    const uint tileIndex = tileList[listIndex];
    const uint tilesX = max(tileParams.tile.x, 1u);
    const uvec2 pixel = uvec2(tileIndex % tilesX, tileIndex / tilesX) * 8u + gl_LocalInvocationID.xy;
#else
    const uvec2 pixel = gl_GlobalInvocationID.xy;
#endif
    if (pixel.x >= params.screen.x || pixel.y >= params.screen.y)
    {
        return;
    }

    const uint id = texelFetch(idTexture, ivec2(pixel), 0).x;
    VisibilityDrawRecord record;
    uint triangleIndex;
    if (!VisLoadRecord(id, record, triangleIndex))
    {
        return;
    }
#ifdef NORVES_VISRESOLVE_TILES
    // タイルに混じる別の材質の画素は、その材質の dispatch が解決する
    if (VisRecordMaterialIndex(record) != tileParams.tile.y)
    {
        return;
    }
#endif

    VisTriangle triangle;
    if (!VisLoadTriangle(record, triangleIndex, triangle))
    {
        return;
    }

    const vec2 pixelCenter = vec2(pixel) + vec2(0.5);
    const VisResolvedPixel resolved = VisResolveTriangle(triangle, pixelCenter);
    const vec2 velocity = VisComputeVelocity(pixelCenter, resolved.previousClip);

    // 材質の表から引けない材質の番号は、白・幾何の法線の面にする
    const uint materialIndex = VisRecordMaterialIndex(record);
    VisMaterialEntry materialEntry;
    const bool bHasMaterial = VisLoadMaterial(materialIndex, materialEntry);
    const vec3 objectColor = VisObjectColor(record, materialEntry, bHasMaterial);
    // MegaGeometry のデバッグの表示（megageometry.frag の WriteDebugGBuffer と同じ面: 幾何の法線・金属度 0・粗さ 1・AO 1・発光 0）。
    // ラスタと同じく、材質のテクスチャは引かず VT の要求も書かない
    const uint debugView = uint(params.frame.y + 0.5);
    const bool bDebugView = debugView != RESOLVE_DEBUG_NONE && VisRecordKind(record) == VIS_KIND_MEGA_CLUSTER;
    VisMaterialSurface surface;
    if (bDebugView)
    {
        const uint payload = VisRecordLodPayload(record);
        surface.albedo = vec4(debugView == RESOLVE_DEBUG_CLUSTERS ? ClusterDebugColor(payload) : LODLevelDebugColor(payload), 1.0);
        surface.normal = resolved.normal;
        surface.material = vec3(0.0, 1.0, 1.0);
    }
#ifdef NORVES_VISRESOLVE_TILES
    // 表から引ける材質の dispatch だけがテクスチャを束ねている（引けない材質の番号は定数の面）
    else if (bHasMaterial)
    {
        surface = VisEvaluateMaterialSurface(objectColor, materialEntry, record, resolved, pixel);
    }
#endif
    else
    {
        surface = VisFlatMaterialSurface(objectColor, materialEntry, bHasMaterial, resolved);
    }

    imageStore(albedoImage, ivec2(pixel), surface.albedo);
    imageStore(normalImage, ivec2(pixel), vec4(surface.normal, 0.0));
    imageStore(materialImage, ivec2(pixel), vec4(surface.material, 0.0));
    imageStore(velocityImage, ivec2(pixel), vec4(velocity, 0.0, 0.0));
    // 発光: 色度 × 輝度（nits）× プリエクスポージャ（ラスタの gbuffer.frag・megageometry.frag と同じ関数）。表から引けない材質は 0
    const vec3 emissive = (bHasMaterial && !bDebugView)
                              ? ComputePreExposedEmissive(materialEntry.emissive.rgb, materialEntry.emissive.a, params.frame.x)
                              : vec3(0.0);
    imageStore(emissiveImage, ivec2(pixel), vec4(emissive, 1.0));

#ifdef NORVES_VISRESOLVE_DUMP
    const uint base = (pixel.y * params.screen.x + pixel.x) * VIS_RESOLVE_DUMP_STRIDE;
    dump[base + 0u] = vec4(resolved.barycentric, float(VisRecordKind(record)));
    dump[base + 1u] = vec4(resolved.uv, resolved.duvdx);
    dump[base + 2u] = vec4(resolved.duvdy, 0.0, 0.0);
    dump[base + 3u] = vec4(resolved.hitPosition, 0.0);
    dump[base + 4u] = vec4(resolved.dposdx, 0.0);
    dump[base + 5u] = vec4(resolved.dposdy, 0.0);
    dump[base + 6u] = vec4(resolved.tangentFrame[0], 0.0);
    dump[base + 7u] = vec4(resolved.tangentFrame[1], 0.0);
    dump[base + 8u] = vec4(resolved.tangentFrame[2], 0.0);
    dump[base + 9u] = resolved.previousClip;
    dump[base + 10u] = vec4(velocity, float(materialIndex), float(triangleIndex));
    dump[base + 11u] = vec4(0.0);
#endif
}

#endif // NORVES_VISIBILITY_RESOLVE_GLSL
