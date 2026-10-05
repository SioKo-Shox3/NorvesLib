// 余接フレームの軸の正規化と、勾配を引数に取る視差オクルージョンマッピング（POM）。画面微分を使わない。
// フラグメントシェーダー（Common/ParallaxOcclusionMapping.glsl 経由）と計算シェーダー（材質の解決）が共有する。
// 高さのサンプルは Common/SparseResidencySampling.glsl の関数を使うので、先に取り込む。

#ifndef NORVES_PARALLAX_OCCLUSION_MAPPING_CORE_GLSL
#define NORVES_PARALLAX_OCCLUSION_MAPPING_CORE_GLSL

vec3 NormalizeTangentAxis(vec3 axis)
{
    return axis * inversesqrt(max(dot(axis, axis), 1.0e-20));
}

/**
 * @brief 視差オクルージョンマッピングで見かけのUVを求める。
 *
 * 高さマップは白=高いとして読み、深さ = 1 - 高さ でレイマーチする。
 * 輪郭のフェードと層数は幾何法線とビュー方向の内積（N·V）で決める。余接フレームは
 * 正規直交でないため、転置での変換は z が N·V より大きくなり輪郭のフェードが効かなくなる。
 * 接空間のビュー方向は正規化した T・B への射影と N·V から作る。
 * マーチ中のサンプルは、分岐の前に取った元のUVの勾配で textureGrad を使う。
 * bVirtualTexture のとき（高さが sparse）は、常駐していないタイルを読まず粗いミップへ逃げる。
 * 逃げ始めのミップは、分岐・ループの前に元のUVの textureQueryLOD で取った実際の標本ミップ（異方性の上限を含む）にする。
 *
 * @param tangentFrame CalculateCotangentFrame で元のUVから作った基底
 * @param viewDirWS    表面からカメラへ向かうワールド方向
 * @param bVirtualTexture 高さのテクスチャが sparse か
 * @param uvDx・uvDy   texCoord の画素あたりの勾配。フラグメントは画面微分、計算シェーダーは三角形から求めた解析的な微分
 * @param sampledLod   その勾配で実際に標本される高さのミップ（bVirtualTexture のときだけ使う）
 */
vec2 ApplyParallaxOcclusionMappingGrad(sampler2D heightSampler,
                                       vec2 texCoord,
                                       mat3 tangentFrame,
                                       vec3 viewDirWS,
                                       float heightScale,
                                       bool bVirtualTexture,
                                       vec2 uvDx,
                                       vec2 uvDy,
                                       float sampledLod)
{
    vec3 V = normalize(viewDirWS);
    float nDotV = clamp(dot(tangentFrame[2], V), 0.0, 1.0);

    // 輪郭（N·V→0）では素のUVへ寄せる。背面寄りは 0 に落ちて POM を行わない。
    float pomFade = smoothstep(0.1, 0.3, nDotV);
    if (pomFade <= 0.0)
    {
        return texCoord;
    }

    vec3 viewDirTS = vec3(dot(V, NormalizeTangentAxis(tangentFrame[0])),
                          dot(V, NormalizeTangentAxis(tangentFrame[1])),
                          nDotV);

    // 視線が浅いほど層を増やす。
    const float kMinLayers = 8.0;
    const float kMaxLayers = 32.0;
    float layerCount = mix(kMaxLayers, kMinLayers, nDotV);
    float layerDepth = 1.0 / layerCount;

    // offset limiting: viewDirTS.z で割らず、オフセットを heightScale 程度に抑える。
    vec2 deltaTexCoords = viewDirTS.xy * heightScale / layerCount;

    vec2 currentTexCoords = texCoord;
    float currentLayerDepth = 0.0;
    float currentDepth = 1.0 - SampleMaterialTextureGrad(heightSampler, currentTexCoords, uvDx, uvDy, sampledLod, bVirtualTexture).r;
    for (int layer = 0; layer <= int(kMaxLayers) && currentLayerDepth < currentDepth; ++layer)
    {
        currentTexCoords -= deltaTexCoords;
        currentDepth = 1.0 - SampleMaterialTextureGrad(heightSampler, currentTexCoords, uvDx, uvDy, sampledLod, bVirtualTexture).r;
        currentLayerDepth += layerDepth;
    }

    // 交差の前後2サンプルの間を線形補間する。
    vec2 previousTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepth - currentLayerDepth;
    float beforeDepth = (1.0 - SampleMaterialTextureGrad(heightSampler, previousTexCoords, uvDx, uvDy, sampledLod, bVirtualTexture).r) -
                        (currentLayerDepth - layerDepth);
    float denominator = afterDepth - beforeDepth;
    float weight = abs(denominator) > 1.0e-6 ? clamp(afterDepth / denominator, 0.0, 1.0) : 0.0;
    vec2 pomTexCoord = mix(currentTexCoords, previousTexCoords, weight);

    return mix(texCoord, pomTexCoord, pomFade);
}

#endif // NORVES_PARALLAX_OCCLUSION_MAPPING_CORE_GLSL
