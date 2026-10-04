#version 450
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragObjectColor;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 emissiveColor;
    vec4 pomParams; // x=heightScale, y=hasHeightMap, z=ORMの1枚が metallicTexture の枠に張られているか（1/0）, w=法線が2チャンネル（BC5）か（1/0）
    vec4 sceneColorParams;
    mat4 lightView[4];
    mat4 lightProjection[4];
    vec4 shadowSplitDistances[2];
    uint cascadeCount;
    uint lightCount;
    uint bShadowEnabled;
    uint bIBLEnabled;
    uint prefilteredSpecularMipLevels;
    float iblIntensity;
    uint bVirtualTexture; // 材質のテクスチャが sparse（VT）か（1/0）
    uint padding1;
    uint padding2;
    vec4 cameraForward;
} mvp;

layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;

struct LightData
{
    vec4 position;
    vec4 direction;
    vec4 chromaticityAndIntensity;
    vec4 attenuation;
};

layout(std430, set = 0, binding = 8) readonly buffer LightBuffer
{
    LightData lights[];
} lightBuffer;

layout(set = 0, binding = 9) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 10) uniform sampler2D environmentRadiance;
layout(set = 0, binding = 11) uniform sampler2D diffuseIrradiance;
layout(set = 0, binding = 12) uniform sampler2D prefilteredSpecular;
layout(set = 0, binding = 13) uniform sampler2D dfgLut;
// 点光源のキューブシャドウ（LightingPassと同じキューブ配列。光源バッファのattenuation.wで引く）
layout(set = 0, binding = 14) uniform samplerCubeArray pointShadowCubes;

layout(location = 0) out vec4 outColor;

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/PbrMaterialTextureSampling.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"
#include "Common/PointShadow.glsl"

vec2 EquirectangularUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989);
    return uv + 0.5;
}

float CalculateInverseSquareAttenuation(float distanceToLight)
{
    return 1.0 / max(distanceToLight * distanceToLight, 0.01 * 0.01);
}

float CalculateRangeWindow(float distanceToLight, float range)
{
    float rangeRatio = distanceToLight / max(range, 0.0001);
    float window = max(1.0 - pow(rangeRatio, 4.0), 0.0);
    return window * window;
}

float GetShadowSplitDistance(uint splitIndex)
{
    return splitIndex < 4u
               ? mvp.shadowSplitDistances[0][splitIndex]
               : mvp.shadowSplitDistances[1][splitIndex - 4u];
}

bool IsFiniteShadowValue(float value)
{
    return !isnan(value) && !isinf(value);
}

bool HasValidCascadedShadowData()
{
    if (mvp.bShadowEnabled == 0u || mvp.cascadeCount != 4u)
    {
        return false;
    }

    float previousSplit = GetShadowSplitDistance(0u);
    if (!IsFiniteShadowValue(previousSplit))
    {
        return false;
    }
    for (uint splitIndex = 1u; splitIndex < 5u; ++splitIndex)
    {
        float split = GetShadowSplitDistance(splitIndex);
        if (!IsFiniteShadowValue(split) || split <= previousSplit)
        {
            return false;
        }
        previousSplit = split;
    }
    return true;
}

float SampleShadowCascade(vec3 worldPos, uint cascadeIndex)
{
    vec4 lightSpacePosition = mvp.lightProjection[cascadeIndex] *
                               mvp.lightView[cascadeIndex] *
                               vec4(worldPos, 1.0);
    if (!IsFiniteShadowValue(lightSpacePosition.w) || abs(lightSpacePosition.w) < 0.000001)
    {
        return 1.0;
    }

    vec3 projection = lightSpacePosition.xyz / lightSpacePosition.w;
    vec2 shadowUV = projection.xy * 0.5 + 0.5;
    float receiverDepth = projection.z;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        receiverDepth < 0.0 || receiverDepth > 1.0)
    {
        return 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    const vec2 offsets[4] = vec2[4](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5, 0.5), vec2(0.5, 0.5));
    float visible = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        float shadowDepth = texture(shadowMap,
                                    vec3(shadowUV + offsets[index] * texelSize,
                                         float(cascadeIndex))).r;
        visible += receiverDepth - 0.005 <= shadowDepth ? 1.0 : 0.0;
    }
    return visible * 0.25;
}

float CalculateShadow(vec3 worldPos)
{
    if (!HasValidCascadedShadowData())
    {
        return 1.0;
    }

    vec3 viewForward = mvp.cameraForward.xyz;
    float forwardLength = length(viewForward);
    if (!IsFiniteShadowValue(forwardLength) || forwardLength <= 0.00001)
    {
        return 1.0;
    }
    viewForward /= forwardLength;
    float receiverDistance = dot(worldPos - mvp.cameraPosition.xyz, viewForward);
    float nearDistance = GetShadowSplitDistance(0u);
    float farDistance = GetShadowSplitDistance(4u);
    if (!IsFiniteShadowValue(receiverDistance) ||
        receiverDistance < nearDistance || receiverDistance > farDistance)
    {
        return 1.0;
    }

    uint cascadeIndex = 3u;
    for (uint candidate = 0u; candidate < 3u; ++candidate)
    {
        if (receiverDistance < GetShadowSplitDistance(candidate + 1u))
        {
            cascadeIndex = candidate;
            break;
        }
    }

    float shadow = SampleShadowCascade(worldPos, cascadeIndex);
    if (cascadeIndex < 3u)
    {
        float boundary = GetShadowSplitDistance(cascadeIndex + 1u);
        float previousBoundary = GetShadowSplitDistance(cascadeIndex);
        float blendWidth = max((boundary - previousBoundary) * 0.1, 0.001);
        float blendStart = boundary - blendWidth;
        if (receiverDistance > blendStart)
        {
            float nextShadow = SampleShadowCascade(worldPos, cascadeIndex + 1u);
            float blend = smoothstep(blendStart, boundary, receiverDistance);
            shadow = mix(shadow, nextShadow, blend);
        }
    }
    else
    {
        // 影の最大距離の手前、最後のカスケードの奥の10%で影を薄め、境界で急に消えないようにする。
        float fadeWidth = max((farDistance - GetShadowSplitDistance(3u)) * 0.1, 0.001);
        shadow = mix(shadow, 1.0, smoothstep(farDistance - fadeWidth, farDistance, receiverDistance));
    }
    return shadow;
}

void main()
{
    vec2 texCoord = fragTexCoord;
    // 余接フレームは元のUVから一度だけ作り、POMと法線マップの両方に使う。
    mat3 TBN = CalculateCotangentFrame(fragNormal, fragWorldPos, fragTexCoord);
    vec3 viewDirection = normalize(mvp.cameraPosition.xyz - fragWorldPos);
    bool bVirtualTexture = mvp.bVirtualTexture != 0u;
    if (mvp.pomParams.y > 0.5)
    {
        texCoord = ApplyParallaxOcclusionMapping(heightTexture, fragTexCoord, TBN, viewDirection,
                                                 mvp.pomParams.x, bVirtualTexture);
    }

    PbrMaterialTextureSamples textureSamples = SamplePbrMaterialTextures(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord,
        mvp.pomParams.z > 0.5, mvp.pomParams.w > 0.5, bVirtualTexture);
    vec4 texColor = textureSamples.Albedo;
    vec3 baseColor = texColor.rgb * fragObjectColor.rgb;
    float alpha = texColor.a * fragObjectColor.a;

    if (alpha <= 0.001)
    {
        discard;
    }

    vec3 normal = ApplyTangentSpaceNormal(TBN, textureSamples.TangentNormal);
    float metallic = clamp(textureSamples.Material.r, 0.0, 1.0);
    float roughness = clamp(textureSamples.Material.g, 0.04, 1.0);
    float ao = clamp(textureSamples.Material.b, 0.0, 1.0);
    vec3 direct = vec3(0.0);
    float NdotV = max(dot(normal, viewDirection), 0.0);
    vec3 F0d = vec3(0.04);
    vec3 F0c = baseColor;
    vec2 dfg = texture(dfgLut,
                       clamp(vec2(NdotV, roughness),
                             vec2(0.5 / 256.0),
                             vec2(255.5 / 256.0))).rg;
    float Ess = max(dfg.x + dfg.y, 0.0001);
    vec3 compensationD = vec3(1.0) + F0d * (1.0 - Ess) / Ess;
    vec3 compensationC = vec3(1.0) + F0c * (1.0 - Ess) / Ess;

    for (uint index = 0u; index < mvp.lightCount; ++index)
    {
        LightData light = lightBuffer.lights[index];
        float lightType = light.position.w;
        vec3 lightDirection;
        float attenuation = 1.0;
        if (lightType < 0.5)
        {
            lightDirection = normalize(-light.direction.xyz);
        }
        else
        {
            vec3 toLight = light.position.xyz - fragWorldPos;
            float distanceToLight = length(toLight);
            lightDirection = toLight / max(distanceToLight, 0.0001);
            attenuation = CalculateInverseSquareAttenuation(distanceToLight) *
                          CalculateRangeWindow(distanceToLight, light.attenuation.x);
        }

        float NdotL = max(dot(normal, lightDirection), 0.0);
        if (NdotL <= 0.0)
        {
            continue;
        }
        vec3 halfVector = normalize(viewDirection + lightDirection);
        vec3 diffuseBRDF;
        vec3 specularBRDF;
        EvaluateAnalyticalDirectEndpointBRDF(
            baseColor, metallic, roughness, normal, viewDirection, lightDirection,
            halfVector, dfg, diffuseBRDF, specularBRDF);
        vec3 radiance = light.chromaticityAndIntensity.rgb *
                        light.chromaticityAndIntensity.w * attenuation * NdotL;
        // CSMはattenuation.z=1の方向光（CSMを選んだ灯）だけへ掛ける。
        if (lightType < 0.5 && light.attenuation.z > 0.5 && mvp.bShadowEnabled != 0u)
        {
            radiance *= CalculateShadow(fragWorldPos);
        }
        // 点光源のキューブシャドウはattenuation.w=キューブの番号+1の灯だけへ掛ける。
        else if (lightType > 0.5 && lightType < 1.5 && light.attenuation.w > 0.5)
        {
            radiance *= SamplePointShadow(pointShadowCubes,
                                          light.attenuation.w - 1.0,
                                          light.position.xyz,
                                          light.attenuation.x,
                                          fragWorldPos,
                                          normal);
        }
        direct += (diffuseBRDF + specularBRDF) * radiance;
    }

    vec3 ambient = vec3(0.0);
    if (mvp.bIBLEnabled != 0u)
    {
        vec3 irradiance = textureLod(diffuseIrradiance,
                                     EquirectangularUV(normal),
                                     0.0).rgb;
        vec3 reflectionDirection = reflect(-viewDirection, normal);
        vec3 sourceRadiance = textureLod(environmentRadiance,
                                         EquirectangularUV(reflectionDirection),
                                         0.0).rgb;
        float mipDenominator = float(max(mvp.prefilteredSpecularMipLevels, 1u) - 1u);
        vec3 prefilteredColor = mipDenominator > 0.0
                                    ? textureLod(prefilteredSpecular,
                                                 EquirectangularUV(reflectionDirection),
                                                 roughness * mipDenominator).rgb
                                    : sourceRadiance;
        vec3 Ed = clamp((F0d * dfg.x + dfg.y) * compensationD,
                        vec3(0.0), vec3(1.0));
        vec3 Ec = clamp((F0c * dfg.x + dfg.y) * compensationC,
                        vec3(0.0), vec3(1.0));
        vec3 diffuseIBL = irradiance * (baseColor / PI) *
                          (1.0 - metallic) * (vec3(1.0) - Ed);
        vec3 specularIBL = prefilteredColor *
                           ((1.0 - metallic) * Ed + metallic * Ec);
        float specularAO = ComputeSpecularAO(NdotV, ao, roughness);
        ambient = (diffuseIBL * ao + specularIBL * specularAO) * mvp.iblIntensity;
    }

    vec3 emissive = mvp.emissiveColor.rgb * mvp.emissiveColor.a;
    outColor = vec4((direct + ambient + emissive) * mvp.sceneColorParams.x, alpha);
}
