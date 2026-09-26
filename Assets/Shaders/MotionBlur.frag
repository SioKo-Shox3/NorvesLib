#version 450

// ラスタの動きぼけのgather。シャッター区間はPTの連番の1フレームと同じく前後の間隔の終わり側にあり、
// 現在の像で位置qにある面は、区間の終わりからτ（シャッターの間の動きを1とする割合）だけ前の時刻には
// q - τ × 動き(q)にあった。したがって時刻τに中心の画素pを覆う面は、現在の像で p + τ × 動き(q) にある。
// 各時刻の標本は二つの候補から選ぶ:
// - 隣の3×3 tileで最も長い動きの向きの標本 q = p + τ × 最長の動き。その面が時刻τにpを覆い（動きが
//   最長の動きに近い）、pより手前なら前景としてpの上を通り過ぎる。
// - 自分の動きの向きの標本 q = p + τ × 動き(p)。その面が時刻τにpを覆う（同じ動きの面）なら使う。
// どちらもpを覆わない時刻は、現在の像では手前の物体に隠れた面がpに見えていたので、pと自分の動きの向きの
// 標本のうち奥の側で埋める。像の外の標本は縁の画素を使う。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D velocityTexture;
layout(set = 0, binding = 1) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 2) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 3) uniform sampler2D tileMaxTexture;

layout(std140, set = 0, binding = 4) uniform MotionBlurParams
{
    mat4 inverseViewProjection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPositionAndHistory;   // w: 前のカメラがあるか
    vec4 imageSizeAndShutter;        // xy: 寸法、z: シャッター時間/フレーム長、w: 動きの長さの上限（画素）
    vec4 tileInfo;                   // x: tileの一辺、yz: tileの数
} params;

// シャッター区間を等分する時刻の数（動きの長さの上限の画素数と同じ）。
const int SampleCount = 32;
// 標本の面が時刻τに中心の画素を覆うとみなす、面の位置と中心の差の上限（画素、各軸）。
const float CoverRadius = 0.6;
// 手前・奥を分けるカメラからの距離の相対差。
const float DepthSeparation = 0.02;
const float SkyDistance = 1.0e30;

vec2 ImageSize()
{
    return params.imageSizeAndShutter.xy;
}

ivec2 ClampTexel(vec2 position)
{
    return clamp(ivec2(floor(position)), ivec2(0), ivec2(ImageSize()) - 1);
}

// 画素のシャッターの間の動き（画素）。空（深度1）は無限遠の方向を前のカメラへ投影して求める。
vec2 ShutterMotion(ivec2 texel)
{
    vec2 size = ImageSize();
    vec2 velocity = texelFetch(velocityTexture, texel, 0).xy;
    float depth = texelFetch(sceneDepthTexture, texel, 0).r;
    if (depth >= 1.0 && params.cameraPositionAndHistory.w > 0.5)
    {
        vec2 ndc = (vec2(texel) + 0.5) / size * 2.0 - 1.0;
        vec4 world = params.inverseViewProjection * vec4(ndc, 1.0, 1.0);
        velocity = vec2(0.0);
        if (abs(world.w) > 1.0e-12)
        {
            vec3 direction = world.xyz / world.w - params.cameraPositionAndHistory.xyz;
            vec4 previousClip = params.previousProjection * params.previousView * vec4(direction, 0.0);
            if (previousClip.w > 1.0e-6)
            {
                velocity = (ndc - previousClip.xy / previousClip.w) * 0.5;
            }
        }
    }
    vec2 motion = velocity * size * params.imageSizeAndShutter.z;
    float length2 = dot(motion, motion);
    float limit = params.imageSizeAndShutter.w;
    if (!(length2 < 1.0e12))
    {
        return vec2(0.0);
    }
    return length2 > limit * limit ? motion * (limit / sqrt(length2)) : motion;
}

// カメラから面までの距離（空は非常に遠い値）。
float ViewDistance(ivec2 texel)
{
    float depth = texelFetch(sceneDepthTexture, texel, 0).r;
    if (depth >= 1.0)
    {
        return SkyDistance;
    }
    vec2 ndc = (vec2(texel) + 0.5) / ImageSize() * 2.0 - 1.0;
    vec4 world = params.inverseViewProjection * vec4(ndc, depth, 1.0);
    if (abs(world.w) <= 1.0e-12)
    {
        return SkyDistance;
    }
    return length(world.xyz / world.w - params.cameraPositionAndHistory.xyz);
}

// 現在の像でtexelにある面（動きmotion）が、時刻τに中心centerを覆うか。
bool Covers(ivec2 texel, vec2 motion, float tau, vec2 center)
{
    vec2 offset = abs(vec2(texel) + 0.5 - tau * motion - center);
    return max(offset.x, offset.y) <= CoverRadius;
}

void main()
{
    ivec2 texel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), ivec2(ImageSize()) - 1);
    vec4 centerColor = texelFetch(sceneColorTexture, texel, 0);
    vec2 center = vec2(texel) + 0.5;

    // 隣の3×3 tileで最も長い動き。
    ivec2 tileCount = ivec2(params.tileInfo.yz);
    ivec2 tile = texel / int(params.tileInfo.x);
    vec2 longest = vec2(0.0);
    float longestLength = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            ivec2 neighbor = tile + ivec2(x, y);
            if (any(lessThan(neighbor, ivec2(0))) || any(greaterThanEqual(neighbor, tileCount)))
            {
                continue;
            }
            vec4 tileMax = texelFetch(tileMaxTexture, neighbor, 0);
            if (tileMax.z > longestLength)
            {
                longest = tileMax.xy;
                longestLength = tileMax.z;
            }
        }
    }
    if (longestLength < 0.25)
    {
        outColor = centerColor;
        return;
    }

    vec2 centerMotion = ShutterMotion(texel);
    float centerDistance = ViewDistance(texel);
    vec3 sum = vec3(0.0);
    for (int sampleIndex = 0; sampleIndex < SampleCount; ++sampleIndex)
    {
        float tau = (float(sampleIndex) + 0.5) / float(SampleCount);
        ivec2 foreground = ClampTexel(center + tau * longest);
        vec2 foregroundMotion = ShutterMotion(foreground);
        float foregroundDistance = ViewDistance(foreground);
        bool bForegroundCovers = Covers(foreground, foregroundMotion, tau, center);
        if (bForegroundCovers && foregroundDistance < centerDistance * (1.0 - DepthSeparation))
        {
            sum += texelFetch(sceneColorTexture, foreground, 0).rgb;
            continue;
        }
        ivec2 own = ClampTexel(center + tau * centerMotion);
        vec2 ownMotion = ShutterMotion(own);
        if (Covers(own, ownMotion, tau, center))
        {
            sum += texelFetch(sceneColorTexture, own, 0).rgb;
            continue;
        }
        if (bForegroundCovers)
        {
            sum += texelFetch(sceneColorTexture, foreground, 0).rgb;
            continue;
        }
        // 手前の物体に隠れていた面が見える時刻: 中心と自分の動きの向きの標本のうち奥の側で埋める。
        sum += ViewDistance(own) > centerDistance * (1.0 + DepthSeparation)
                   ? texelFetch(sceneColorTexture, own, 0).rgb
                   : centerColor.rgb;
    }
    outColor = vec4(sum / float(SampleCount), centerColor.a);
}
