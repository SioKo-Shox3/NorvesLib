#version 450

// ラスタの被写界深度のgather。深度からPTと同じ薄レンズの式でCoCを求め、各画素が直径CoCの円板へ
// 一様に散らした寄与を中心の画素で集める（scatter-as-gather）。
// 前景と背景の層: 中心へ届く標本のうち最も手前のCoCを求め、それに近い標本を前景、残りを背景に分ける
// （ピント面の前後ではなく、その画素で最も手前の面を基準にする）。前景は重みの和（散らした被覆の密度）を
// 不透明度として背景の上に重ね、背景は正規化した平均にする。前ボケの縁の外側と手前の面の後ろの穴は、
// 背景の層を広げた近傍の背景の画素で埋める。
// 背景の標本のうち中心より奥のものの広がりは中心のCoC（+半画素弱の余裕）までに抑え、ピントの合った手前の
// 物体へ奥のぼけが乗らないようにする。
// PTはピントを合わせた像面の倍率で画角を狭めるため、出力の画素を入力の像の部分画素の位置へ写してから集める
// （拡大を別の補間で行うと、明るい光源の縁がにじむ）。

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
// 前景とみなす、最も手前のCoCからの差の幅（画素）。この幅で前景の割合を1から0へ滑らかに落とす。
const float ForegroundBand = 0.25;
// 中心より奥の背景の標本の広がりを抑える上限に足す余裕（画素）。中心の画素の縁が部分画素の位置に
// あるため、ピントの合った物体の縁にも奥の面が半画素ほど回り込む。
const float BehindSpreadSlack = 0.35;

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
    float filmScale = params.imageSizeAndLimits.w;
    vec2 centerPosition = 0.5 * vec2(size) + (gl_FragCoord.xy - 0.5 * vec2(size)) * filmScale;
    ivec2 center = clamp(ivec2(floor(centerPosition)), ivec2(0), size - 1);
    vec2 subPixel = centerPosition - (vec2(center) + 0.5);
    vec4 centerColor = texelFetch(sceneColorTexture, center, 0);
    float centerCoc = SignedCocRadius(center, size);
    float centerRadius = abs(centerCoc);
    float maxRadius = params.imageSizeAndLimits.z;
    int searchRadius = int(ceil(maxRadius + 1.0));

    // 散らした円板がこの画素へ届く標本のうち、最も手前のCoC。
    float nearestCoc = centerCoc;
    for (int dy = -searchRadius; dy <= searchRadius; ++dy)
    {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx)
        {
            float distanceToCenter = length(vec2(dx, dy) - subPixel);
            if (distanceToCenter > maxRadius + 0.5)
            {
                continue;
            }
            ivec2 texel = clamp(center + ivec2(dx, dy), ivec2(0), size - 1);
            float coc = SignedCocRadius(texel, size);
            if (ScatterWeight(abs(coc), distanceToCenter) > 0.0)
            {
                nearestCoc = min(nearestCoc, coc);
            }
        }
    }

    vec3 foregroundSum = vec3(0.0);
    float foregroundWeight = 0.0;
    vec3 backgroundSum = vec3(0.0);
    float backgroundWeight = 0.0;
    for (int dy = -searchRadius; dy <= searchRadius; ++dy)
    {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx)
        {
            float distanceToCenter = length(vec2(dx, dy) - subPixel);
            if (distanceToCenter > maxRadius + 0.5)
            {
                continue;
            }
            ivec2 texel = clamp(center + ivec2(dx, dy), ivec2(0), size - 1);
            float coc = SignedCocRadius(texel, size);
            vec3 color = texelFetch(sceneColorTexture, texel, 0).rgb;
            float foregroundShare = 1.0 - smoothstep(0.0, ForegroundBand, coc - nearestCoc);
            float weight = ScatterWeight(abs(coc), distanceToCenter) * foregroundShare;
            foregroundSum += weight * color;
            foregroundWeight += weight;
            // CoCは深度に対して単調に増えるため、CoCの大小で前後を比べる。
            float radius = abs(coc);
            if (coc > centerCoc)
            {
                radius = min(radius, max(centerRadius + BehindSpreadSlack, 0.5));
            }
            weight = ScatterWeight(radius, distanceToCenter) * (1.0 - foregroundShare);
            backgroundSum += weight * color;
            backgroundWeight += weight;
        }
    }

    vec3 foregroundColor = foregroundWeight > 0.0 ? foregroundSum / foregroundWeight : centerColor.rgb;
    vec3 backgroundColor = backgroundWeight > 0.0 ? backgroundSum / backgroundWeight : foregroundColor;
    float foregroundAlpha = clamp(foregroundWeight, 0.0, 1.0);
    outColor = vec4(mix(backgroundColor, foregroundColor, foregroundAlpha), centerColor.a);
}
