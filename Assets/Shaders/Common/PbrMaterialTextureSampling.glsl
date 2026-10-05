// 暗黙のミップで標本する。画面微分（dFdx・dFdy）を使うので、フラグメントシェーダー専用で、
// 動的に一様な制御フローで呼ぶ。bVirtualTexture のときは sparseTextureARB で標本し、常駐していなければ粗いミップへ逃げる。
// 分岐・ループの後になりうる材質のシェーダーは使わず、SamplePbrMaterialTextures（MaterialTextureFootprint を取る）を使う。
vec4 SampleMaterialTexture(sampler2D tex, vec2 uv, bool bVirtualTexture)
{
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        // 画面微分を関数の先頭で取る（コンピュートでは画面微分が使えないので、この関数は共有の SparseResidencySampling.glsl に置かない）
        bool bEscaped;
        return SampleSparseResidentTracked(tex, uv, dFdx(uv), dFdy(uv), textureQueryLOD(tex, uv).y, bEscaped);
    }
#endif
    return texture(tex, uv);
}

// 材質の標本が画面微分から求める量。分岐・早期 return・ループの後では画面微分が未定義なので、
// QueryMaterialTextureFootprint で main の一様な位置（POM の直後）に一度だけ取り、標本・フィードバック・法線の粗い傾きへ引数で渡す。
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

// 画面微分と各層の標本ミップを取る。texCoord は POM の後の uv で、POM の直後の一様な位置で呼ぶ。
// bQueryLods（描画ごとに一様）が false のときは勾配だけ取り、ミップは 0 にする（VT でない材質は標本ミップを使わない）。
// bHasORM のとき roughness・ao の枠は引かないのでミップを問い合わせない。
MaterialTextureFootprint QueryMaterialTextureFootprint(
    sampler2D albedoSampler,
    sampler2D normalSampler,
    sampler2D metallicSampler,
    sampler2D roughnessSampler,
    sampler2D aoSampler,
    vec2 texCoord,
    bool bHasORM,
    bool bQueryLods)
{
    MaterialTextureFootprint footprint;
    footprint.UvDx = dFdx(texCoord);
    footprint.UvDy = dFdy(texCoord);
    footprint.AlbedoLod = 0.0;
    footprint.NormalLod = 0.0;
    footprint.MetallicLod = 0.0;
    footprint.RoughnessLod = 0.0;
    footprint.AoLod = 0.0;
    if (bQueryLods)
    {
        footprint.AlbedoLod = textureQueryLOD(albedoSampler, texCoord).y;
        footprint.NormalLod = textureQueryLOD(normalSampler, texCoord).y;
        footprint.MetallicLod = textureQueryLOD(metallicSampler, texCoord).y;
        if (!bHasORM)
        {
            footprint.RoughnessLod = textureQueryLOD(roughnessSampler, texCoord).y;
            footprint.AoLod = textureQueryLOD(aoSampler, texCoord).y;
        }
    }
    return footprint;
}

// footprint の勾配とミップで標本し、非常駐で粗いミップへ逃げたかを bEscaped へ返す（VT でなければ常に false）。
// 画面微分を使わないので、分岐・ループの後でも呼べる（VT でない材質の texture() は暗黙の勾配のまま）。
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
    return texture(tex, uv);
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
