#version 450

layout(location = 0) in vec2 fragUV;

// GBufferテクスチャ
layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 3) uniform sampler2D gbufferDepth;

// ライティングパラメータ
layout(set = 0, binding = 4) uniform LightingParams
{
    mat4 invViewProjection;
    vec4 cameraPosition;    // xyz=position, w=unused
    vec4 ambientColor;      // xyz=color, w=intensity
    uint lightCount;
    uint _pad0;
    uint _pad1;
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

layout(location = 0) out vec4 outColor;

// ========================================
// PBR関連関数
// ========================================

const float PI = 3.14159265359;

// フレネル（Schlickの近似）
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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
// 深度からワールド座標を復元
// ========================================
vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    // UV→NDC座標変換 (VulkanのY軸反転を考慮)
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = params.invViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
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

void main()
{
    // GBufferからデータを取得
    vec4 albedoSample = texture(gbufferAlbedo, fragUV);
    vec4 normalSample = texture(gbufferNormal, fragUV);
    vec4 materialSample = texture(gbufferMaterial, fragUV);
    float depthSample = texture(gbufferDepth, fragUV).r;

    // アルファが0の場合はスキップ（描画されていないピクセル）
    if (albedoSample.a < 0.01)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // データ展開
    vec3 albedo = albedoSample.rgb;
    vec3 N = normalize(normalSample.xyz);
    float metallic = materialSample.r;
    float roughness = materialSample.g;
    float ao = materialSample.b;

    // ワールド座標を復元
    vec3 worldPos = ReconstructWorldPosition(fragUV, depthSample);

    // カメラからの視線ベクトル
    vec3 V = normalize(params.cameraPosition.xyz - worldPos);

    // 基本反射率（非金属: 0.04、金属: アルベドカラー）
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // ライティング計算
    vec3 Lo = vec3(0.0);

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

        // Cook-Torrance BRDF
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

        Lo += (kD * albedo / PI + specular) * lightColor * NdotL * attenuation;
    }

    // アンビエントライト
    vec3 ambient = params.ambientColor.rgb * params.ambientColor.w * albedo * ao;

    // 最終カラー（HDR）
    vec3 color = ambient + Lo;
    outColor = vec4(color, 1.0);
}
