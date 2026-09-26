#version 450

// ラスタの被写界深度のgather。深度からPTと同じ薄レンズの式でCoCを求め、各画素が直径CoCの円板へ
// 一様に散らした寄与を中心の画素で集める（scatter-as-gather）。
// 背景（ピント面より奥）: 重みは被覆/円板の面積で、正規化した平均にする。中心より奥の画素の広がりは
// 中心のCoCまでに抑え、ピントの合った手前の物体へ奥のぼけが乗らないようにする。
// 前景（ピント面より手前）: 重みの和（散らした被覆の密度）を不透明度として背景の上に重ねる。前ボケの縁は
// 半透明になり、隠れた背景は近傍の背景の画素で埋める。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 1) uniform sampler2D sceneDepthTexture;

layout(std140, set = 0, binding = 2) uniform DepthOfFieldParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndFocusDistance;
    vec4 cameraForwardAndCocScale;   // w: 入力画像の画素でのCoCの直径の係数
    vec4 imageSizeAndLimits;         // xy: 寸法、z: CoCの半径の上限、w: 拡大の倍率
} params;

const float Pi = 3.14159265358979;

// 符号付きのCoCの半径（入力画像の画素）。負はピント面より手前。空（深度1）は無限遠の値。
float SignedCocRadius(ivec2 texel, ivec2 size)
{
    float depth = texelFetch(sceneDepthTexture, texel, 0).r;
    float focusDistance = params.cameraPositionAndFocusDistance.w;
    float cocScale = params.cameraForwardAndCocScale.w;
    float maxRadius = params.imageSizeAndLimits.z;
    if (isnan(depth) || isinf(depth) || depth >= 0.999999)
    {
        return min(0.5 * cocScale, maxRadius);
    }
    vec2 uv = (vec2(texel) + 0.5) / vec2(size);
    vec4 world = params.inverseViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    if (abs(world.w) <= 1.0e-8)
    {
        return 0.0;
    }
    float viewDepth = dot(world.xyz / world.w - params.cameraPositionAndFocusDistance.xyz,
                          params.cameraForwardAndCocScale.xyz);
    if (!(viewDepth > 1.0e-4))
    {
        return -maxRadius;
    }
    float radius = 0.5 * cocScale * (viewDepth - focusDistance) / viewDepth;
    return clamp(radius, -maxRadius, maxRadius);
}

// 半径rの円板の中心から距離dの画素の被覆（縁を1画素幅で線形に落とす）と、円板の面積で割った重み。
float ScatterWeight(float radius, float distanceToCenter)
{
    float r = max(radius, 0.5);
    float coverage = clamp(r + 0.5 - distanceToCenter, 0.0, 1.0);
    return coverage / max(Pi * r * r, 1.0);
}

void main()
{
    ivec2 size = ivec2(params.imageSizeAndLimits.xy);
    ivec2 center = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
    vec4 centerColor = texelFetch(sceneColorTexture, center, 0);
    float centerCoc = SignedCocRadius(center, size);
    float centerRadius = abs(centerCoc);
    float maxRadius = params.imageSizeAndLimits.z;
    int searchRadius = int(ceil(maxRadius + 0.5));

    vec3 farSum = vec3(0.0);
    float farWeight = 0.0;
    vec3 nearSum = vec3(0.0);
    float nearWeight = 0.0;
    for (int dy = -searchRadius; dy <= searchRadius; ++dy)
    {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx)
        {
            float distanceToCenter = length(vec2(dx, dy));
            if (distanceToCenter > maxRadius + 0.5)
            {
                continue;
            }
            ivec2 texel = clamp(center + ivec2(dx, dy), ivec2(0), size - 1);
            float coc = SignedCocRadius(texel, size);
            vec3 color = texelFetch(sceneColorTexture, texel, 0).rgb;
            if (coc < 0.0)
            {
                float weight = ScatterWeight(-coc, distanceToCenter);
                nearSum += weight * color;
                nearWeight += weight;
            }
            else
            {
                // CoCは深度に対して単調に増えるため、CoCの大小で前後を比べる。
                float radius = coc > centerCoc ? min(coc, max(centerRadius, 0.5)) : coc;
                float weight = ScatterWeight(radius, distanceToCenter);
                farSum += weight * color;
                farWeight += weight;
            }
        }
    }

    vec3 nearColor = nearWeight > 0.0 ? nearSum / nearWeight : centerColor.rgb;
    vec3 farColor = farWeight > 0.0 ? farSum / farWeight : nearColor;
    float nearAlpha = clamp(nearWeight, 0.0, 1.0);
    outColor = vec4(mix(farColor, nearColor, nearAlpha), centerColor.a);
}
