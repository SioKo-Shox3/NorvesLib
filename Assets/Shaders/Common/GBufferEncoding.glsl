// GBuffer の空きチャネルに格納する材質値と受け手の印を符号化・復号する。
#ifndef NORVES_GBUFFER_ENCODING_GLSL
#define NORVES_GBUFFER_ENCODING_GLSL

// 定数表は Rendering/ShadingModelIds.h と同じ値に保つ。
const uint GBUFFER_SHADING_MODEL_DEFAULT_LIT = 0u;
const uint GBUFFER_SHADING_MODEL_FUR = 1u;
const uint GBUFFER_SHADING_MODEL_EYE = 2u;
const uint GBUFFER_SHADING_MODEL_PALE_FLAT = 3u;
const uint GBUFFER_SHADING_MODEL_ID_BITS = 4u;
const uint GBUFFER_SHADING_MODEL_ID_MASK = 15u;
const uint GBUFFER_RECEIVER_FLAG_TERRAIN = 1u;
const uint GBUFFER_RECEIVER_FLAG_TIGHT_TERRAIN_SHADOW = 2u;
const uint GBUFFER_RECEIVER_FLAG_FOLIAGE_CLUMP = 4u;
const uint GBUFFER_RECEIVER_FLAG_DIFFUSE_ONLY = 8u;
const uint GBUFFER_RECEIVER_FLAG_MASK = 15u;

// 復号後の法線は正規化せず、格納された xyz をそのまま返す。
struct GBufferNormalData
{
    vec3 normal;
    uint shadingModelId;
    uint receiverFlags;
};

// 法線の xyz と、ID と印をまとめた整数を格納する。
vec4 GBufferEncodeNormal(vec3 normal, uint shadingModelId, uint receiverFlags)
{
    uint id = shadingModelId & GBUFFER_SHADING_MODEL_ID_MASK;
    uint flags = receiverFlags & GBUFFER_RECEIVER_FLAG_MASK;
    return vec4(normal, float((flags << GBUFFER_SHADING_MODEL_ID_BITS) | id));
}

// 金属度・粗さ・アンビエントオクルージョンと材質値 P0 を格納する。
vec4 GBufferEncodeMaterial(vec3 metallicRoughnessAo, float p0)
{
    return vec4(metallicRoughnessAo, p0);
}

// PaleFlat は金属度を 0、粗さを 1 にし、SSR が粗い画素を棄却する性質で SSR を改造せずに反射を切る。
vec4 GBufferEncodeMaterialForShadingModel(vec3 metallicRoughnessAo, float p0, uint shadingModelId)
{
    if ((shadingModelId & GBUFFER_SHADING_MODEL_ID_MASK) == GBUFFER_SHADING_MODEL_PALE_FLAT)
    {
        metallicRoughnessAo.x = 0.0;
        metallicRoughnessAo.y = 1.0;
    }
    return GBufferEncodeMaterial(metallicRoughnessAo, p0);
}

// プリエクスポージャを掛けた発光と材質値 P1 を格納する。
// DefaultLit の P1 は水たまりの被覆 0〜1（水たまりが無ければ 0）。
vec4 GBufferEncodeEmissive(vec3 preExposedEmissive, float p1)
{
    return vec4(preExposedEmissive, p1);
}

// デカール法線の合成重みは A に置き、書き込みマスクで Normal.w を保護する。
vec4 GBufferEncodeDecalNormalBlend(vec3 normal, float blendWeight)
{
    return vec4(normal, blendWeight);
}

// Normal.w を整数に戻し、ID と受け手の印を取り出す。
GBufferNormalData GBufferDecodeNormal(vec4 normalSample)
{
    uint packed = uint(clamp(round(normalSample.w), 0.0, 2047.0));
    GBufferNormalData data;
    data.normal = normalSample.xyz;
    data.shadingModelId = packed & GBUFFER_SHADING_MODEL_ID_MASK;
    data.receiverFlags = (packed >> GBUFFER_SHADING_MODEL_ID_BITS) & GBUFFER_RECEIVER_FLAG_MASK;
    return data;
}

// 復号済みの受け手が指定された印を持つか調べる。
bool GBufferNormalHasReceiverFlag(GBufferNormalData data, uint receiverFlag)
{
    return (data.receiverFlags & receiverFlag) != 0u;
}

// シェーディングモデル ID を取り出す。
uint GBufferDecodeShadingModelId(vec4 normalSample)
{
    return GBufferDecodeNormal(normalSample).shadingModelId;
}

// 受け手の印を取り出す。
uint GBufferDecodeReceiverFlags(vec4 normalSample)
{
    return GBufferDecodeNormal(normalSample).receiverFlags;
}

// 標本の受け手が指定された印を持つか調べる。
bool GBufferHasReceiverFlag(vec4 normalSample, uint receiverFlag)
{
    return (GBufferDecodeReceiverFlags(normalSample) & receiverFlag) != 0u;
}

// Material.a から材質値 P0 を取り出す。
float GBufferDecodeP0(vec4 materialSample)
{
    return materialSample.a;
}

// Emissive.a から材質値 P1 を取り出す。
float GBufferDecodeP1(vec4 emissiveSample)
{
    return emissiveSample.a;
}

// Fur の画素の Albedo.a に濡れのつや g = kGloss・w を符号化する。
// g = 0 なら 1.0 ちょうど、最小値は 3/255 で空の判定（0.01）と重ならない。
float GBufferEncodeFurWetGloss(float g)
{
    return 1.0 - clamp(g, 0.0, 1.0) * (252.0 / 255.0);
}

// Fur の画素の Albedo.a から濡れのつや g を復号する。
float GBufferDecodeFurWetGloss(float albedoAlpha)
{
    return clamp((1.0 - albedoAlpha) * (255.0 / 252.0), 0.0, 1.0);
}

// 汎用の書き手が Fur の材質を書くとき、テクスチャの alpha が濡れのつやとして読まれないよう A をそろえる。
// 空の印（0.01 未満）は保つ。
float GBufferAlbedoAlphaForShadingModel(float albedoAlpha, uint shadingModelId)
{
    return (shadingModelId & GBUFFER_SHADING_MODEL_ID_MASK) == GBUFFER_SHADING_MODEL_FUR && albedoAlpha >= 0.01
        ? 1.0 : albedoAlpha;
}

// MaterialTypes.h の ResolveShadingParam と同じ規約で、各成分を解決する。
vec4 ResolveShadingParams(vec4 instanceParams, vec4 materialParams)
{
    vec4 defaults = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 resolved = mix(defaults, materialParams, greaterThanEqual(materialParams, vec4(0.0)));
    return mix(resolved, instanceParams, greaterThanEqual(instanceParams, vec4(0.0)));
}

#endif
