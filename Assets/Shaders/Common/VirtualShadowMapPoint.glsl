// 点光源の仮想シャドウマップ（VSM。--point-shadow-method=vsm）の評価。lighting.frag の点光源の影と、計算シェーダーからの影の測定・テストが
// 同じ規則で影を引くための共通の include。
//
// 受け手の面・解像度の段は印付け（vsm_mark.comp の MarkPoints）と同じ選び方: 光源からの向きの主軸の面、texel（面の軸の距離 z で 2z ÷ 段の解像度）が
// 「カメラからの距離 × 画素の大きさ × 2^bias」以下になる最も粗い段（どの段も超えれば段 0）。
// 面の深度は面の軸の向きの線形の距離 ÷ Range（[0, 1]。何も無い texel は 1）で、キューブ（Common/PointShadow.glsl）の「光源からの距離 ÷ Range」とは違う。
//
// 法線の向きへのずらしと深度の比較の余裕は PointShadow.glsl と同じ考え方で、使う段の texel（面の軸の距離での 2z ÷ 段の解像度）に比例させる:
//   ずらし = texel × (0.6 + 1.4 × (1 − NdotL))（光が斜めに当たる面ほど足す）
//   余裕   = texel × (1.0 + 受け面の深度の傾き)（受け面の傾きは、1 texel ぶん動いたときの面の軸の向きの距離の変化。キューブの「受け面を平面とみなす」の代わり）
// PCF は 16 点（Common/PoissonDisk16.glsl）で、受け手の接平面上の半径 r の円盤に取る。r はワールドの長さで
//   r = max(画素の大きさ × PCF_MIN_RADIUS_PIXELS, 受け手の段の 1 texel)
// （太陽の VSM と同じ連続な下限。段が切り替わっても縁の幅が跳ばない）。各標本は自分の位置の面・ページの表を引き、
// 割り当てのないページは粗い段へ順に逃げ、どの段にも無ければ影なし（キューブの値は使わない）とする。逃げた標本は VSM_COUNT_POINT_FALLBACK() で数える。
//
// include する側が、先にページの表・物理ページのプールの storage buffer と点光源のパラメータの uniform block（VsmPointSampleParams。
// Common/VirtualShadowMapParams.glsl）を宣言し、次のマクロを与える（この include より前に定義する）。
//   VSM_POINT_PARAMS         VsmPointSampleParams の式
//   VSM_SLICE(i)             スライスの表の i 番目（Common/VirtualShadowMapSlice.glsl。太陽の VSM と同じ表）
//   VSM_PAGE_TABLE(i)        ページの表の語（uint）
//   VSM_POOL(i)              物理ページのプールの語（uint）
// 省略できるマクロ:
//   VSM_COUNT_POINT_FALLBACK()  自分の段のページが無く、粗い段へ逃げた標本 1 つにつき 1 回呼ぶ（既定は何もしない）
//
// 入口は VsmSamplePointShadow(light, worldPos, normal, texelMeters)。可視度（1 = 光が当たる、0 = 影）を返し、標本が実際に読んだ段の
// texel の一辺（m。粗い段へ逃げた標本はその段の値。読めた標本の平均）を返す。

#ifndef VIRTUAL_SHADOW_MAP_POINT_GLSL
#define VIRTUAL_SHADOW_MAP_POINT_GLSL

#include "Common/PoissonDisk16.glsl"
#include "Common/VirtualShadowMapParams.glsl"
#include "Common/VirtualShadowMapSlice.glsl"

#ifndef VSM_COUNT_POINT_FALLBACK
#define VSM_COUNT_POINT_FALLBACK()
#endif

// 1 ページの一辺（texel）とページの表の 1 要素の印（VirtualShadowMap::PAGE_* と一致）
const uint VSMP_PAGE_RESOLUTION = 128u;
const uint VSMP_PAGE_WORDS = VSMP_PAGE_RESOLUTION * VSMP_PAGE_RESOLUTION;
const uint VSMP_PAGE_ENTRY_ALLOCATED = 1u << 31;
const uint VSMP_PAGE_INDEX_MASK = (1u << 20) - 1u;
const uint VSMP_FACE_COUNT = 6u;

// 法線の向きへのずらし（使う段の texel の倍数）。光が斜めに当たる面ほど足す
const float VSMP_NORMAL_OFFSET_TEXELS = 0.6;
const float VSMP_GRAZING_NORMAL_OFFSET_TEXELS = 1.4;
// 深度の比較の余裕のうち一定の分（使う段の texel の倍数）と、受け面の傾きの上限
const float VSMP_CONSTANT_BIAS_TEXELS = 1.0;
const float VSMP_MAX_SLOPE = 8.0;

// 光源からの向きの主軸の面（キューブの層の順 +X,-X,+Y,-Y,+Z,-Z）。同じ大きさなら X、次に Y、最後に Z を優先する
// （CPU の SelectVirtualShadowMapPointFace・vsm_mark.comp の SelectPointFace と同じ）
uint VsmPointSelectFace(vec3 direction)
{
    const vec3 magnitude = abs(direction);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        return direction.x >= 0.0 ? 0u : 1u;
    }
    if (magnitude.y >= magnitude.z)
    {
        return direction.y >= 0.0 ? 2u : 3u;
    }
    return direction.z >= 0.0 ? 4u : 5u;
}

// 灯・面・段のスライスの表の番号（CPU の VirtualShadowMapPointSliceIndex と同じ）
uint VsmPointSliceIndex(uint light, uint face, uint mip)
{
    return VSM_POINT_PARAMS.header.y + (light * VSMP_FACE_COUNT + face) * VSM_POINT_PARAMS.header.z + mip;
}

// 面のスライス slice の NDC ndc の texel の深度（軸の距離 ÷ Range）。そのページが割り当てられていなければ false。
// NDC は面の縁・角の丸めで [-1, 1] をわずかに超えうるので、収めてからページに写す
bool VsmPointFetchDepth(VsmSlice slice, vec2 ndc, out float outDepth01)
{
    outDepth01 = 1.0;
    const int pages = slice.origin.w;
    const vec2 scaled = (clamp(ndc, vec2(-1.0), vec2(1.0)) * 0.5 + 0.5) * float(pages);
    const ivec2 page = clamp(ivec2(floor(scaled)), ivec2(0), ivec2(pages - 1));
    const uint entry = VSM_PAGE_TABLE(VsmSliceEntryIndex(slice, page.x, page.y));
    if ((entry & VSMP_PAGE_ENTRY_ALLOCATED) == 0u)
    {
        return false;
    }
    const uint physical = entry & VSMP_PAGE_INDEX_MASK;
    if (physical >= VSM_POINT_PARAMS.header.w)
    {
        return false;
    }
    const vec2 local = (scaled - vec2(page)) * float(VSMP_PAGE_RESOLUTION);
    const ivec2 texel = clamp(ivec2(floor(local)), ivec2(0), ivec2(int(VSMP_PAGE_RESOLUTION) - 1));
    outDepth01 = uintBitsToFloat(VSM_POOL(physical * VSMP_PAGE_WORDS + uint(texel.y) * VSMP_PAGE_RESOLUTION + uint(texel.x)));
    return true;
}

// 点光源 light（影の灯の番号。キューブの番号と同じ）の可視度（1 = 光が当たる、0 = 影）。outTexelMeters は PCF の標本が実際に読んだ段の
// texel の一辺（m）の平均（どの標本も読めなかったときは受け手の段の値）。灯が無い・Range の外・光源の後ろ・不正な値は 1（影なし）
float VsmSamplePointShadow(uint light, vec3 worldPos, vec3 normal, out float outTexelMeters)
{
    outTexelMeters = 0.0;
    const uint mipCount = VSM_POINT_PARAMS.header.z;
    if (light >= min(VSM_POINT_PARAMS.header.x, 4u) || mipCount == 0u)
    {
        return 1.0;
    }
    const vec4 lightData = VSM_POINT_PARAMS.lights[light];
    const float range = lightData.w;
    const vec3 offset = worldPos - lightData.xyz;
    const float distanceSquared = dot(offset, offset);
    if (!(range > 0.0) || !(distanceSquared <= range * range) || !(distanceSquared > 1.0e-8))
    {
        return 1.0;
    }
    const float nearPlane = VSM_POINT_PARAMS.plane.x;
    const uint faceResolution = uint(VSM_POINT_PARAMS.plane.y + 0.5);

    // 受け手の面・軸の距離・段（vsm_mark.comp の MarkPoints と同じ式と順序）
    const uint face = VsmPointSelectFace(offset);
    const VsmSlice faceSlice = VSM_SLICE(VsmPointSliceIndex(light, face, 0u));
    const float axial = dot(faceSlice.axisZ.xyz, offset);
    if (!(axial >= nearPlane))
    {
        return 1.0;
    }
    const float cameraDistance = length(worldPos - VSM_POINT_PARAMS.cameraPosition.xyz);
    const float targetTexel = cameraDistance * VSM_POINT_PARAMS.tuning.x * VSM_POINT_PARAMS.tuning.y;
    if (!(targetTexel > 0.0))
    {
        return 1.0;
    }
    uint mip = 0u;
    for (uint candidate = mipCount; candidate-- > 0u;)
    {
        if (2.0 * axial / float(faceResolution >> candidate) <= targetTexel)
        {
            mip = candidate;
            break;
        }
    }
    const float receiverTexel = 2.0 * axial / float(faceResolution >> mip);
    outTexelMeters = receiverTexel;

    // 受け面の法線を光源の側へ向け、位置を法線の向きへずらす（受け手の段の texel に比例）
    const float normalLength = length(normal);
    const float distanceToLight = sqrt(distanceSquared);
    const vec3 surfaceToLight = -offset / distanceToLight;
    vec3 n = normalLength > 1.0e-6 ? normal / normalLength : surfaceToLight;
    float NdotL = dot(n, surfaceToLight);
    if (NdotL < 0.0)
    {
        n = -n;
        NdotL = -NdotL;
    }
    const float normalOffset = receiverTexel * (VSMP_NORMAL_OFFSET_TEXELS +
                                                VSMP_GRAZING_NORMAL_OFFSET_TEXELS * (1.0 - clamp(NdotL, 0.0, 1.0)));
    const vec3 receiverPosition = worldPos + n * normalOffset;

    // PCF の円盤（受け手の接平面）。半径の下限はワールドで連続な量: 画素の大きさ × 係数と受け手の段の 1 texel の大きいほう
    const vec3 helperAxis = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 tangent = normalize(cross(helperAxis, n));
    const vec3 bitangent = cross(n, tangent);
    const float radius = max(cameraDistance * VSM_POINT_PARAMS.tuning.x * VSM_POINT_PARAMS.tuning.z, receiverTexel);

    float lit = 0.0;
    float readTexelSum = 0.0;
    float readCount = 0.0;
    for (int index = 0; index < 16; ++index)
    {
        const vec2 diskOffset = POISSON_DISK[index] * radius;
        const vec3 tapOffset = receiverPosition + tangent * diskOffset.x + bitangent * diskOffset.y - lightData.xyz;

        // 標本は自分の位置の面を引く（面の縁をまたぐ円盤は隣の面のページを読む）。Range の外・近い平面の手前は影なし
        float visible = 1.0;
        bool bEscaped = false;
        const uint tapFace = VsmPointSelectFace(tapOffset);
        const VsmSlice tapSlice = VSM_SLICE(VsmPointSliceIndex(light, tapFace, mip));
        const float tapAxial = dot(tapSlice.axisZ.xyz, tapOffset);
        if (tapAxial >= nearPlane && tapAxial <= range)
        {
            const vec2 ndc = vec2(dot(tapSlice.axisX.xyz, tapOffset), dot(tapSlice.axisY.xyz, tapOffset)) / tapAxial;
            // 受け面の深度の傾き: 1 texel（ワールドで 2z ÷ 解像度）ぶん面の接線方向へ動いたときの面の軸の距離の変化の比
            const float planeDenominator = max(abs(dot(n, tapOffset)) / tapAxial, 1.0 / VSMP_MAX_SLOPE);
            const float slope = min((abs(dot(n, tapSlice.axisX.xyz)) + abs(dot(n, tapSlice.axisY.xyz))) / planeDenominator, VSMP_MAX_SLOPE);

            // 自分の段から粗い段へ順に引く。どの段にも無ければ影なし
            for (uint candidate = mip; candidate < mipCount; ++candidate)
            {
                float stored = 1.0;
                if (VsmPointFetchDepth(VSM_SLICE(VsmPointSliceIndex(light, tapFace, candidate)), ndc, stored))
                {
                    const float candidateTexel = 2.0 * tapAxial / float(faceResolution >> candidate);
                    const float bias = (VSMP_CONSTANT_BIAS_TEXELS + slope) * candidateTexel;
                    visible = (tapAxial - bias > stored * range) ? 0.0 : 1.0;
                    readTexelSum += candidateTexel;
                    readCount += 1.0;
                    break;
                }
                bEscaped = true;
            }
        }
        if (bEscaped)
        {
            VSM_COUNT_POINT_FALLBACK();
        }
        lit += visible;
    }
    if (readCount > 0.0)
    {
        outTexelMeters = readTexelSum / readCount;
    }
    return lit / 16.0;
}

#endif // VIRTUAL_SHADOW_MAP_POINT_GLSL
