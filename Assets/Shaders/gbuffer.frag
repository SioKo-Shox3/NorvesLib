#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveColor;
layout(location = 4) in vec2 fragTexCoord;

// PBRテクスチャサンプラー
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;

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

void main()
{
    // テクスチャサンプリング × オブジェクトカラー
    vec4 texColor = texture(albedoTexture, fragTexCoord);
    outAlbedo = vec4(fragObjectColor * texColor.rgb, texColor.a);

    // ノーマルマップ適用
    // TBN行列はVulkan dFdy補正済みのため、OpenGL形式ノーマルマップをそのまま使用可能
    // デフォルト(0.5, 0.5, 1.0)テクスチャは接線空間(0, 0, 1)に変換され、
    // TBN変換後は元の表面法線と一致するため、条件分岐は不要
    vec3 normalMapSample = texture(normalTexture, fragTexCoord).rgb;
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;
    mat3 TBN = CalculateTBN(fragNormal, fragWorldPos, fragTexCoord);
    vec3 normal = normalize(TBN * tangentNormal);
    outNormal = vec4(normal, 0.0);

    // PBRマテリアルパラメータ（テクスチャからサンプリング）
    float metallic  = texture(metallicTexture, fragTexCoord).r;
    float roughness = texture(roughnessTexture, fragTexCoord).r;
    float ao        = texture(aoTexture, fragTexCoord).r;
    outMaterial = vec4(metallic, roughness, ao, 0.0);

    // Emissive: エミッシブカラー × 強度 → HDR値
    outEmissive = vec4(fragEmissiveColor.rgb * fragEmissiveColor.a, 1.0);
}
