// 暗黙のミップで標本する。画面微分（dFdx・dFdy）を使うので、フラグメントシェーダー専用で、
// 動的に一様な制御フローで呼ぶ。bVirtualTexture のときは sparseTextureARB で標本し、常駐していなければ粗いミップへ逃げる。
vec4 SampleMaterialTexture(sampler2D tex, vec2 uv, bool bVirtualTexture)
{
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResident(tex, uv);
    }
#endif
    return texture(tex, uv);
}

// 暗黙のミップで標本し、非常駐で粗いミップへ逃げたかを bEscaped へ返す（VT でなければ常に false）。
// 画面微分を使うので、フラグメントシェーダー専用で、動的に一様な制御フローで呼ぶ。
vec4 SampleMaterialTextureTracked(sampler2D tex, vec2 uv, bool bVirtualTexture, out bool bEscaped)
{
    bEscaped = false;
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResidentTracked(tex, uv, bEscaped);
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
// 画面微分を使うので、動的に一様な制御フローで呼ぶ。
PbrMaterialTextureSamples SamplePbrMaterialTextures(
    sampler2D albedoSampler,
    sampler2D normalSampler,
    sampler2D metallicSampler,
    sampler2D roughnessSampler,
    sampler2D aoSampler,
    vec2 texCoord,
    bool bHasORM,
    bool bNormalTwoChannel,
    bool bVirtualTexture)
{
    vec3 material;
    if (bHasORM)
    {
        material = DecodePbrOrmSample(
            SampleMaterialTextureTracked(metallicSampler, texCoord, bVirtualTexture, g_VirtualTextureOrmEscaped));
    }
    else
    {
        material = vec3(SampleMaterialTexture(metallicSampler, texCoord, bVirtualTexture).r,
                        SampleMaterialTexture(roughnessSampler, texCoord, bVirtualTexture).r,
                        SampleMaterialTexture(aoSampler, texCoord, bVirtualTexture).r);
    }
    // アルベド・法線・ORM は、非常駐で粗いミップへ逃げたかを g_VirtualTexture*Escaped へ残す（VT のフィードバックが使う）。
    vec4 albedo = SampleMaterialTextureTracked(albedoSampler, texCoord, bVirtualTexture, g_VirtualTextureAlbedoEscaped);
    vec4 normalSample = SampleMaterialTextureTracked(normalSampler, texCoord, bVirtualTexture, g_VirtualTextureNormalEscaped);
    return DecodePbrMaterialTextureSamples(albedo,
                                           normalSample,
                                           material.x,
                                           material.y,
                                           material.z,
                                           bNormalTwoChannel);
}
