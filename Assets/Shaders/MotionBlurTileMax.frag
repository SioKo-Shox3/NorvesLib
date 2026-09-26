#version 450

// 動きぼけのtileごとの最も長い動き。tileの全画素のシャッターの間の動き（画素）を求め、長さが最大のものを
// xyへ、その長さをzへ書く。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outTileMax;

layout(set = 0, binding = 0) uniform sampler2D velocityTexture;
layout(set = 0, binding = 1) uniform sampler2D sceneDepthTexture;

layout(std140, set = 0, binding = 4) uniform MotionBlurParams
{
    mat4 inverseViewProjection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPositionAndHistory;   // w: 前のカメラがあるか
    vec4 imageSizeAndShutter;        // xy: 寸法、z: シャッター時間/フレーム長、w: 動きの長さの上限（画素）
    vec4 tileInfo;                   // x: tileの一辺、yz: tileの数
} params;

// 画素のシャッターの間の動き（画素）。空（深度1）は無限遠の方向を前のカメラへ投影して求める。
vec2 ShutterMotion(ivec2 texel)
{
    vec2 size = params.imageSizeAndShutter.xy;
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

void main()
{
    int tileSize = int(params.tileInfo.x);
    ivec2 size = ivec2(params.imageSizeAndShutter.xy);
    ivec2 origin = ivec2(gl_FragCoord.xy) * tileSize;
    vec2 longest = vec2(0.0);
    float longestLength2 = 0.0;
    for (int y = 0; y < tileSize; ++y)
    {
        for (int x = 0; x < tileSize; ++x)
        {
            ivec2 texel = origin + ivec2(x, y);
            if (texel.x >= size.x || texel.y >= size.y)
            {
                continue;
            }
            vec2 motion = ShutterMotion(texel);
            float length2 = dot(motion, motion);
            if (length2 > longestLength2)
            {
                longest = motion;
                longestLength2 = length2;
            }
        }
    }
    outTileMax = vec4(longest, sqrt(longestLength2), 0.0);
}
