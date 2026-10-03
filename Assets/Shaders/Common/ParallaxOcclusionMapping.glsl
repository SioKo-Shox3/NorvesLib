// 余接フレームと視差オクルージョンマッピング（POM）。
// ラスタの材質シェーダー（gbuffer.frag・forward_transparent.frag・megageometry.frag）が共有する。
// IsCotangentFrameDegenerate を使うため、Common/PbrMaterialEvaluation.glsl の後に取り込む。
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
 *
 * @param tangentFrame CalculateCotangentFrame で元のUVから作った基底
 * @param viewDirWS    表面からカメラへ向かうワールド方向
 */
vec2 ApplyParallaxOcclusionMapping(sampler2D heightSampler,
                                   vec2 texCoord,
                                   mat3 tangentFrame,
                                   vec3 viewDirWS,
                                   float heightScale)
{
    vec2 uvDx = dFdx(texCoord);
    vec2 uvDy = dFdy(texCoord);

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
    float currentDepth = 1.0 - textureGrad(heightSampler, currentTexCoords, uvDx, uvDy).r;
    for (int layer = 0; layer <= int(kMaxLayers) && currentLayerDepth < currentDepth; ++layer)
    {
        currentTexCoords -= deltaTexCoords;
        currentDepth = 1.0 - textureGrad(heightSampler, currentTexCoords, uvDx, uvDy).r;
        currentLayerDepth += layerDepth;
    }

    // 交差の前後2サンプルの間を線形補間する。
    vec2 previousTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepth - currentLayerDepth;
    float beforeDepth = (1.0 - textureGrad(heightSampler, previousTexCoords, uvDx, uvDy).r) -
                        (currentLayerDepth - layerDepth);
    float denominator = afterDepth - beforeDepth;
    float weight = abs(denominator) > 1.0e-6 ? clamp(afterDepth / denominator, 0.0, 1.0) : 0.0;
    vec2 pomTexCoord = mix(currentTexCoords, previousTexCoords, weight);

    return mix(texCoord, pomTexCoord, pomFade);
}
