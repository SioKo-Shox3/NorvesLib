#version 450

// 草・低木の株をインスタンスで描き、風で揺らす（FoliagePass）。頂点は株の根元を原点とするローカルの位置で、
// インスタンスの位置・向き・大きさでワールドへ置いてから、根元は動かず先ほど大きく風下へずらす

#include "Common/FoliageData.glsl"
#include "Common/FoliagePlacement.glsl"

// 頂点（FoliageVertex）
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inSide;
// x = しなりの重み, y = 茎から先の色への混ぜ具合, z = 株の中の遮蔽, w = 葉・枝ごとの揺れの位相
layout(location = 3) in vec4 inAttributes;
// インスタンス（FoliageInstance）: xyz = 根元の位置, w = Y 軸まわりの向き[rad]
layout(location = 4) in vec4 inInstancePlacement;
// x = 横の倍率, y = 高さの倍率, z = 色の揺らぎ, w = 揺れの位相
layout(location = 5) in vec4 inInstanceParams;
// 距離で間引くときのしきい値（0〜1）
layout(location = 6) in float inInstanceFadeThreshold;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragAlbedo;
layout(location = 3) out float fragOcclusion;
layout(location = 4) out vec4 fragCurrentClip;
layout(location = 5) out vec4 fragPreviousClip;

// 株が縮み始めてから消えるまでの、残す割合の幅（間引く範囲が 80 m なら、株は 6 m ほどかけて縮む）
const float kFadeShrinkBand = 0.08;

// 根元までの距離で決まる株の大きさの倍率（1 = そのまま、0 = 消える）。残す割合は間引く範囲で 1 から 0 へ下がり、
// しきい値の近くで株を縮める（FoliagePass が区画ごとに切る個数と同じ規則）
float FoliageFadeScale(vec3 root, float threshold)
{
    float fadeStart = foliage.frameParams.z;
    float fadeEnd = foliage.frameParams.w;
    if (fadeEnd <= fadeStart)
    {
        return 1.0;
    }
    float keep = 1.0 - clamp((length(root - foliage.cameraPosition.xyz) - fadeStart) / (fadeEnd - fadeStart), 0.0, 1.0);
    return clamp((keep - threshold) / kFadeShrinkBand, 0.0, 1.0);
}

// 時刻 t の風のずれ（ワールドの XZ）。式は Common/FoliagePlacement.glsl（影の描画 vsm_draw.vert と同じ）、値はこの束の定数
vec2 FoliageWindOffset(vec2 root, float flex, float plantHeight, float plantPhase, float partPhase, float t)
{
    return FoliageWindOffsetFor(root, flex, plantHeight, plantPhase, partPhase, t, foliage.wind.xy, foliage.windParams.xy,
                                foliage.windParams.z, foliage.windParams.w);
}

void main()
{
    vec3 root = inInstancePlacement.xyz;
    float yaw = inInstancePlacement.w;
    // 遠くの株は、株ごとのしきい値で縮めて消す（全部の頂点が根元へ寄ると三角形が潰れて描かれない）
    float fadeScale = FoliageFadeScale(root, inInstanceFadeThreshold);
    float widthScale = inInstanceParams.x * fadeScale;
    float heightScale = inInstanceParams.y * fadeScale;
    float colorVariation = inInstanceParams.z;
    float plantPhase = inInstanceParams.w;

    // Y 軸まわりの回転と、横・高さの倍率
    float c = cos(yaw);
    float s = sin(yaw);
    vec3 scale = vec3(widthScale, heightScale, widthScale);
    vec3 local = inPosition * scale;
    vec3 position = root + FoliageRotateYaw(local, c, s);
    vec3 sideLocal = inSide * scale;
    vec3 side = FoliageRotateYaw(sideLocal, c, s);
    vec3 normalLocal = inNormal / scale;
    vec3 normal = normalize(FoliageRotateYaw(normalLocal, c, s));

    // 遠くで帯・枝が画素より細くなったら、横の向きへ広げて最小の幅を保つ（広げる上限は元の幅の倍率）
    float halfWidth = length(side);
    float minPixelWidth = foliage.tipColorA.a;
    if (minPixelWidth > 0.0 && halfWidth > 1.0e-6)
    {
        float pixelMeters = foliage.cameraPosition.w * length(position - foliage.cameraPosition.xyz);
        float targetHalfWidth = 0.5 * minPixelWidth * pixelMeters;
        if (halfWidth < targetHalfWidth)
        {
            float widen = min(targetHalfWidth / halfWidth, foliage.tipColorB.a);
            position += side * (widen - 1.0);
        }
    }

    float flex = inAttributes.x;
    float partPhase = inAttributes.w;
    float plantHeight = foliage.frameParams.y * heightScale;
    float heightAboveRoot = max(local.y, 0.0);
    vec2 currentOffset = FoliageWindOffset(root.xz, flex, plantHeight, plantPhase, partPhase, foliage.wind.z);
    vec2 previousOffset = FoliageWindOffset(root.xz, flex, plantHeight, plantPhase, partPhase, foliage.wind.w);
    vec3 worldPos = ApplyFoliageWind(position, currentOffset, heightAboveRoot);
    vec3 previousWorldPos = ApplyFoliageWind(position, previousOffset, heightAboveRoot);

    // 傾いた分だけ法線も風下へ倒す（先ほど大きい）
    vec2 tilt = currentOffset / max(plantHeight, 0.05);
    fragNormal = normalize(normal + vec3(tilt.x, 0.0, tilt.y) * normal.y);
    fragWorldPos = worldPos;

    // 色: 茎（根元）から先の色へ。先の色は株ごとに 2 色の間で揺らぎ、明るさも株ごとに少し変える
    vec3 tipColor = mix(foliage.tipColorA.rgb, foliage.tipColorB.rgb, colorVariation);
    vec3 albedo = mix(foliage.stemColor.rgb, tipColor, inAttributes.y);
    albedo *= 0.88 + 0.24 * fract(plantPhase * 7.31 + colorVariation * 3.17);
    fragAlbedo = albedo;
    fragOcclusion = inAttributes.z;

    fragCurrentClip = foliage.projection * foliage.view * vec4(worldPos, 1.0);
    fragPreviousClip = foliage.previousProjection * foliage.previousView * vec4(previousWorldPos, 1.0);
    gl_Position = fragCurrentClip;
}
