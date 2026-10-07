// 材質のテクスチャの標本（暗黙の微分を使わない部分）。フラグメントシェーダー（Common/PbrMaterialTextureSampling.glsl 経由）と
// 計算シェーダー（材質の解決。Common/VisibilityResolve.glsl）が共有する。
// 勾配（UvDx・UvDy）と標本ミップは呼び出し側が与える。フラグメントでは画面微分（QueryMaterialTextureFootprint）、
// 計算では三角形から解析的に求めた微分。
// Common/PbrMaterialEvaluation.glsl と Common/SparseResidencySampling.glsl の後に取り込む。

#ifndef NORVES_PBR_MATERIAL_TEXTURE_SAMPLING_CORE_GLSL
#define NORVES_PBR_MATERIAL_TEXTURE_SAMPLING_CORE_GLSL

// 材質の標本が使う勾配とミップ。フラグメントでは分岐・早期 return・ループの後で画面微分が未定義なので、
// QueryMaterialTextureFootprint で main の一様な位置（POM の直後）に一度だけ取り、標本・フィードバック・法線の粗い傾きへ引数で渡す。
// 計算シェーダーは画面微分を使えないので、解析的な微分と求めたミップを直接入れる。
struct MaterialTextureFootprint
{
    vec2 UvDx;       // 標本する uv の画面微分
    vec2 UvDy;
    float AlbedoLod; // 各テクスチャの実際の標本ミップ（textureQueryLOD。VT でないときは使わないので 0）
    float NormalLod;
    float MetallicLod; // ORM を metallicTexture の枠に張った材質では、ORM のミップ
    float RoughnessLod;
    float AoLod;
};

// footprint の勾配とミップで標本し、非常駐で粗いミップへ逃げたかを bEscaped へ返す（VT でなければ常に false）。
// 画面微分を使わないので、分岐・ループの後でも呼べる（VT でない材質の texture() は暗黙の勾配のまま）。
// 計算シェーダーは暗黙の勾配（texture()）が使えない（導関数が未定義で、粗いミップを引く）ので、取り込む側が
// NORVES_MATERIAL_SAMPLING_EXPLICIT_GRADIENT を定義して、VT でない標本も footprint の勾配で textureGrad する。
vec4 SampleMaterialTextureTracked(sampler2D tex, vec2 uv, MaterialTextureFootprint footprint, float sampledLod,
                                  bool bVirtualTexture, out bool bEscaped)
{
    bEscaped = false;
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResidentTracked(tex, uv, footprint.UvDx, footprint.UvDy, sampledLod, bEscaped);
    }
#endif
#ifdef NORVES_MATERIAL_SAMPLING_EXPLICIT_GRADIENT
    return textureGrad(tex, uv, footprint.UvDx, footprint.UvDy);
#else
    return texture(tex, uv);
#endif
}

// 材質の texture 群（sampler2D）を標本して材質値へ復号する。ラスタの材質シェーダー
// （gbuffer.frag・megageometry.frag・forward_transparent.frag）が使う。
// Common/PbrMaterialEvaluation.glsl と Common/SparseResidencySampling.glsl の後に取り込む。
//
// bHasORM のとき ORM は metallicSampler の枠に張られており、その1枚だけを引く
// （roughnessSampler・aoSampler は引かない）。無いときは別々の枠を引く従来の経路。
// bVirtualTexture のとき（材質のテクスチャが sparse）は、常駐していないタイルを読まず粗いミップへ逃げる。
// footprint は QueryMaterialTextureFootprint で一様な位置に取った勾配とミップ（この関数の中では画面微分を取らない）。
PbrMaterialTextureSamples SamplePbrMaterialTextures(
    sampler2D albedoSampler,
    sampler2D normalSampler,
    sampler2D metallicSampler,
    sampler2D roughnessSampler,
    sampler2D aoSampler,
    vec2 texCoord,
    MaterialTextureFootprint footprint,
    bool bHasORM,
    bool bNormalTwoChannel,
    bool bVirtualTexture)
{
    vec3 material;
    if (bHasORM)
    {
        material = DecodePbrOrmSample(SampleMaterialTextureTracked(
            metallicSampler, texCoord, footprint, footprint.MetallicLod, bVirtualTexture, g_VirtualTextureOrmEscaped));
    }
    else
    {
        bool bUnusedEscaped;
        material = vec3(SampleMaterialTextureTracked(metallicSampler, texCoord, footprint, footprint.MetallicLod,
                                                     bVirtualTexture, bUnusedEscaped).r,
                        SampleMaterialTextureTracked(roughnessSampler, texCoord, footprint, footprint.RoughnessLod,
                                                     bVirtualTexture, bUnusedEscaped).r,
                        SampleMaterialTextureTracked(aoSampler, texCoord, footprint, footprint.AoLod,
                                                     bVirtualTexture, bUnusedEscaped).r);
    }
    // アルベド・法線・ORM は、非常駐で粗いミップへ逃げたかを g_VirtualTexture*Escaped へ残す（VT のフィードバックが使う）。
    vec4 albedo = SampleMaterialTextureTracked(albedoSampler, texCoord, footprint, footprint.AlbedoLod,
                                               bVirtualTexture, g_VirtualTextureAlbedoEscaped);
    vec4 normalSample = SampleMaterialTextureTracked(normalSampler, texCoord, footprint, footprint.NormalLod,
                                                     bVirtualTexture, g_VirtualTextureNormalEscaped);
    return DecodePbrMaterialTextureSamples(albedo,
                                           normalSample,
                                           material.x,
                                           material.y,
                                           material.z,
                                           bNormalTwoChannel);
}

#endif // NORVES_PBR_MATERIAL_TEXTURE_SAMPLING_CORE_GLSL
