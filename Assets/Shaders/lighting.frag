#version 450

layout(location = 0) in vec2 fragUV;

// GBufferテクスチャ
layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 3) uniform sampler2D gbufferDepth;

// ライティングパラメータ
layout(std140, set = 0, binding = 4) uniform LightingParams
{
    mat4 invViewProjection;
    vec4 cameraPosition;    // xyz=position, w=unused
    vec4 ambientColor;      // xyz=color, w=intensity
    mat4 lightView;         // シャドウマップ用ライトビュー行列
    mat4 lightProjection;   // シャドウマップ用ライトプロジェクション行列
    uint lightCount;
    uint bShadowEnabled;    // シャドウマップ有効フラグ
    uint envMapMipLevels;   // 環境マップミップレベル数
    uint bIBLEnabled;       // IBL有効フラグ
    uint bSSAOEnabled;      // SSAO有効フラグ
    uint bNeuralBRDFEnabled; // Neural BRDF有効フラグ
    uint debugViewMode;
    uint _pad2;
} params;

// ライトデータ構造
struct LightData
{
    vec4 position;      // xyz=position, w=type (0:Dir, 1:Point, 2:Spot)
    vec4 direction;     // xyz=direction, w=innerAngle
    vec4 color;         // xyz=color, w=intensity
    vec4 attenuation;   // x=range, y=outerAngle, z=unused, w=unused
};

// ライト配列（SSBO的にUBO内に配置、最大16ライト）
layout(set = 0, binding = 5) uniform LightBuffer
{
    LightData lights[16];
} lightBuffer;

// シャドウマップ
layout(set = 0, binding = 6) uniform sampler2D shadowMap;

// GBufferエミッシブ
layout(set = 0, binding = 7) uniform sampler2D gbufferEmissive;

// IBL (Image-Based Lighting)
layout(set = 0, binding = 8) uniform sampler2D envMap;    // HDR環境マップ（equirectangular）
layout(set = 0, binding = 9) uniform sampler2D brdfLUT;   // BRDF LUT（split-sum近似）

// SSAO (Screen-Space Ambient Occlusion)
layout(set = 0, binding = 10) uniform sampler2D ssaoTexture;

// Neural BRDF重みデータ（Disney BRDF MLP）
layout(set = 0, binding = 11) readonly buffer NeuralBRDFWeights
{
    float data[];
} neuralBRDF;

layout(location = 0) out vec4 outColor;

// ========================================
// PBR関連関数
// ========================================

const float PI = 3.14159265359;
const uint DEBUG_VIEW_MODE_NORMAL = 0u;
const uint DEBUG_VIEW_MODE_UNLIT = 1u;
const uint DEBUG_VIEW_MODE_WIREFRAME = 2u;
const uint DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS = 3u;
const uint DEBUG_VIEW_MODE_GBUFFER_ALBEDO = 4u;
const uint DEBUG_VIEW_MODE_GBUFFER_NORMAL = 5u;
const uint DEBUG_VIEW_MODE_GBUFFER_MATERIAL = 6u;
const uint DEBUG_VIEW_MODE_GBUFFER_DEPTH = 7u;
const uint DEBUG_VIEW_MODE_LOD_LEVEL = 8u;
const uint DEBUG_VIEW_MODE_COUNT = 9u;

// ========================================
// Equirectangular UV from direction vector
// ========================================
vec2 EquirectangularUV(vec3 dir)
{
    // atan(z, x) → [-PI, PI] → [0, 1]
    // asin(y) → [-PI/2, PI/2] → [0, 1]
    // Vulkan座標系ではY軸が反転しているため -dir.y を使用
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(-dir.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989); // 1/(2*PI), 1/PI
    uv += 0.5;
    return uv;
}

// フレネル（Schlickの近似）
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// フレネル（Schlickの近似、ラフネス考慮版 - アンビエント/IBL用）
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 法線分布関数（GGX/Trowbridge-Reitz）
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0001);
}

// 幾何遮蔽関数（Smith's method with Schlick-GGX）
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    return ggx1 * ggx2;
}

// ========================================
// スペキュラオクルージョン (Lagarde 2014)
// AO値からスペキュラ方向のオクルージョンを近似計算
// ========================================
float ComputeSpecularAO(float NdotV, float ao, float roughness)
{
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// ========================================
// 深度からワールド座標を復元
// ========================================
vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    // UV→NDC座標変換 (VulkanのY軸反転を考慮)
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = params.invViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

float ComputeDebugDepth01(vec2 uv, float depth)
{
    vec3 worldPos = ReconstructWorldPosition(uv, depth);
    float cameraDistance = distance(params.cameraPosition.xyz, worldPos);
    float depth01 = cameraDistance / (cameraDistance + 25.0);
    return clamp(depth01, 0.0, 1.0);
}

// ========================================
// ライト減衰計算
// ========================================
float CalculateAttenuation(float distance, float range)
{
    // 距離ベースの減衰（スムーズな減衰カーブ）
    float attenuation = 1.0 / (distance * distance + 1.0);
    float factor = clamp(1.0 - pow(distance / max(range, 0.001), 4.0), 0.0, 1.0);
    return attenuation * factor * factor;
}

// ========================================
// PCSS (Percentage-Closer Soft Shadows)
// ブロッカーサーチ + 可変カーネルPCF
// ========================================

// Poisson disk サンプル（16点）
const vec2 POISSON_DISK[16] = vec2[16](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

// ライトサイズ（ソフトネス制御）
const float PCSS_LIGHT_SIZE = 0.04;
const float PCSS_BLOCKER_SEARCH_RADIUS = 0.02;

// Phase 1: ブロッカーサーチ（平均ブロッカー深度を求める）
float FindBlockerDepth(vec2 shadowUV, float receiverDepth, vec2 texelSize)
{
    float blockerSum = 0.0;
    int blockerCount = 0;
    float searchRadius = PCSS_BLOCKER_SEARCH_RADIUS;

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = POISSON_DISK[i] * searchRadius;
        float sampleDepth = texture(shadowMap, shadowUV + offset).r;
        if (sampleDepth < receiverDepth - 0.005)
        {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
    {
        return -1.0; // ブロッカーなし
    }

    return blockerSum / float(blockerCount);
}

// Phase 2: ペナンブラサイズ推定
float EstimatePenumbraSize(float receiverDepth, float blockerDepth)
{
    return PCSS_LIGHT_SIZE * (receiverDepth - blockerDepth) / blockerDepth;
}

// Phase 3: 可変カーネルPCF
float PCSSFilter(vec2 shadowUV, float receiverDepth, float filterRadius)
{
    float shadow = 0.0;
    float bias = 0.005;

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = POISSON_DISK[i] * filterRadius;
        float sampleDepth = texture(shadowMap, shadowUV + offset).r;
        shadow += (receiverDepth - bias > sampleDepth) ? 0.0 : 1.0;
    }

    return shadow / 16.0;
}

float CalculateShadow(vec3 worldPos)
{
    // ワールド座標をライトクリップ空間に変換
    vec4 lightSpacePos = params.lightProjection * params.lightView * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // クリップ空間[-1,1] → UV座標[0,1]に変換
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    // シャドウマップ範囲外は影なし
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
    {
        return 1.0;
    }

    // 深度範囲外も影なし
    if (currentDepth < 0.0 || currentDepth > 1.0)
    {
        return 1.0;
    }

    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    // Phase 1: ブロッカーサーチ
    float avgBlockerDepth = FindBlockerDepth(shadowUV, currentDepth, texelSize);

    // ブロッカーなし → 完全にライトが当たっている
    if (avgBlockerDepth < 0.0)
    {
        return 1.0;
    }

    // Phase 2: ペナンブラサイズ推定
    float penumbraSize = EstimatePenumbraSize(currentDepth, avgBlockerDepth);

    // フィルタ半径をクランプ（最小=1texel, 最大=制限）
    float filterRadius = clamp(penumbraSize, texelSize.x, 0.05);

    // Phase 3: 可変カーネルPCF
    return PCSSFilter(shadowUV, currentDepth, filterRadius);
}

// ========================================
// Neural Disney BRDF評価
// 事前学習済みMLP（30→32→32→32→4）による推論
// 入力: NdotL, NdotV, NdotH, LdotH, roughness
// 出力: 4コンポーネント (x=diffuse_scale, y=specular_GD, z=fresnel, w=clearcoat)
// ========================================

// 重みオフセット定数（FP32 float配列インデックス）
// Layer 0 (30→32): weights[0..959], biases[960..991]
// Layer 1 (32→32): weights[992..2015], biases[2016..2047]
// Layer 2 (32→32): weights[2048..3071], biases[3072..3103]
// Layer 3 (32→4):  weights[3104..3231], biases[3232..3235]

vec4 EvaluateNeuralBRDF(float NdotL, float NdotV, float NdotH, float LdotH, float roughness)
{
    // 周波数エンコーディング: 5入力 × 3周波数 × 2(sin/cos) = 30ニューロン
    float encoded[30];
    float features[5] = float[5](NdotL, NdotV, NdotH, LdotH, roughness);

    for (int i = 0; i < 5; i++)
    {
        for (int k = 0; k < 3; k++)
        {
            float freq = exp2(float(k)) * PI * features[i];
            encoded[i * 6 + k * 2]     = sin(freq);
            encoded[i * 6 + k * 2 + 1] = cos(freq);
        }
    }

    // Layer 0: 30→32, ReLU
    float h0[32];
    for (int o = 0; o < 32; o++)
    {
        float sum = neuralBRDF.data[960 + o];
        for (int i = 0; i < 30; i++)
        {
            sum += encoded[i] * neuralBRDF.data[o * 30 + i];
        }
        h0[o] = max(sum, 0.0);
    }

    // Layer 1: 32→32, ReLU
    float h1[32];
    for (int o = 0; o < 32; o++)
    {
        float sum = neuralBRDF.data[2016 + o];
        for (int i = 0; i < 32; i++)
        {
            sum += h0[i] * neuralBRDF.data[992 + o * 32 + i];
        }
        h1[o] = max(sum, 0.0);
    }

    // Layer 2: 32→32, ReLU
    float h2[32];
    for (int o = 0; o < 32; o++)
    {
        float sum = neuralBRDF.data[3072 + o];
        for (int i = 0; i < 32; i++)
        {
            sum += h1[i] * neuralBRDF.data[2048 + o * 32 + i];
        }
        h2[o] = max(sum, 0.0);
    }

    // Layer 3: 32→4, exp活性化
    vec4 result;
    for (int o = 0; o < 4; o++)
    {
        float sum = neuralBRDF.data[3232 + o];
        for (int i = 0; i < 32; i++)
        {
            sum += h2[i] * neuralBRDF.data[3104 + o * 32 + i];
        }
        result[o] = exp(sum);
    }

    return result;
}

void main()
{
    // GBufferからデータを取得
    vec4 albedoSample = texture(gbufferAlbedo, fragUV);
    vec4 normalSample = texture(gbufferNormal, fragUV);
    vec4 materialSample = texture(gbufferMaterial, fragUV);
    float depthSample = texture(gbufferDepth, fragUV).r;

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_ALBEDO)
    {
        outColor = vec4(albedoSample.rgb, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_NORMAL)
    {
        vec3 debugNormal = normalize(normalSample.xyz) * 0.5 + 0.5;
        outColor = vec4(debugNormal, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_MATERIAL)
    {
        outColor = vec4(materialSample.rgb, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_DEPTH)
    {
        float depth01 = ComputeDebugDepth01(fragUV, depthSample);
        outColor = vec4(vec3(depth01), 1.0);
        return;
    }

    // アルファが0の場合は天球（環境マップ）を描画
    if (albedoSample.a < 0.01)
    {
        if (params.bIBLEnabled != 0u)
        {
            // スクリーンUVからワールド方向を復元（far planeのdepth=1.0を使用）
            vec4 clipPos = vec4(fragUV * 2.0 - 1.0, 1.0, 1.0);
            vec4 worldPos4 = params.invViewProjection * clipPos;
            vec3 worldPos = worldPos4.xyz / worldPos4.w;
            vec3 rayDir = normalize(worldPos - params.cameraPosition.xyz);

            // equirectangular環境マップをサンプリング（LOD 0 = 最高解像度）
            vec2 envUV = EquirectangularUV(rayDir);
            vec3 skyColor = textureLod(envMap, envUV, 0.0).rgb;

            outColor = vec4(skyColor, 1.0);
        }
        else
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||
        params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||
        params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS ||
        params.debugViewMode == DEBUG_VIEW_MODE_LOD_LEVEL)
    {
        outColor = vec4(albedoSample.rgb, 1.0);
        return;
    }

    // データ展開
    vec3 albedo = albedoSample.rgb;
    vec3 N = normalize(normalSample.xyz);
    float metallic = materialSample.r;
    float roughness = materialSample.g;
    float ao = materialSample.b;

    // SSAO適用: マテリアルAOとSSAOを掛け合わせる
    if (params.bSSAOEnabled != 0u)
    {
        float ssao = texture(ssaoTexture, fragUV).r;
        ao *= ssao;
    }

    // ワールド座標を復元
    vec3 worldPos = ReconstructWorldPosition(fragUV, depthSample);

    // カメラからの視線ベクトル
    vec3 V = normalize(params.cameraPosition.xyz - worldPos);

    // 基本反射率（非金属: 0.04、金属: アルベドカラー）
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // ライティング計算（ディフューズとスペキュラを分離してAOを個別適用）
    vec3 Lo_diffuse = vec3(0.0);
    vec3 Lo_specular = vec3(0.0);

    for (uint i = 0u; i < min(params.lightCount, 16u); i++)
    {
        LightData light = lightBuffer.lights[i];
        float lightType = light.position.w;
        vec3 lightColor = light.color.rgb * light.color.w; // color * intensity

        vec3 L;
        float attenuation = 1.0;

        if (lightType < 0.5)
        {
            // Directional Light
            L = normalize(-light.direction.xyz);
        }
        else if (lightType < 1.5)
        {
            // Point Light
            vec3 toLight = light.position.xyz - worldPos;
            float distance = length(toLight);
            L = normalize(toLight);
            attenuation = CalculateAttenuation(distance, light.attenuation.x);
        }
        else
        {
            // Spot Light
            vec3 toLight = light.position.xyz - worldPos;
            float distance = length(toLight);
            L = normalize(toLight);
            attenuation = CalculateAttenuation(distance, light.attenuation.x);

            // スポットライトのコーン減衰
            float theta = dot(L, normalize(-light.direction.xyz));
            float innerAngle = light.direction.w;
            float outerAngle = light.attenuation.y;
            float epsilon = innerAngle - outerAngle;
            float spotFactor = clamp((theta - outerAngle) / max(epsilon, 0.001), 0.0, 1.0);
            attenuation *= spotFactor;
        }

        // PBR BRDF計算
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        // シャドウ計算（ディレクショナルライトのみ）
        float shadow = 1.0;
        if (lightType < 0.5 && params.bShadowEnabled != 0u)
        {
            shadow = CalculateShadow(worldPos);
        }

        vec3 radiance = lightColor * NdotL * attenuation * shadow;

        if (params.bNeuralBRDFEnabled != 0u)
        {
            // Neural Disney BRDFによる評価
            // 出力: x=diffuse_scale, y=specular_GD, z=fresnel, w=clearcoat
            float NdotV_val = max(dot(N, V), 0.0);
            float NdotH_val = max(dot(N, H), 0.0);
            float LdotH_val = max(dot(L, H), 0.0);

            vec4 nn = EvaluateNeuralBRDF(NdotL, NdotV_val, NdotH_val, LdotH_val, roughness);

            // Disney BRDF再構成（RTXNS SimpleInferencing準拠）
            vec3 Cspec0 = mix(vec3(0.04), albedo, metallic);
            vec3 diffuseContrib = nn.x * albedo * (1.0 - metallic);
            vec3 specularContrib = nn.y * mix(Cspec0, vec3(1.0), nn.z) + vec3(nn.w);

            Lo_diffuse += diffuseContrib * radiance;
            Lo_specular += specularContrib * radiance;
        }
        else
        {
            // Analytical Cook-Torrance BRDF
            float D = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 numerator = D * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            vec3 specular = numerator / denominator;

            // エネルギー保存
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic; // 金属はディフューズなし

            Lo_diffuse += (kD * albedo / PI) * radiance;
            Lo_specular += specular * radiance;
        }
    }

    // エミッシブ（自発光）をGBufferから取得
    vec3 emissive = texture(gbufferEmissive, fragUV).rgb;

    // ========================================
    // アンビエント / IBL計算
    // ========================================
    float NdotV = max(dot(N, V), 0.0);
    vec3 F_ambient = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS_ambient = F_ambient;
    vec3 kD_ambient = (1.0 - kS_ambient) * (1.0 - metallic);

    // スペキュラAO（Lagarde 2014: 視線角度とラフネスに基づく遮蔽近似）
    float specularAO = ComputeSpecularAO(NdotV, ao, roughness);

    vec3 ambient;

    if (params.bIBLEnabled != 0u)
    {
        // ========================================
        // IBL (Image-Based Lighting)
        // ========================================
        float maxLod = float(params.envMapMipLevels - 1u);
        float iblIntensity = params.ambientColor.w; // IBL有効時はambientColor.wがIBL強度

        // ---- Diffuse IBL (Irradiance) ----
        // 法線方向で環境マップの高LOD（ブラー済み）をサンプリング → 拡散放射照度近似
        vec2 irradianceUV = EquirectangularUV(N);
        vec3 irradiance = textureLod(envMap, irradianceUV, maxLod * 0.7).rgb;
        // HDR値を露出補正してからトーンマップ（高輝度を適切に圧縮）
        float iblExposure = 0.15;
        irradiance = vec3(1.0) - exp(-irradiance * iblExposure);
        vec3 diffuseIBL = kD_ambient * irradiance * albedo;

        // ---- Specular IBL (Pre-filtered Environment) ----
        // 反射ベクトル方向でラフネスに応じたLODをサンプリング
        vec3 R = reflect(-V, N);
        vec2 specularUV = EquirectangularUV(R);
        float lod = roughness * maxLod;
        vec3 prefilteredColor = textureLod(envMap, specularUV, lod).rgb;
        // HDR値を露出補正してからトーンマップ
        prefilteredColor = vec3(1.0) - exp(-prefilteredColor * iblExposure);

        // BRDF LUT参照（split-sum近似の第2項）
        vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

        // スペキュラIBL = プリフィルタ環境色 × (F × scale + bias)
        vec3 specularIBL = prefilteredColor * (F_ambient * brdf.x + brdf.y);

        // AO適用: ディフューズ→AO直接、スペキュラ→スペキュラAO（Lagarde法）
        ambient = (diffuseIBL * ao + specularIBL * specularAO) * iblIntensity;
    }
    else
    {
        // ========================================
        // フォールバック: フラットアンビエントライト
        // ========================================
        vec3 ambientLight = params.ambientColor.rgb * params.ambientColor.w;
        vec3 diffuseAmbient = kD_ambient * ambientLight * albedo;
        vec3 specularAmbient = F_ambient * ambientLight * (1.0 - roughness * 0.5);
        ambient = diffuseAmbient * ao + specularAmbient * specularAO;
    }

    // 直接光へのAO適用（マイクロシャドウ近似）:
    // 直接光はライト方向が明確なため、AOは控えめに適用（30%）
    // アンビエント/IBLへはフルAO適用（上記で適用済み）
    float directAO = mix(1.0, ao, 0.3);
    float directSpecAO = mix(1.0, specularAO, 0.3);

    // 最終カラー（HDR）
    vec3 color = ambient + Lo_diffuse * directAO + Lo_specular * directSpecAO + emissive;
    outColor = vec4(color, 1.0);
}
