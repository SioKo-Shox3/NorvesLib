// 余接フレームと視差オクルージョンマッピング（POM）。
// ラスタの材質シェーダー（gbuffer.frag・forward_transparent.frag・megageometry.frag）が共有する。
// IsCotangentFrameDegenerate を使うため、Common/PbrMaterialEvaluation.glsl の後に取り込む。
// 高さのサンプルは Common/SparseResidencySampling.glsl の関数を使うので、それも先に取り込む。
// 画面微分を使うので、どちらの関数も動的に一様な制御フローで呼ぶ。

/**
 * @brief 画面微分から余接フレーム（T=+∇u, B=+∇v, N）を作る。
 *
 * Christian Schüler "Normal Mapping Without Precomputed Tangents" に基づく。
 * Vulkan の dFdy は OpenGL と符号が逆なので、位置とUVの両方の dFdy を反転して揃える。
 * T・B は長さの比を保ったまま長い方が 1 になるよう共通の倍率で正規化し、直交化はしない。
 */
mat3 CalculateCotangentFrame(vec3 worldNormal, vec3 worldPos, vec2 texCoord)
{
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = -dFdy(worldPos);
    vec2 duv1 = dFdx(texCoord);
    vec2 duv2 = -dFdy(texCoord);

    vec3 N = normalize(worldNormal);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // 退化判定: 画素あたりの微分は解像度で小さくなるため、絶対値ではなく
    // 位置微分とUV微分の大きさに対する比で判定する。
    float maxLen2 = max(dot(T, T), dot(B, B));
    float positionScale = max(dot(dp1, dp1), dot(dp2, dp2));
    float uvScale = max(dot(duv1, duv1), dot(duv2, duv2));
    if (IsCotangentFrameDegenerate(maxLen2, positionScale, uvScale))
    {
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
        B = cross(N, T);
        return mat3(T, B, N);
    }

    float invmax = inversesqrt(maxLen2);
    return mat3(T * invmax, B * invmax, N);
}

#include "Common/ParallaxOcclusionMappingCore.glsl"

/**
 * @brief 画面微分から勾配を取って ApplyParallaxOcclusionMappingGrad を呼ぶ（フラグメントシェーダー用）。
 *
 * 勾配と標本ミップは、早期 return やループより前の一様な位置で取る（textureQueryLOD も画面微分を使う。bVirtualTexture は一様）。
 */
vec2 ApplyParallaxOcclusionMapping(sampler2D heightSampler,
                                   vec2 texCoord,
                                   mat3 tangentFrame,
                                   vec3 viewDirWS,
                                   float heightScale,
                                   bool bVirtualTexture)
{
    vec2 uvDx = dFdx(texCoord);
    vec2 uvDy = dFdy(texCoord);
    float sampledLod = bVirtualTexture ? textureQueryLOD(heightSampler, texCoord).y : 0.0;
    return ApplyParallaxOcclusionMappingGrad(heightSampler, texCoord, tangentFrame, viewDirWS, heightScale,
                                             bVirtualTexture, uvDx, uvDy, sampledLod);
}
