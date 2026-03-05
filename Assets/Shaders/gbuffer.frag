#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveColor;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in vec3 fragViewDir;

// UBOからPOMパラメータを参照
layout(set = 0, binding = 0) uniform MVPData
{
    mat4 world;
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 objectColor;
    vec4 emissiveColor;
    vec4 pomParams;  // x=heightScale, y=hasHeightMap, z=unused, w=unused
} mvp;

// PBRテクスチャサンプラー
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;

// GBuffer MRT出力
layout(location = 0) out vec4 outAlbedo;    // RT0: Albedo (RGB) + alpha
layout(location = 1) out vec4 outNormal;    // RT1: World Normal (RGB) + unused
layout(location = 2) out vec4 outMaterial;  // RT2: Metallic(R) / Roughness(G) / AO(B) / unused(A)
layout(location = 3) out vec4 outEmissive;  // RT3: Emissive (RGB, HDR) + unused

/**
 * @brief スクリーンスペース微分からTBN行列を計算（Cotangent Frame法）
 *
 * Christian Schüler "Normal Mapping Without Precomputed Tangents" に基づく。
 * dFdx/dFdyを使用して接線空間を導出するため、
 * 頂点データにTangent属性が不要です。
 *
 * Vulkan補正: dFdyはOpenGLと符号が逆（Vulkanのスクリーン座標Y軸は下向き）のため、
 * dFdyの結果を反転してOpenGL規約に揃えてからTBN行列を構築します。
 */
mat3 CalculateTBN(vec3 worldNormal, vec3 worldPos, vec2 texCoord)
{
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = -dFdy(worldPos);   // Vulkan Y-flip補正（OpenGL規約に合わせる）
    vec2 duv1 = dFdx(texCoord);
    vec2 duv2 = -dFdy(texCoord);  // Vulkan Y-flip補正

    vec3 N = normalize(worldNormal);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // 退化チェック: UV微分が零の場合（UVシームや極付近）
    float maxLen2 = max(dot(T, T), dot(B, B));
    if (maxLen2 < 1e-8)
    {
        // フォールバック: 任意の接線フレームを構築
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
        B = cross(N, T);
        return mat3(T, B, N);
    }

    float invmax = inversesqrt(maxLen2);
    return mat3(T * invmax, B * invmax, N);
}

/**
 * @brief Parallax Occlusion Mapping (POM)
 *
 * ハイトマップに基づいてUV座標をオフセットし、
 * 表面に凹凸があるかのような錯覚を生み出す。
 * 急勾配のレイマーチでおおまかな交差を求め、
 * 2サンプル間の線形補間で精度を上げる。
 */
vec2 ParallaxOcclusionMapping(vec2 texCoord, vec3 viewDirTS, float heightScale)
{
    // レイヤー数: 視線が浅い（grazing angle）ほど多く
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, abs(viewDirTS.z));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    // 接線空間のビュー方向からUVオフセット方向を算出
    vec2 P = viewDirTS.xy / viewDirTS.z * heightScale;
    vec2 deltaTexCoords = P / numLayers;

    vec2 currentTexCoords = texCoord;
    float currentDepthMapValue = texture(heightTexture, currentTexCoords).r;

    // 急勾配レイマーチ: レイヤーがハイトマップより深くなるまで進む
    while (currentLayerDepth < currentDepthMapValue)
    {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = texture(heightTexture, currentTexCoords).r;
        currentLayerDepth += layerDepth;
    }

    // 前後2サンプル間で線形補間（オクルージョン補間）
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = texture(heightTexture, prevTexCoords).r - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}

void main()
{
    // POMパラメータ取得
    float heightScale = mvp.pomParams.x;
    float hasHeightMap = mvp.pomParams.y;

    // POM適用: ハイトマップがある場合のみUVオフセット
    vec2 texCoord = fragTexCoord;
    if (hasHeightMap > 0.5)
    {
        // TBN行列を構築して、ビュー方向を接線空間に変換
        mat3 TBN = CalculateTBN(fragNormal, fragWorldPos, fragTexCoord);
        mat3 TBN_inv = transpose(TBN);  // 正規直交基底なので転置=逆行列
        vec3 viewDirTS = normalize(TBN_inv * fragViewDir);
        texCoord = ParallaxOcclusionMapping(fragTexCoord, viewDirTS, heightScale);

        // UV範囲外チェック（タイリングテクスチャなら不要だが念のため）
        // if (texCoord.x > 1.0 || texCoord.y > 1.0 || texCoord.x < 0.0 || texCoord.y < 0.0)
        //     discard;
    }

    // テクスチャサンプリング × オブジェクトカラー（POM補正済みUV使用）
    vec4 texColor = texture(albedoTexture, texCoord);
    outAlbedo = vec4(fragObjectColor * texColor.rgb, texColor.a);

    // ノーマルマップ適用（POM補正済みUV使用）
    vec3 normalMapSample = texture(normalTexture, texCoord).rgb;
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;
    mat3 TBN_normal = CalculateTBN(fragNormal, fragWorldPos, texCoord);
    vec3 normal = normalize(TBN_normal * tangentNormal);
    outNormal = vec4(normal, 0.0);

    // PBRマテリアルパラメータ（POM補正済みUV使用）
    float metallic  = texture(metallicTexture, texCoord).r;
    float roughness = texture(roughnessTexture, texCoord).r;
    float ao        = texture(aoTexture, texCoord).r;
    outMaterial = vec4(metallic, roughness, ao, 0.0);

    // Emissive: エミッシブカラー × 強度 → HDR値
    outEmissive = vec4(fragEmissiveColor.rgb * fragEmissiveColor.a, 1.0);
}
