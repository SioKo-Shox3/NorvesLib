#version 450

// 二次ベジエの草の帯を両面で GBuffer 5 枚へ書く。
#include "Grass/GrassDraw.glsl"
#include "Common/GBufferEncoding.glsl"
#include "Common/WetSurface.glsl"
#include "Common/VelocityEncoding.glsl"

NORVES_DECLARE_ENVIRONMENT_UBO(0, 6)

layout(std430, set = 0, binding = 2) readonly buffer GrassSpecies
{
    GrassSpeciesGpuData Species[];
} speciesTable;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragV;
layout(location = 3) in float fragColorVariation;
layout(location = 4) in float fragWetness;
layout(location = 5) flat in uint fragSpecies;
layout(location = 6) in float fragCameraDistance;
layout(location = 7) in vec4 fragCurrentClip;
layout(location = 8) in vec4 fragPreviousClip;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec2 outVelocity;

const int kBayer4[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);

void main()
{
    if (fragCameraDistance < draw.NearFade.y)
    {
        ivec2 pixel = ivec2(gl_FragCoord.xy) & ivec2(3);
        float threshold = (float(kBayer4[pixel.y * 4 + pixel.x]) + 0.5) / 16.0;
        if (fragCameraDistance / draw.NearFade.y < threshold)
        {
            discard;
        }
    }
    GrassSpeciesGpuData species = speciesTable.Species[fragSpecies];
    vec3 normal = normalize(fragNormal);
    // 巻き順と投影の Y 反転に依存せず、法線をカメラの側へ向ける。
    vec3 toCamera = draw.CameraPosition.xyz - fragWorldPos;
    if (dot(normal, toCamera) < 0.0)
    {
        normal = -normal;
    }
    vec3 geometricNormal = normal;
    // 両面とも上向きを 0.35 混ぜ、薄い帯の照明を安定させる。
    normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), 0.35));
    vec3 albedo = mix(species.RootColor.rgb, species.TipColor.rgb, fragV) * fragColorVariation;
    albedo *= mix(vec3(1.0), species.WetColor.rgb, fragWetness);
    float roughness = mix(species.Roughness, species.WetRoughness, fragWetness);
    float metallic = 0.0;
    ApplyWetSurface(albedo, roughness, metallic, normal, geometricNormal,
                    u_Environment.params.surface.x, fragWetness, u_Environment.params.surface.y, 1.0);
    outAlbedo = vec4(albedo, 1.0);
    outNormal = GBufferEncodeNormal(normal, GBUFFER_SHADING_MODEL_DEFAULT_LIT, GBUFFER_RECEIVER_FLAG_FOLIAGE_CLUMP);
    outMaterial = GBufferEncodeMaterial(vec3(metallic, roughness, mix(0.35, 1.0, fragV)), 0.0);
    outEmissive = GBufferEncodeEmissive(vec3(0.0), 0.0);

    // 前のフレームの w が閾値以下なら履歴なしの印を書く。
    outVelocity = vec2(0.0);
    if ((draw.Flags.x & kGrassDrawFlagPreviousCamera) != 0u &&
        abs(fragCurrentClip.w) > 1.0e-6)
    {
        outVelocity = VelocityFromClip(fragCurrentClip.xy / fragCurrentClip.w, fragPreviousClip);
    }
}
