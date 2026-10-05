// ========================================
// ビジビリティバッファの幾何の解決（画素の ID から三角形を引き、重心座標・微分・法線・速度を求める）
//
// 取り込む側（visbuffer_resolve.comp など）が先に #version 460 を書く。NORVES_VISRESOLVE_DUMP を定義して取り込むと、
// 画素ごとの中間の値（重心座標・UV・微分・接線の基底・前のクリップ座標）を検証用のバッファ（binding 9）へも書く
// （GPU のテストが CPU の参照と照合する。製品の経路は定義しない）。
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
//
// 規約:
//   - 光線はカメラ相対（原点がカメラの位置）で交わらせる。大きなワールド座標でも交点の精度が落ちない。
//   - 微分の y 方向は、画素の y が増える向き（Vulkan の dFdy と同じ向き）の差分。接線の基底は CalculateCotangentFrame と
//     同じく、位置と UV の dFdy を反転して作る。
//   - GBuffer の形式・意味（Albedo.a、法線の格納、Velocity の式）はラスタの経路と同じ。
//     Albedo = 材質の基本色（テクスチャなし。α = 1）、Normal = ワールド法線、Velocity = (現在の NDC - 前の NDC) * 0.5。
//
// 材質のテクスチャ・ORM・発光は、この関数が返す微分と接線の基底を使う別の解決が受け持つ。
// ========================================

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#ifndef NORVES_VISIBILITY_RESOLVE_GLSL
#define NORVES_VISIBILITY_RESOLVE_GLSL

#define VIS_RECORD_TABLE_SET 0
#define VIS_RECORD_TABLE_BINDING 1
#include "Common/VisibilityBuffer.glsl"
#include "Common/PbrMaterialEvaluation.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, set = 0, binding = 0) uniform ResolveParams
{
    mat4 invView;          // ビュー空間 → ワールド
    mat4 invProj;          // クリップ → ビュー空間（ラスタと同じ投影。ジッターを含む）
    mat4 previousViewProj; // 前のフレームのカメラの投影 * ビュー（前のカメラが無いときは使わない）
    vec4 cameraPosition;   // xyz = ワールドのカメラ位置
    vec4 viewport;         // x, y, 幅, 高さ（画素。ラスタの描画範囲）
    uvec4 screen;          // x = 画面の幅, y = 画面の高さ, z = フラグ, w = 材質の表の件数
} params;

// params.screen.z のビット
const uint RESOLVE_FLAG_PREVIOUS_VALID = 1u;

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

#ifdef NORVES_VISRESOLVE_DUMP
// 画素ごとに VIS_RESOLVE_DUMP_STRIDE 個の vec4 を書く（並びは VisibilityResolvePass.h の ResolveDump と同じ）
layout(std430, set = 0, binding = 9) writeonly buffer ResolveDump
{
    vec4 dump[];
};
const uint VIS_RESOLVE_DUMP_STRIDE = 12u;
#endif

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
    vec3 normal[3];    // ワールド法線（正規化前）
    vec2 uv[3];
};

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
            triangle.normal[k] = mat3(instance.world) * localNormal;
        }
        else if (kind == VIS_KIND_PROCEDURAL_CHUNK)
        {
            // gbuffer.vert と同じ式: 法線は法線行列の行から作る
            const VisDrawInstance instance = drawInstances[instanceIndex];
            triangle.position[k] = (instance.world * vec4(local, 1.0)).xyz;
            triangle.normal[k] = instance.normalRows[0].xyz * localNormal.x +
                                 instance.normalRows[1].xyz * localNormal.y +
                                 instance.normalRows[2].xyz * localNormal.z;
            triangle.previous[k] = bHasPreviousTransform
                                       ? (drawInstances[previousTransformIndex].previousWorld * vec4(local, 1.0)).xyz
                                       : triangle.position[k];
        }
        else
        {
            // スキニング: 頂点は変形済みのワールド空間。前のフレームの頂点は別の列から同じ頂点番号で読む
            triangle.position[k] = local;
            triangle.normal[k] = localNormal;
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

void main()
{
    const uvec2 pixel = gl_GlobalInvocationID.xy;
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

    VisTriangle triangle;
    if (!VisLoadTriangle(record, triangleIndex, triangle))
    {
        return;
    }

    const vec2 pixelCenter = vec2(pixel) + vec2(0.5);
    const VisResolvedPixel resolved = VisResolveTriangle(triangle, pixelCenter);
    const vec2 velocity = VisComputeVelocity(pixelCenter, resolved.previousClip);

    // 材質の基本色（テクスチャなし）。表から引けない材質の番号は白
    vec4 albedo = vec4(1.0);
    const uint materialIndex = VisRecordMaterialIndex(record);
    if (materialIndex < uint(materials.length()))
    {
        albedo = vec4(materials[materialIndex].baseColor.rgb, 1.0);
    }

    imageStore(albedoImage, ivec2(pixel), albedo);
    imageStore(normalImage, ivec2(pixel), vec4(resolved.normal, 0.0));
    imageStore(velocityImage, ivec2(pixel), vec4(velocity, 0.0, 0.0));

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
