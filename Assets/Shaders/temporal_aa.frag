#version 450

// TAA の解決。ジッタを掛けて描いた SceneColor の画素に、前のフレームの履歴を混ぜる。
// 1. 3×3 の近傍の色（YCoCg）から平均と標準偏差を求め、最も手前の画素を探す。
// 2. その画素の動きで履歴を再投影し、平均 ± gamma × 標準偏差の箱の中心へ向けてクリップする
//    （箱の外の履歴はゴーストとみなす）。動きは、カメラの動きを履歴を書いたフレームのカメラから深度で求め、
//    物体自身の動き（velocity からパケットの前のカメラによる動きを引いた分）を経過時間の比で伸ばして足す。
//    RenderThread がパケットを飛ばすと velocity は履歴より新しいフレームからの動きになるため。連続した
//    フレームでは2つのカメラが同じで比は1なので、velocity（currentUV - previousUV）そのものになる。
// 3. 現在の色と履歴を 1/(1 + 輝度) の重みで混ぜ、明るい画素のちらつきを抑える。
// 履歴が無いときと、再投影が画面の外に出たときは現在の色をそのまま使う。
// ジッタは前のカメラと履歴のカメラにも同じ量を掛けてあるので、velocity と再投影にはジッタが入らない。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 1) uniform sampler2D historyTexture;
layout(set = 0, binding = 2) uniform sampler2D velocityTexture;
layout(set = 0, binding = 3) uniform sampler2D sceneDepthTexture;

layout(std140, set = 0, binding = 4) uniform TemporalAAParams
{
    mat4 inverseViewProjection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPositionAndHistory;   // w: 履歴を使うか
    vec4 imageSize;                  // xy: 寸法、zw: 逆数
    vec4 blend;                      // x: 現在の色の割合、y: 履歴へ掛ける露出の比、z: クリップの箱の半幅
    vec4 jitter;                     // xy: ジッタ（画素）
    mat4 historyView;                // 履歴を書いたフレームのカメラ（今のフレームと同じジッタ）
    mat4 historyProjection;
    vec4 motion;                     // x: 物体自身の動きを履歴からの経過時間へ伸ばす比
} params;

// RGBA16F の上限。非有限値や溢れた値が履歴へ残り続けないように切る。
const float MaxColor = 65504.0;

vec3 SanitizeColor(vec3 color)
{
    bvec3 bad = bvec3(isnan(color.x) || isinf(color.x),
                      isnan(color.y) || isinf(color.y),
                      isnan(color.z) || isinf(color.z));
    color = mix(color, vec3(0.0), bad);
    return clamp(color, vec3(0.0), vec3(MaxColor));
}

vec3 RgbToYCoCg(vec3 rgb)
{
    return vec3(0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
                0.5 * rgb.r - 0.5 * rgb.b,
                -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b);
}

vec3 YCoCgToRgb(vec3 ycocg)
{
    return vec3(ycocg.x + ycocg.y - ycocg.z,
                ycocg.x + ycocg.z,
                ycocg.x - ycocg.y - ycocg.z);
}

// 箱の中心から履歴への線分が箱を出る点へ縮める（箱の中ならそのまま）。
vec3 ClipToBox(vec3 history, vec3 center, vec3 extents)
{
    vec3 offset = history - center;
    vec3 unit = abs(offset) / max(extents, vec3(1.0e-5));
    float scale = max(unit.x, max(unit.y, unit.z));
    return scale > 1.0 ? center + offset / scale : history;
}

// 履歴を Catmull-Rom で読む（双線形で毎フレーム読み直すと、動く面のぼけが積もるため）。
// 4×4 の重みのうち四隅を除いた12画素を、双線形の5回の読み出しにまとめる。負の重みで出た値はクリップで抑える。
vec3 SampleHistoryCatmullRom(vec2 uv)
{
    vec2 samplePosition = uv * params.imageSize.xy;
    vec2 texelPosition1 = floor(samplePosition - 0.5) + 0.5;
    vec2 f = samplePosition - texelPosition1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 uv0 = (texelPosition1 - 1.0) * params.imageSize.zw;
    vec2 uv3 = (texelPosition1 + 2.0) * params.imageSize.zw;
    vec2 uv12 = (texelPosition1 + w2 / w12) * params.imageSize.zw;
    vec3 result = texture(historyTexture, vec2(uv12.x, uv0.y)).rgb * (w12.x * w0.y) +
                  texture(historyTexture, vec2(uv0.x, uv12.y)).rgb * (w0.x * w12.y) +
                  texture(historyTexture, uv12).rgb * (w12.x * w12.y) +
                  texture(historyTexture, vec2(uv3.x, uv12.y)).rgb * (w3.x * w12.y) +
                  texture(historyTexture, vec2(uv12.x, uv3.y)).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return result / max(weight, 1.0e-4);
}

// 画素が view・projection のカメラから今のカメラへ動いた量（currentUV - previousUV）。深度から位置を
// 戻して投影する。空（深度1）は無限遠の方向を投影する。投影できなければ0。
vec2 CameraMotion(ivec2 texel, float depth, mat4 view, mat4 projection)
{
    vec2 ndc = (vec2(texel) + 0.5) * params.imageSize.zw * 2.0 - 1.0;
    bool bSky = depth >= 1.0;
    vec4 world = params.inverseViewProjection * vec4(ndc, bSky ? 1.0 : depth, 1.0);
    if (abs(world.w) <= 1.0e-12)
    {
        return vec2(0.0);
    }
    vec3 position = world.xyz / world.w;
    vec4 target = bSky ? vec4(position - params.cameraPositionAndHistory.xyz, 0.0) : vec4(position, 1.0);
    vec4 previousClip = projection * view * target;
    if (previousClip.w <= 1.0e-6)
    {
        return vec2(0.0);
    }
    return (ndc - previousClip.xy / previousClip.w) * 0.5;
}

vec2 SanitizeMotion(vec2 motion)
{
    return dot(motion, motion) < 1.0e12 ? motion : vec2(0.0);
}

// 画素を履歴の位置へ戻す動き（currentUV - historyUV）。
vec2 PixelHistoryMotion(ivec2 texel, float depth)
{
    vec2 historyCameraMotion = SanitizeMotion(CameraMotion(texel, depth, params.historyView, params.historyProjection));
    if (depth >= 1.0)
    {
        return historyCameraMotion;
    }
    // velocity はパケットの前のカメラ・前の変換からの動き。そこからカメラの分を引くと物体自身の動きが残る。
    vec2 velocity = SanitizeMotion(texelFetch(velocityTexture, texel, 0).xy);
    vec2 packetCameraMotion = SanitizeMotion(CameraMotion(texel, depth, params.previousView, params.previousProjection));
    return SanitizeMotion(historyCameraMotion + (velocity - packetCameraMotion) * params.motion.x);
}

void main()
{
    ivec2 size = ivec2(params.imageSize.xy);
    ivec2 center = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);

    vec3 current = vec3(0.0);
    vec3 moment1 = vec3(0.0);
    vec3 moment2 = vec3(0.0);
    float closestDepth = 2.0;
    ivec2 closestTexel = center;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            ivec2 texel = clamp(center + ivec2(x, y), ivec2(0), size - 1);
            vec3 color = SanitizeColor(texelFetch(sceneColorTexture, texel, 0).rgb);
            vec3 ycocg = RgbToYCoCg(color);
            moment1 += ycocg;
            moment2 += ycocg * ycocg;
            if (x == 0 && y == 0)
            {
                current = color;
            }
            float depth = texelFetch(sceneDepthTexture, texel, 0).r;
            if (depth < closestDepth)
            {
                closestDepth = depth;
                closestTexel = texel;
            }
        }
    }

    // 不透明度は現在の画素のものを保つ。
    float currentAlpha = texelFetch(sceneColorTexture, center, 0).a;
    if (params.cameraPositionAndHistory.w < 0.5)
    {
        outColor = vec4(current, currentAlpha);
        return;
    }

    vec2 motion = PixelHistoryMotion(closestTexel, closestDepth);
    vec2 previousUV = (vec2(center) + 0.5) * params.imageSize.zw - motion;
    if (any(lessThan(previousUV, vec2(0.0))) || any(greaterThan(previousUV, vec2(1.0))))
    {
        outColor = vec4(current, currentAlpha);
        return;
    }

    vec3 history = SanitizeColor(SampleHistoryCatmullRom(previousUV) * params.blend.y);

    vec3 mean = moment1 / 9.0;
    vec3 sigma = sqrt(max(moment2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 clipped = YCoCgToRgb(ClipToBox(RgbToYCoCg(history), mean, params.blend.z * sigma));
    history = max(clipped, vec3(0.0));

    float currentLuma = dot(current, vec3(0.2126, 0.7152, 0.0722));
    float historyLuma = dot(history, vec3(0.2126, 0.7152, 0.0722));
    float currentWeight = params.blend.x / (1.0 + currentLuma);
    float historyWeight = (1.0 - params.blend.x) / (1.0 + historyLuma);
    vec3 resolved = (current * currentWeight + history * historyWeight) / max(currentWeight + historyWeight, 1.0e-6);
    outColor = vec4(SanitizeColor(resolved), currentAlpha);
}
