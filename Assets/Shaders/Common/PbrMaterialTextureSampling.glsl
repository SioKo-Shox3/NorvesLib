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

// 勾配を引数に取る標本（MaterialTextureFootprint・SampleMaterialTextureTracked・SamplePbrMaterialTextures）は、
// 画面微分を使わないので Core に置き、計算シェーダー（材質の解決）と共有する。
#include "Common/PbrMaterialTextureSamplingCore.glsl"

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
