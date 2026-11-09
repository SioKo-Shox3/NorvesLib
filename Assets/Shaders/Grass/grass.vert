#version 450

// 生成されたブレードを二次ベジエの帯へ展開する。CPU の写しは GrassBladeMesh.cpp。
#include "Grass/GrassDraw.glsl"
#include "Common/Wind.glsl"

layout(std430, set = 0, binding = 1) readonly buffer GrassBlades
{
    GrassBladeRecord Blades[];
} blades;
layout(std430, set = 0, binding = 2) readonly buffer GrassSpecies
{
    GrassSpeciesGpuData Species[];
} speciesTable;
layout(set = 0, binding = 3) uniform sampler2DArray layerA;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragV;
layout(location = 3) out float fragColorVariation;
layout(location = 4) out float fragWetness;
layout(location = 5) flat out uint fragSpecies;
layout(location = 6) out float fragCameraDistance;
layout(location = 7) out vec4 fragCurrentClip;
layout(location = 8) out vec4 fragPreviousClip;

// 以下の G1〜G6 は CPU の同名段階と演算順をそろえる。退化時も有限の位置と法線を保つ。
float GrassLength(vec3 value)
{
    precise float result = sqrt((value.x * value.x + value.y * value.y) + value.z * value.z);
    return result;
}

vec3 GrassNormalize(vec3 value, vec3 fallback)
{
    precise float size = GrassLength(value);
    precise vec3 result = size > 1.0e-6 ? value * (1.0 / size) : fallback;
    return result;
}

// CPU の GrassTransform と同じ列参照・左からの積和。行列同士の先行乗算を避ける。
vec4 GrassTransform(mat4 matrix, vec4 value)
{
    precise vec4 result;
    for (int row = 0; row < 4; ++row)
    {
        result[row] = ((matrix[0][row] * value.x + matrix[1][row] * value.y) +
            matrix[2][row] * value.z) + matrix[3][row] * value.w;
    }
    return result;
}
float GrassWetness(vec3 root)
{
    if (draw.Surface.x <= 0.0)
    {
        return 0.0;
    }
    precise float chunkSize = draw.LayerWindow.x;
    int n = int(draw.LayerWindow.y);
    int resolution = int(draw.LayerWindow.z);
    ivec2 chunk = ivec2(floor(root.xz / chunkSize));
    ivec2 slot = ((chunk % n) + n) % n;
    precise vec2 local = root.xz - vec2(chunk) * chunkSize;
    // SampleLayerA の最近傍と同じ floor(local * invPixelWidth)。B は UNORM のバイト / 255。
    precise float invPixelWidth = float(resolution) / chunkSize;
    ivec2 pixel = clamp(ivec2(floor(local * invPixelWidth)), ivec2(0), ivec2(resolution - 1));
    precise float moisture = texelFetch(layerA, ivec3(pixel, slot.y * n + slot.x), 0).b;
    return clamp(draw.Surface.x * (0.6 + 0.8 * moisture), 0.0, 1.0);
}

vec3 EvaluateGrassBladeVertex(GrassBladeRecord blade, GrassSpeciesGpuData species, uint lod,
    uint index, bool bPrevious, float wetness, out vec3 normal)
{
    // G1: パックの復号、セルの再導出、塩 4/5/7 による向き・位相・傾き。
    precise vec3 root = vec3(blade.PositionX, blade.PositionY, blade.PositionZ);
    precise float h = (float(blade.Packed & 1023u) / 1023.0) * draw.Quant.x;
    precise float w = (float((blade.Packed >> 10) & 255u) / 255.0) * draw.Quant.y;
    precise float fade = float(blade.Packed >> 24) / 255.0;
    precise float v = float(GrassBladeIndexRow(index)) / float(draw.LodSegments[lod]);
    uvec2 cell = uvec2(ivec2(floor(root.xz / draw.BandSpacingMeters[lod])));
    uint salt = lod * 8u;
    precise float u = 2.0 * GrassHashToUnit(GrassHash(cell.x, cell.y, salt + kGrassHashSaltFacing)) - 1.0;
    precise vec3 facing = GrassNormalize(vec3(1.0 - u * u, 0.0, 2.0 * u), vec3(1.0, 0.0, 0.0));
    precise vec3 side = vec3(-facing.z, 0.0, facing.x);
    precise float phase = GrassHashToUnit(GrassHash(cell.x, cell.y, salt + kGrassHashSaltPhase));
    precise float lean = GrassHashToUnit(GrassHash(cell.x, cell.y, salt + kGrassHashSaltLean));

    // G2: 遠景は縦横を fade 倍、近景は 0.35〜1.2 m の滑らかな補間で高さだけ 0.25〜1 倍。
    bool bHistory = bPrevious && (draw.Flags.x & kGrassDrawFlagPreviousCamera) != 0u;
    precise vec4 camera = bHistory ? draw.PreviousCameraPosition : draw.CameraPosition;
    precise float time = bHistory ? draw.Wind.w : draw.Wind.z;
    precise float d = GrassLength(root - camera.xyz);
    precise float nearT = clamp((d - draw.NearFade.y) / max(draw.NearFade.x - draw.NearFade.y, 1.0e-6), 0.0, 1.0);
    precise float nearSmooth = (nearT * nearT) * (3.0 - 2.0 * nearT);
    h = (h * fade) * (0.25 + 0.75 * nearSmooth);
    w = w * fade;

    // G3: 傾きは高さ × Bend × 0.4〜1 倍、風は 0.04 秒相当、個体の揺れは 0.9〜1.1 倍。
    precise float leanSigned = 2.0 * lean - 1.0;
    precise vec3 leanDirection = GrassNormalize(facing * leanSigned + side * (1.0 - abs(leanSigned)), side);
    precise vec3 offset = leanDirection * ((h * species.Bend) * (0.4 + 0.6 * lean));
    if ((draw.Flags.x & kGrassDrawFlagWind) != 0u)
    {
        precise vec3 velocity = WindVelocity(root, time, draw.Wind.xy, draw.WindParams.x, draw.WindParams.y,
            draw.WindParams.z, draw.WindParams.w, draw.WindExtra.x);
        precise float flutter = 0.9 + 0.2 * WindValueNoise1D(time * 0.7 + phase * 19.0, 0u);
        precise float response = ((h * 0.04) * species.WindResponse) / max(species.Stiffness, 0.05);
        offset = offset + velocity * (response * flutter);
    }
    offset = offset + vec3(draw.Trample.x, 0.0, draw.Trample.z) * species.TrampleResponse;
    offset = offset + leanDirection * ((h * species.WetDroop) * wetness);
    precise float offsetLength = GrassLength(offset);
    precise float maxOffset = h * 0.8;
    if (offsetLength > maxOffset)
    {
        offset = offset * (maxOffset / offsetLength);
    }

    // G4: 制御点は高さの 0.5 倍。先端の高さを下げ、根元から先端までの距離を h に保つ。
    precise float offsetSquared = (offset.x * offset.x + offset.y * offset.y) + offset.z * offset.z;
    precise float tipHeight = sqrt(max(h * h - offsetSquared, 0.0));
    precise vec3 p1 = root + vec3(0.0, h * 0.5, 0.0);
    precise vec3 p2 = (root + vec3(0.0, tipHeight, 0.0)) + offset;
    precise float inverseV = 1.0 - v;
    precise vec3 center = (root * (inverseV * inverseV) + p1 * ((2.0 * inverseV) * v)) + p2 * (v * v);
    precise vec3 tangent = (p1 - root) * (2.0 * inverseV) + (p2 - p1) * (2.0 * v);

    // G5: 面と視線の内積が 0.5 未満なら水平ビルボードへ寄せる。左右と法線の符号を保つ。
    precise vec3 view = GrassNormalize(vec3(camera.x - center.x, 0.0, camera.z - center.z), facing);
    precise float facingDot = facing.x * view.x + facing.z * view.z;
    precise float turn = clamp((0.5 - abs(facingDot)) * 2.0, 0.0, 1.0);
    precise vec3 viewFacing = view * (facingDot < 0.0 ? -1.0 : 1.0);
    precise vec3 viewSide = vec3(-viewFacing.z, 0.0, viewFacing.x);
    side = GrassNormalize(side + (viewSide - side) * turn, side);
    normal = GrassNormalize(cross(tangent, side), facing);

    // G6: 先端幅は根元の 0.1 倍。最小画素幅は FoliagePass と同じ拡幅で、倍率に上限を設ける。
    precise float halfWidth = (0.5 * w) * (1.0 - 0.9 * v);
    precise float pixelScale = bHistory && camera.w <= 0.0 ? draw.CameraPosition.w : camera.w;
    if (draw.Surface.y > 0.0 && halfWidth > 1.0e-6)
    {
        precise float pixelMeters = pixelScale * GrassLength(center - camera.xyz);
        precise float targetHalfWidth = (0.5 * draw.Surface.y) * pixelMeters;
        if (halfWidth < targetHalfWidth)
        {
            halfWidth = halfWidth * min(targetHalfWidth / halfWidth, max(draw.Surface.z, 1.0));
        }
    }
    precise float sideSign = GrassBladeIndexSide(index) == 0u ? -1.0 : 1.0;
    precise vec3 result = center + side * (halfWidth * sideSign);
    return result;
}

void main()
{
    GrassBladeRecord blade = blades.Blades[gl_InstanceIndex];
    uint index = uint(gl_VertexIndex);
    uint lod = GrassBladeIndexLod(index);
    fragSpecies = (blade.Packed >> 18) & 63u;
    GrassSpeciesGpuData species = speciesTable.Species[fragSpecies];
    precise vec3 root = vec3(blade.PositionX, blade.PositionY, blade.PositionZ);
    fragWetness = GrassWetness(root);
    fragWorldPos = EvaluateGrassBladeVertex(blade, species, lod, index, false, fragWetness, fragNormal);
    vec3 previousNormal;
    precise vec3 previousWorld = EvaluateGrassBladeVertex(blade, species, lod, index, true, fragWetness, previousNormal);
    fragV = float(GrassBladeIndexRow(index)) / float(draw.LodSegments[lod]);
    uvec2 cell = uvec2(ivec2(floor(root.xz / draw.BandSpacingMeters[lod])));
    fragColorVariation = 0.85 + 0.3 * GrassHashToUnit(GrassHash(cell.x, cell.y, lod * 8u + kGrassHashSaltColor));
    fragCameraDistance = GrassLength(root - draw.CameraPosition.xyz);
    fragCurrentClip = GrassTransform(draw.Projection, GrassTransform(draw.View, vec4(fragWorldPos, 1.0)));
    fragPreviousClip = (draw.Flags.x & kGrassDrawFlagPreviousCamera) != 0u
        ? GrassTransform(draw.PreviousProjection, GrassTransform(draw.PreviousView, vec4(previousWorld, 1.0))) : fragCurrentClip;
    gl_Position = fragCurrentClip;
}
