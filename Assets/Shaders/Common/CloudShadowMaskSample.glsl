// 雲の影のマスクを束縛を持たずに参照する。
#ifndef CLOUD_SHADOW_MASK_SAMPLE_GLSL
#define CLOUD_SHADOW_MASK_SAMPLE_GLSL

// CloudShadow.h の CloudShadowMinSunHeight と同じ太陽の高さ。
const float kCloudShadowMinSunHeight = 0.02;
// マスクの中心から縁までの割合がこの値を超えたら、外の値へ滑らかに寄せる。
const float kCloudShadowEdgeFadeStart = 0.8;

// 世界の点を太陽の方向に沿って雲の底の平面へ写す。
bool CloudShadowProjectToBottomPlane(vec3 position, vec3 sunDirection, float bottomAltitude, out vec2 planeXZ)
{
    if (sunDirection.y < kCloudShadowMinSunHeight)
    {
        return false;
    }
    float distanceToBottom = max(bottomAltitude - position.y, 0.0) / sunDirection.y;
    planeXZ = position.xz + sunDirection.xz * distanceToBottom;
    return true;
}

// 雲の底の平面での切れ間の重み。gap は中心 X・Z、半径(m)、柔らかさ(m)。
float CloudSunGapWeight(vec2 planeXZ, vec4 gap)
{
    float distanceToCenter = length(planeXZ - gap.xy);
    if (gap.w > 0.0)
    {
        return 1.0 - smoothstep(gap.z, gap.z + gap.w, distanceToCenter);
    }
    return distanceToCenter <= gap.z ? 1.0 : 0.0;
}

// 世界の点から太陽へ向かう位置の透過率を引く。画像の横は X、縦は Z に対応する。
// centerAndExtent は中心 X・Z、一辺(m)、有効フラグ。呼ぶ側が有効フラグを確かめる。
// sunDirectionAndBottom は正規化済みの太陽の方向と雲の底の高度(m)。
float CloudShadowMaskLookup(sampler2D mask, vec3 worldPosition, vec4 centerAndExtent,
                            vec4 sunDirectionAndBottom, float outsideTransmittance)
{
    vec2 planeXZ;
    if (!CloudShadowProjectToBottomPlane(worldPosition, sunDirectionAndBottom.xyz,
                                         sunDirectionAndBottom.w, planeXZ))
    {
        return outsideTransmittance;
    }

    vec2 uv = (planeXZ - centerAndExtent.xy) / centerAndExtent.z + vec2(0.5);
    float edge = 2.0 * max(abs(uv.x - 0.5), abs(uv.y - 0.5));
    if (edge >= 1.0)
    {
        return outsideTransmittance;
    }

    float transmittance = clamp(textureLod(mask, uv, 0.0).r, 0.0, 1.0);
    return mix(transmittance, outsideTransmittance, smoothstep(kCloudShadowEdgeFadeStart, 1.0, edge));
}

#endif
