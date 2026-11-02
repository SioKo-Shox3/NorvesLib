#version 450

// 手前の物に隠れない円盤から、画面の高さを単位とする歪みを出力する。
// 独立した二つの値ノイズで方向を作り、円盤の縁で滑らかに弱める。

layout(set = 0, binding = 0, std140) uniform DistortionData
{
    mat4 viewProjection;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 params;
} distortion;

layout(set = 0, binding = 1) uniform sampler2D sceneDepth;

layout(location = 0) in vec2 corner;
layout(location = 1) flat in float strength;
layout(location = 2) flat in float patternScale;
layout(location = 3) flat in float scrollSpeed;
layout(location = 4) flat in uint seed;

layout(location = 0) out vec2 outOffset;

float latticeValue(ivec2 lattice, uint noiseSeed)
{
    // 負の格子座標も整数のビット列として種と混ぜ、上位24ビットを0〜1へ写す。
    uvec2 cell = uvec2(lattice);
    uint hash = cell.x * 0x9e3779b9u ^ cell.y * 0x85ebca6bu ^ noiseSeed;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16u;
    return float(hash >> 8u) / 16777215.0;
}

float valueNoise(vec2 p, uint noiseSeed)
{
    ivec2 lattice = ivec2(floor(p));
    vec2 blend = smoothstep(vec2(0.0), vec2(1.0), fract(p));
    float bottom = mix(latticeValue(lattice, noiseSeed),
        latticeValue(lattice + ivec2(1, 0), noiseSeed), blend.x);
    float top = mix(latticeValue(lattice + ivec2(0, 1), noiseSeed),
        latticeValue(lattice + ivec2(1, 1), noiseSeed), blend.x);
    return mix(bottom, top, blend.y);
}

void main()
{
    if (texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r < gl_FragCoord.z)
    {
        discard;
    }

    float r = length(corner);
    if (r >= 1.0)
    {
        discard;
    }

    float edgeWeight = 1.0 - r * r;
    float falloff = edgeWeight * edgeWeight;
    // 時間を局所yから引くため、正の速さでは模様がcameraUpの向きへ流れる。
    vec2 p = corner * patternScale
        + vec2(0.0, -distortion.params.x * scrollSpeed * patternScale);
    float nx = valueNoise(p, seed);
    float ny = valueNoise(p, seed ^ 0x68bc21ebu);
    vec2 dir = vec2(nx, ny) * 2.0 - 1.0;
    outOffset = dir * strength * falloff;
}
