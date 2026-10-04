// 暗黙のミップで標本する。画面微分（dFdx・dFdy）を使うので、フラグメントシェーダー専用で、
// 動的に一様な制御フローで呼ぶ。bVirtualTexture のときは勾配を明示した常駐フォールバック付きの標本になる。
vec4 SampleMaterialTexture(sampler2D tex, vec2 uv, bool bVirtualTexture)
{
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResidentGrad(tex, uv, dFdx(uv), dFdy(uv));
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
        material = DecodePbrOrmSample(SampleMaterialTexture(metallicSampler, texCoord, bVirtualTexture));
    }
    else
    {
        material = vec3(SampleMaterialTexture(metallicSampler, texCoord, bVirtualTexture).r,
                        SampleMaterialTexture(roughnessSampler, texCoord, bVirtualTexture).r,
                        SampleMaterialTexture(aoSampler, texCoord, bVirtualTexture).r);
    }
    return DecodePbrMaterialTextureSamples(SampleMaterialTexture(albedoSampler, texCoord, bVirtualTexture),
                                           SampleMaterialTexture(normalSampler, texCoord, bVirtualTexture),
                                           material.x,
                                           material.y,
                                           material.z,
                                           bNormalTwoChannel);
}
