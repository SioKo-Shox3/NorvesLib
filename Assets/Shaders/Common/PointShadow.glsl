// 点光源のキューブシャドウ（ShadowMapPassのキューブ配列）を引く共通関数。
// lighting.frag と forward_transparent.frag が同じ規則で影を掛ける。
//
// キューブには光源からの線形距離を範囲で割った値（0〜1）が入っている。
// アクネ（縞）を抑えるため、受け面の位置を法線方向へずらし、PCFの各タップでは
// 受け面を平面とみなしてそのタップの方向での受け面までの距離を求めて比べる。
// ずらしはキューブの1テクセルの大きさに比例させ、接地部が浮かない（ピーターパン）程度に保つ。

#ifndef NORVES_POINT_SHADOW_GLSL
#define NORVES_POINT_SHADOW_GLSL

// PCFの格子（4×4）の間隔（テクセル）
const float POINT_SHADOW_PCF_SPACING_TEXELS = 1.0;
// 比べるときの定数のずらし（テクセル）
const float POINT_SHADOW_CONSTANT_BIAS_TEXELS = 0.35;
// 法線方向のずらし（テクセル）。光が斜めに当たる面ほど足す。
const float POINT_SHADOW_NORMAL_OFFSET_TEXELS = 0.6;
const float POINT_SHADOW_GRAZING_NORMAL_OFFSET_TEXELS = 1.4;

/**
 * 点光源の影の可視（1=照らされる、0=遮られる）を返す。
 * cubeIndex はキューブ配列内のキューブの番号（光源バッファの attenuation.w - 1）。
 * normal は受け面の法線（正規化済みでなくてよい）。
 */
float SamplePointShadow(samplerCubeArray cubes,
                        float cubeIndex,
                        vec3 lightPosition,
                        float range,
                        vec3 worldPosition,
                        vec3 normal)
{
    vec3 lightToSurface = worldPosition - lightPosition;
    float distanceToLight = length(lightToSurface);
    if (!(range > 0.0) || !(distanceToLight > 1.0e-4) || distanceToLight >= range)
    {
        return 1.0;
    }

    vec3 n = normal;
    float normalLength = length(n);
    n = normalLength > 1.0e-6 ? n / normalLength : -lightToSurface / distanceToLight;
    vec3 surfaceToLight = -lightToSurface / distanceToLight;
    float NdotL = dot(n, surfaceToLight);
    if (NdotL < 0.0)
    {
        // 光源に背を向けた面は照らされないが、法線を光源側へ向けて比べる（薄い物体の裏面の縞を防ぐ）。
        n = -n;
        NdotL = -NdotL;
    }

    // 受け面の位置でのキューブの1テクセルの大きさ（面の中心で 2d/解像度）。
    float faceResolution = max(float(textureSize(cubes, 0).x), 1.0);
    float texelWorld = 2.0 * distanceToLight / faceResolution;

    float normalOffset = texelWorld * (POINT_SHADOW_NORMAL_OFFSET_TEXELS +
                                       POINT_SHADOW_GRAZING_NORMAL_OFFSET_TEXELS *
                                           (1.0 - clamp(NdotL, 0.0, 1.0)));
    vec3 receiverPosition = worldPosition + n * normalOffset;
    vec3 lightToReceiver = receiverPosition - lightPosition;
    float receiverDistance = length(lightToReceiver);
    if (!(receiverDistance > 1.0e-4))
    {
        return 1.0;
    }
    vec3 centerDirection = lightToReceiver / receiverDistance;

    // 受け面の平面（receiverPositionを通り法線n）。タップの方向の光線がこの平面と交わる距離で比べる。
    float planeDistance = dot(lightToReceiver, n);
    float biasDistance = POINT_SHADOW_CONSTANT_BIAS_TEXELS * texelWorld;

    vec3 helperAxis = abs(centerDirection.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helperAxis, centerDirection));
    vec3 bitangent = cross(centerDirection, tangent);
    float tapSpacing = POINT_SHADOW_PCF_SPACING_TEXELS * texelWorld;

    float visibility = 0.0;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 offset = (vec2(float(x), float(y)) - 1.5) * tapSpacing;
            vec3 tapVector = lightToReceiver + tangent * offset.x + bitangent * offset.y;
            vec3 tapDirection = normalize(tapVector);
            float denominator = dot(tapDirection, n);
            float tapReceiverDistance = receiverDistance;
            if (abs(denominator) > 0.05)
            {
                tapReceiverDistance = clamp(planeDistance / denominator,
                                            receiverDistance * 0.5,
                                            receiverDistance * 2.0);
            }
            float stored01 = textureLod(cubes, vec4(tapDirection, cubeIndex), 0.0).r;
            float compare01 = (tapReceiverDistance - biasDistance) / range;
            visibility += compare01 > stored01 ? 0.0 : 1.0;
        }
    }
    return visibility / 16.0;
}

#endif
