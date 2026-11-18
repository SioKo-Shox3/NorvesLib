// 八面体アトラスの近い 3 セルを補間し、インポスターの材質・補正した深度・速度を GBuffer へ書く。
#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common/ImpostorOctahedral.glsl"
#include "Common/GBufferEncoding.glsl"
#include "Common/WetSurface.glsl"
#include "Common/VelocityEncoding.glsl"
#include "Common/LodCrossfade.glsl"

layout(std140, set = 0, binding = 0) uniform ImpostorGBufferData
{
    mat4 view;
    mat4 projection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 frameParams;
    vec4 atlasParams;
} impostor;

layout(set = 0, binding = 1) uniform sampler2D albedoAtlas;
layout(set = 0, binding = 2) uniform sampler2D normalAtlas;
layout(set = 0, binding = 3) uniform sampler2D ormAtlas;
layout(set = 0, binding = 4) uniform sampler2D depthAtlas;

NORVES_DECLARE_ENVIRONMENT_UBO(0, 5)

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragQuadUV;
layout(location = 2) flat in mat3 fragObjectRotation;
layout(location = 5) flat in vec3 fragObjectViewDirection;
layout(location = 6) flat in vec3 fragPreviousOriginDelta;
layout(location = 7) flat in vec2 fragSizeWorld;
layout(location = 8) flat in vec4 fragAtlasInfo;
layout(location = 9) flat in float fragCellResolution;
layout(location = 10) flat in vec3 fragTint;
layout(location = 11) flat in float fragLodCrossfade;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec2 outVelocity;

vec2 AtlasCellUV(vec2 cell, vec2 grid, vec2 atlasSize, vec2 quadUV)
{
    vec2 cellSize = 1.0 / grid;
    vec2 cellMin = cell * cellSize;
    vec2 cellUV = clamp(vec2(quadUV.x, 1.0 - quadUV.y), 0.0, 1.0);
    vec2 uv = cellMin + cellUV * cellSize;
    vec2 texelInset = 0.5 / atlasSize;
    return clamp(uv, cellMin + texelInset, cellMin + cellSize - texelInset);
}

vec3 NormalizeOrDefault(vec3 direction, vec3 defaultDirection)
{
    float directionLength = length(direction);
    return directionLength > 0.0 ? direction / directionLength : defaultDirection;
}

void main()
{
    vec2 grid = max(fragAtlasInfo.xy, vec2(1.0));
    float cellResolution = max(fragCellResolution, 1.0);
    vec2 atlasSize = max(fragAtlasInfo.zw, grid * cellResolution);
    vec2 encoded = EncodeImpostorOctahedral(fragObjectViewDirection, int(impostor.atlasParams.x + 0.5));
    vec2 cellSpace = encoded * (grid - vec2(1.0));
    vec2 cellBase = floor(cellSpace);
    vec2 fraction = clamp(cellSpace - cellBase, vec2(0.0), vec2(1.0));

    // 区画の対角線の両側で共通の 2 セルを使い、重みを連続につなぐ。
    vec2 cell0;
    vec2 cell1 = min(cellBase + vec2(1.0, 0.0), grid - vec2(1.0));
    vec2 cell2 = min(cellBase + vec2(0.0, 1.0), grid - vec2(1.0));
    vec3 weights;
    if (fraction.x + fraction.y < 1.0)
    {
        cell0 = cellBase;
        weights = vec3(1.0 - fraction.x - fraction.y, fraction.x, fraction.y);
    }
    else
    {
        cell0 = min(cellBase + vec2(1.0), grid - vec2(1.0));
        weights = vec3(fraction.x + fraction.y - 1.0, 1.0 - fraction.y, 1.0 - fraction.x);
    }

    // すべてのアトラスで上下反転と半テクセルの寄せをそろえる。
    vec2 uv0 = AtlasCellUV(cell0, grid, atlasSize, fragQuadUV);
    vec2 uv1 = AtlasCellUV(cell1, grid, atlasSize, fragQuadUV);
    vec2 uv2 = AtlasCellUV(cell2, grid, atlasSize, fragQuadUV);
    vec4 atlasColor = texture(albedoAtlas, uv0) * weights.x +
                      texture(albedoAtlas, uv1) * weights.y +
                      texture(albedoAtlas, uv2) * weights.z;

    vec3 geomNormal = NormalizeOrDefault(impostor.cameraPosition.xyz - fragWorldPos, vec3(0.0, 0.0, 1.0));
    vec3 normal = geomNormal;
    if (impostor.frameParams.y > 0.5)
    {
        vec3 objectNormal = (texture(normalAtlas, uv0).xyz * 2.0 - 1.0) * weights.x +
                            (texture(normalAtlas, uv1).xyz * 2.0 - 1.0) * weights.y +
                            (texture(normalAtlas, uv2).xyz * 2.0 - 1.0) * weights.z;
        objectNormal = NormalizeOrDefault(objectNormal, transpose(fragObjectRotation) * geomNormal);
        normal = NormalizeOrDefault(fragObjectRotation * objectNormal, geomNormal);
    }

    vec3 orm = vec3(1.0, impostor.atlasParams.z, impostor.atlasParams.w);
    if (impostor.frameParams.z > 0.5)
    {
        orm = texture(ormAtlas, uv0).rgb * weights.x +
              texture(ormAtlas, uv1).rgb * weights.y +
              texture(ormAtlas, uv2).rgb * weights.z;
    }

    vec3 surfacePosition = fragWorldPos;
    if (impostor.frameParams.w > 0.5)
    {
        float depth = texture(depthAtlas, uv0).r * weights.x +
                      texture(depthAtlas, uv1).r * weights.y +
                      texture(depthAtlas, uv2).r * weights.z;
        float radius = 0.5 * max(fragSizeWorld.x, fragSizeWorld.y);
        // 焼きの深度 0 は手前、1 は奥なので、浅いほどカメラの側へずらす。
        surfacePosition += geomNormal * radius * (1.0 - 2.0 * depth);
    }

    // 暗黙の画面微分を使う標本は切り抜きより前に済ませる。
    if (atlasColor.a < impostor.atlasParams.y)
    {
        discard;
    }

    // 段の遷移のディザ。値は customData.w、0 は遷移なし。
    if (LodCrossfadeShouldDiscard(fragLodCrossfade, floor(gl_FragCoord.xy)))
    {
        discard;
    }

    vec3 albedo = atlasColor.rgb * fragTint;
    float metallic = orm.b;
    float roughness = orm.g;
    float ao = orm.r;
    ApplyWetSurfaceFromEnvironment(albedo, roughness, metallic, normal, geomNormal, u_Environment.params,
                                   ResolveWetSurfaceResponse(-1.0, -1.0, true));

    outAlbedo = vec4(albedo, 1.0);
    outNormal = GBufferEncodeNormal(normal, GBUFFER_SHADING_MODEL_DEFAULT_LIT, 0u);
    outMaterial = GBufferEncodeMaterial(vec3(metallic, roughness, ao), 0.0);
    outEmissive = GBufferEncodeEmissive(vec3(0.0), 0.0);

    // 深度と速度は同じ補正済みの面の位置から求める。
    vec4 currentClip = impostor.projection * impostor.view * vec4(surfacePosition, 1.0);
    gl_FragDepth = clamp(currentClip.z / currentClip.w, 0.0, 1.0);
    outVelocity = vec2(0.0);
    if (impostor.frameParams.x > 0.5 && abs(currentClip.w) > 1e-6)
    {
        vec4 previousClip = impostor.previousProjection * impostor.previousView *
                            vec4(surfacePosition + fragPreviousOriginDelta, 1.0);
        outVelocity = VelocityFromClip(currentClip.xy / currentClip.w, previousClip);
    }
}
