#version 450
#extension GL_GOOGLE_include_directive : require
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

// スキン済みの体を殻ごとに描き、セル内の毛の被覆を GBuffer に書く。
// set 0 の binding 3〜6 は長さ・流れ・密度と束・領域として読み替える。
// P0 は画素の濡れ度（インスタンスの濡れ度 × 領域の濡れやすさ）、P1 は流れの角度。
// 雨など環境の濡れは適用せず、暗化・彩度・粗さ・長さ・太さ・束の応答に材質の値を使う。

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveChromaticityAndLuminanceNits;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in vec3 fragViewDir;
layout(location = 6) in vec4 fragCurrentClip;
layout(location = 7) in vec4 fragPreviousClip;
layout(location = 8) flat in vec4 fragInstanceShadingParams;
layout(location = 9) in float fragShellHeight;
layout(location = 10) flat in uint fragShellIndex;
layout(location = 11) flat in uint fragShellCount;

#include "Common/FurStrand.glsl"
#include "Common/SurfaceWetness.glsl"
#include "Common/GBufferEncoding.glsl"
#include "Common/VelocityEncoding.glsl"
#include "Common/AlphaClip.glsl"
#include "Common/ThinGeometry.glsl"
#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"

layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 3) uniform sampler2D lengthTexture;
layout(set = 0, binding = 4) uniform sampler2D flowTexture;
layout(set = 0, binding = 5) uniform sampler2D densityClumpTexture;
layout(set = 0, binding = 6) uniform sampler2D regionTexture;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec2 outVelocity;

// 長さに対する高さから、セル幅を単位とする毛の半径を求める。
float FurRadiusAtHeight(float height, float strandLength, float thicknessScale)
{
    float t = clamp(height / strandLength, 0.0, 1.0);
    float taper = pow(1.0 - t, mvp.fur.strand.z);
    return mix(mvp.fur.strand.y, mvp.fur.strand.x, taper) * thicknessScale;
}

void main()
{
    uint mapMask = mvp.fur.counts.z;
    vec3 albedo = texture(albedoTexture, fragTexCoord).rgb;
    float lengthSample = 1.0;
    vec2 flowSample = vec2(0.5);
    vec2 densityClump = vec2(1.0);
    float regionSample = 0.0;

    // 暗黙の画面微分を使う標本と余接フレームは、画素ごとの破棄より前に取る。
    if ((mapMask & FUR_MAP_MASK_LENGTH) != 0u)
    {
        lengthSample = texture(lengthTexture, fragTexCoord).r;
    }
    if ((mapMask & FUR_MAP_MASK_FLOW) != 0u)
    {
        flowSample = texture(flowTexture, fragTexCoord).rg;
    }
    if ((mapMask & FUR_MAP_MASK_DENSITY_CLUMP) != 0u)
    {
        densityClump = texture(densityClumpTexture, fragTexCoord).rg;
    }
    if ((mapMask & FUR_MAP_MASK_REGION) != 0u)
    {
        ivec2 regionSize = textureSize(regionTexture, 0);
        ivec2 regionCoord = clamp(ivec2(fract(fragTexCoord) * vec2(regionSize)), ivec2(0), regionSize - 1);
        regionSample = texelFetch(regionTexture, regionCoord, 0).r;
    }

    FurMaterialRegion region = mvp.fur.regions[FurRegionIndex(regionSample, mvp.fur.counts.y)];
    // 画素の濡れ w = W × 領域の濡れやすさ s。W はスキンのインスタンスの P0（無ければ材質の値）。
    float instanceWetness = clamp(ResolveShadingParams(fragInstanceShadingParams, mvp.shadingParams).x, 0.0, 1.0);
    float wetSusceptibility = clamp(region.wet.x, 0.0, 1.0);
    float wetness = instanceWetness * wetSusceptibility;
    // 濡れの応答は C++ が W を掛けて渡す。w について一次の式なので、掛け済みの係数と s を使う。
    vec3 wetGeometry = mvp.fur.wetGeometry.xyz * wetSusceptibility; // x: 長さの縮み、y: 太さの増し、z: 束の寄せ（垂れは頂点）。
    vec3 normal = normalize(fragNormal);
    mat3 cotangentFrame = CalculateCotangentFrame(normal, fragWorldPos, fragTexCoord);
    vec2 flow = 2.0 * flowSample - 1.0;
    vec3 flowTangent = cotangentFrame[0];
    // RG8 の中央の量子化誤差も向きなしとして扱う。
    if ((mapMask & FUR_MAP_MASK_FLOW) != 0u && dot(flow, flow) > 4.0 / (255.0 * 255.0))
    {
        flowTangent = cotangentFrame[0] * flow.x + cotangentFrame[1] * flow.y;
    }
    if (dot(flowTangent, flowTangent) <= 1.0e-12)
    {
        flowTangent = cotangentFrame[0];
    }
    if (dot(flowTangent, flowTangent) <= 1.0e-12)
    {
        flowTangent = cotangentFrame[1];
    }
    if (dot(flowTangent, flowTangent) <= 1.0e-12)
    {
        vec3 otherTangent;
        FurFlowBasis(normal, flowTangent, otherTangent);
    }
    flowTangent = normalize(flowTangent);

    vec2 cell = fragTexCoord * mvp.fur.growth.y * densityClump.r;
    vec2 cellFootprint = fwidth(cell);
    float footprint = max(cellFootprint.x, cellFootprint.y);
    float height = fragShellIndex == 0u ? 0.0 : fragShellHeight;

    if (fragShellIndex > 0u)
    {
        float maxLength = lengthSample * region.shading.z * (1.0 - wetGeometry.x);
        if (densityClump.r <= 0.0 || maxLength <= 0.0)
        {
            discard;
        }

        ivec2 cellIndex = ivec2(floor(cell));
        float lengthRandom = FurCellHash2(cellIndex).y;
        float strandLength = maxLength * mix(0.6, 1.0, lengthRandom);
        float radius = FurRadiusAtHeight(height, strandLength, region.shading.w * (1.0 + wetGeometry.y));
        bool lastShell = fragShellCount > 1u && fragShellIndex == fragShellCount - 1u;
        float lastShellFraction = clamp(mvp.furShellParams.y, 0.0, 1.0);
        uint clipMode = uint(mvp.shadingModel.z + 0.5);
        uint clipPhase = uint(mvp.shadingModel.w + 0.5);
        vec2 pixel = floor(gl_FragCoord.xy);

        if (footprint <= 0.7)
        {
            if (height > strandLength)
            {
                discard;
            }

            float pixelsPerCell = footprint > 0.0 ? 1.0 / footprint : 1.0e20;
            vec2 widthCoverage = ClampStrandWidth(2.0 * radius, 1.0, pixelsPerCell);
            if (widthCoverage.x <= 0.0)
            {
                discard;
            }
            float centerMargin = min(0.5 * widthCoverage.x, 0.5);
            vec2 centerRandom = FurCellHash2(cellIndex + ivec2(19, 71));
            vec2 center = mix(vec2(centerMargin), vec2(1.0 - centerMargin), centerRandom);
            // 濡れの束は毛先ほど、毛の中心を ClumpSize の UV 幅の塊の中心へ kClump × w × G × h だけ寄せる。
            // 毛は自分のセルの中でしか判定しないため、寄せた中心をセルの内側に収める。
            float clumpPull = clamp(wetGeometry.z * densityClump.g * height, 0.0, 1.0);
            if (clumpPull > 0.0)
            {
                float clumpCells = max(mvp.fur.growth.z * mvp.fur.growth.y * densityClump.r, 1.0);
                vec2 clumpCenter = (floor(cell / clumpCells) + 0.5) * clumpCells - vec2(cellIndex);
                center = clamp(mix(center, clumpCenter, clumpPull), vec2(centerMargin), vec2(1.0 - centerMargin));
            }
            if (length(fract(cell) - center) > 0.5 * widthCoverage.x)
            {
                discard;
            }
            if (lastShell && FurCellHash(cellIndex + ivec2(83, 47)) >= lastShellFraction)
            {
                discard;
            }
            if (widthCoverage.y < 1.0 &&
                (widthCoverage.y <= 0.0 ||
                 AlphaClipShouldDiscard(widthCoverage.y, mvp.shadingModel.y, clipMode, pixel, clipPhase)))
            {
                discard;
            }
        }
        else
        {
            // 長さが [0.6, 1.0] × maxLength に一様分布するときの残存率。
            float survival = clamp((maxLength - height) / (0.4 * maxLength), 0.0, 1.0);
            float coverage = survival * min(1.0, PI * radius * radius);
            if (lastShell)
            {
                coverage *= lastShellFraction;
            }
            if (coverage <= 0.0 ||
                AlphaClipShouldDiscard(coverage, mvp.shadingModel.y, clipMode, pixel, clipPhase))
            {
                discard;
            }
        }
    }

    float brightness = mix(1.0 - region.shading.x, 1.0 + region.shading.y, height);
    albedo = mix(albedo * brightness, mvp.fur.undercoat.rgb, mvp.fur.undercoat.a * (1.0 - height));
    // 暗化・彩度低下・粗さ低下の係数には W が掛け済みなので、濡れ度の引数に s を使う。
    // kDark = 1 − WetDarken、kDesat = WetDesaturate、kWetRough = 粗さ × (1 − WetRoughness)。
    float roughness = mvp.furShellParams.w;
    albedo = SurfaceWetnessAlbedo(albedo, wetSusceptibility, 1.0 - mvp.fur.wetShading.x, mvp.fur.wetShading.y);
    roughness = SurfaceWetnessRoughness(roughness, wetSusceptibility, roughness * (1.0 - mvp.fur.wetShading.z));
    float ao = mix(mvp.fur.occlusion.x, 1.0, pow(height, mvp.fur.occlusion.y));
    vec3 outputNormal = normalize(mix(normal, flowTangent, mvp.fur.strand.w * height));

    // wetShading.w は WetGloss × W なので、領域の濡れやすさを掛けて g = kGloss・w とする。
    float wetGloss = mvp.fur.wetShading.w * wetSusceptibility;
    outAlbedo = vec4(albedo, GBufferEncodeFurWetGloss(wetGloss));
    outNormal = GBufferEncodeNormal(outputNormal, GBUFFER_SHADING_MODEL_FUR, 0u);
    outMaterial = GBufferEncodeMaterialForShadingModel(
        vec3(0.0, roughness, ao), wetness, GBUFFER_SHADING_MODEL_FUR);
    outEmissive = GBufferEncodeEmissive(vec3(0.0), FurEncodeFlowAngle(outputNormal, flowTangent));

    outVelocity = vec2(0.0);
    if (mvp.frameParams.x > 0.5 && abs(fragCurrentClip.w) > 1.0e-6)
    {
        outVelocity = VelocityFromClip(fragCurrentClip.xy / fragCurrentClip.w, fragPreviousClip);
    }
}
