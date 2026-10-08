// 点光源の仮想シャドウマップ（VSM）の面と解像度の段の CPU の計算を確かめる。
//
// 契約: (1) 受け手の面は光源からの向きの主軸で、PointShadowFaceMatrices の面と一致する（その面の行列で写すと NDC が面の範囲に入り、
// 面の NDC は (sc/軸の距離, tc/軸の距離)、軸の距離は行列の w と同じ）。(2) どの向きもどれかの面に入る。(3) 段の texel（軸の距離 z で 2z ÷ 段の一辺）は
// 画素の大きさ p(d)·2^b 以下の最も粗い段（段 0 より細かくは選ばない）で、カメラまでの距離について単調。(4) ページの座標は面の範囲に収まる
// （面の縁・角の向きを含む）。(5) スライスの表は灯 × 6 面 × 段の順で、行が面の座標へ写す。
#include "Rendering/VirtualShadowMapCasters.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPointLights.h"
#include "Math/MatrixUtils.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    int GFailureCount = 0;

    void Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::printf("失敗: %s\n", message);
            ++GFailureCount;
        }
    }

    bool IsNear(float lhs, float rhs, float tolerance)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    // 再現できる擬似乱数（0 以上 1 未満）
    class DeterministicRandom
    {
    public:
        float Next()
        {
            m_State = m_State * 1664525u + 1013904223u;
            return static_cast<float>(m_State >> 8) / static_cast<float>(1u << 24);
        }

        float Range(float minimum, float maximum) { return minimum + (maximum - minimum) * Next(); }

    private:
        uint32_t m_State = 777u;
    };

    constexpr float kFovYDegrees = 60.0f;
    constexpr float kScreenHeight = 720.0f;

    const Math::Vector3 kLightPosition(4.0f, 1.0f, -2.0f);
    constexpr float kLightRange = 10.0f;

    // 1 灯だけのスナップショットから、点光源のスライスの並びを作る（太陽の 10 段の後ろ）
    VirtualShadowMapPointLights MakeLights(uint32_t lightCount, uint32_t firstSlice)
    {
        PointShadowSnapshot snapshot;
        for (uint32_t index = 0; index < lightCount; ++index)
        {
            PointShadowLightSnapshot& light = snapshot.Lights[index];
            light.LightId = 100u + index;
            light.LightIndex = index;
            light.Position = Math::Vector3(kLightPosition.x + 3.0f * static_cast<float>(index), kLightPosition.y, kLightPosition.z);
            light.Range = kLightRange;
            light.Faces = BuildPointShadowFaceMatrices(light.Position, PointShadowNearPlane, light.Range);
        }
        snapshot.LightCount = lightCount;
        return BuildVirtualShadowMapPointLights(snapshot, VirtualShadowMapPointSettings{}, firstSlice);
    }

    // VulkanDevice::AdjustProjectionForClipSpace(projection, false) と同じ補正（Z の反転、Y は反転しない）
    Math::Matrix4x4 MakeVulkanFaceClipMatrix(const PointShadowFaceMatrices& faces, uint32_t faceIndex)
    {
        const Math::Matrix4x4 vulkanProjection = faces.Projection * Math::MatrixUtils::CreateScale(Math::Vector3(1.0f, 1.0f, -1.0f));
        return vulkanProjection * faces.Views[faceIndex];
    }

    Math::Vector3 RandomDirection(DeterministicRandom& random)
    {
        for (;;)
        {
            const Math::Vector3 candidate(random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f));
            const float length = std::sqrt(candidate.x * candidate.x + candidate.y * candidate.y + candidate.z * candidate.z);
            if (length > 0.1f && length <= 1.0f)
            {
                return Math::Vector3(candidate.x / length, candidate.y / length, candidate.z / length);
            }
        }
    }

    Math::Vector3 PointAlong(const Math::Vector3& direction, float distance)
    {
        return Math::Vector3(kLightPosition.x + direction.x * distance,
                             kLightPosition.y + direction.y * distance,
                             kLightPosition.z + direction.z * distance);
    }

    // 光源からの向きの成分の絶対値が最大の軸を、面の番号に直す（テストの側の独立した書き方）
    uint32_t ExpectedFace(const Math::Vector3& direction)
    {
        const float absolute[3] = {std::fabs(direction.x), std::fabs(direction.y), std::fabs(direction.z)};
        const float signedValue[3] = {direction.x, direction.y, direction.z};
        uint32_t axis = 0u;
        for (uint32_t candidate = 1u; candidate < 3u; ++candidate)
        {
            if (absolute[candidate] > absolute[axis])
            {
                axis = candidate;
            }
        }
        return axis * 2u + (signedValue[axis] >= 0.0f ? 0u : 1u);
    }

    // (1)(2) 面の選び方が PointShadowFaceMatrices の面と一致し、どの向きもどれかの面に入る
    void TestFaceSelectionMatchesFaceMatrices()
    {
        const VirtualShadowMapPointLights lights = MakeLights(1u, 10u);
        const PointShadowFaceMatrices faces = BuildPointShadowFaceMatrices(kLightPosition, PointShadowNearPlane, kLightRange);
        DeterministicRandom random;
        char message[256];
        for (uint32_t sample = 0; sample < 4000u; ++sample)
        {
            const Math::Vector3 direction = RandomDirection(random);
            const float distance = random.Range(0.3f, 9.9f);
            const Math::Vector3 receiver = PointAlong(direction, distance);
            VirtualShadowMapPointReceiver located;
            const bool bFound = LocateVirtualShadowMapPointReceiver(lights, 0u, receiver, 3.0f, kFovYDegrees, kScreenHeight, located);
            std::snprintf(message, sizeof(message), "向き(%.3f, %.3f, %.3f)・距離%.2f がどの面にも入らない", direction.x, direction.y, direction.z, distance);
            Check(bFound, message);
            if (!bFound)
            {
                continue;
            }
            std::snprintf(message, sizeof(message), "向き(%.3f, %.3f, %.3f)の面が%uで、主軸の面%uと違う", direction.x, direction.y, direction.z,
                          located.Face, ExpectedFace(direction));
            Check(located.Face == ExpectedFace(direction), message);

            // 選んだ面の行列で写すと、面の範囲に入り、NDC と軸の距離が一致する
            const Math::Vector4 clip = Math::MatrixUtils::TransformPoint(MakeVulkanFaceClipMatrix(faces, located.Face), receiver);
            std::snprintf(message, sizeof(message), "面%uの行列で写すと範囲の外（w=%.4f, ndc=%.4f, %.4f）", located.Face, clip.w,
                          clip.w != 0.0f ? clip.x / clip.w : 0.0f, clip.w != 0.0f ? clip.y / clip.w : 0.0f);
            Check(clip.w > 0.0f && std::fabs(clip.x / clip.w) <= 1.0f + 1.0e-4f && std::fabs(clip.y / clip.w) <= 1.0f + 1.0e-4f, message);
            if (clip.w > 0.0f)
            {
                std::snprintf(message, sizeof(message), "面%uの NDC が(%.4f, %.4f)で、行列の(%.4f, %.4f)と違う", located.Face, located.NdcX,
                              located.NdcY, clip.x / clip.w, clip.y / clip.w);
                Check(IsNear(located.NdcX, clip.x / clip.w, 2.0e-4f) && IsNear(located.NdcY, clip.y / clip.w, 2.0e-4f), message);
                std::snprintf(message, sizeof(message), "面%uの軸の距離が%.4fで、行列の w=%.4f と違う", located.Face, located.AxialDistance, clip.w);
                Check(IsNear(located.AxialDistance, clip.w, 1.0e-3f), message);
            }
            std::snprintf(message, sizeof(message), "深度%.5fが 軸の距離 ÷ Range = %.5f と違う", located.Depth, located.AxialDistance / kLightRange);
            Check(located.Depth >= 0.0f && located.Depth <= 1.0f && IsNear(located.Depth, located.AxialDistance / kLightRange, 1.0e-6f), message);
        }

        // 範囲の外・光源と重なる位置は面に入らない
        VirtualShadowMapPointReceiver located;
        Check(!LocateVirtualShadowMapPointReceiver(lights, 0u, PointAlong(Math::Vector3(1.0f, 0.0f, 0.0f), kLightRange + 0.5f), 3.0f,
                                                   kFovYDegrees, kScreenHeight, located),
              "Range の外の受け手が面に入った");
        Check(!LocateVirtualShadowMapPointReceiver(lights, 0u, kLightPosition, 3.0f, kFovYDegrees, kScreenHeight, located),
              "光源と重なる受け手が面に入った");
        Check(!LocateVirtualShadowMapPointReceiver(lights, 1u, PointAlong(Math::Vector3(1.0f, 0.0f, 0.0f), 1.0f), 3.0f, kFovYDegrees,
                                                   kScreenHeight, located),
              "灯の数を超える番号が面に入った");
    }

    // (3) 段の texel は画素の大きさ × 2^b 以下の最も粗い段で、距離について単調
    void TestMipSelection()
    {
        const VirtualShadowMapPointSettings settings;
        char message[256];
        const float biasScale = std::exp2(settings.BiasLevels);

        for (float axial : {0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 9.0f})
        {
            int32_t previousMip = -1;
            for (float cameraDistance = 0.2f; cameraDistance < 60.0f; cameraDistance *= 1.07f)
            {
                const int32_t mip = SelectVirtualShadowMapPointMip(settings, axial, cameraDistance, kFovYDegrees, kScreenHeight);
                std::snprintf(message, sizeof(message), "軸の距離%.2f・カメラ%.2f で段が選ばれない", axial, cameraDistance);
                Check(mip >= 0 && mip < static_cast<int32_t>(settings.MipCount), message);
                if (mip < 0)
                {
                    continue;
                }
                const float target = VirtualShadowMapScreenPixelMeters(cameraDistance, kFovYDegrees, kScreenHeight) * biasScale;
                const float texel = VirtualShadowMapPointTexelMeters(settings, static_cast<uint32_t>(mip), axial);
                if (mip > 0)
                {
                    std::snprintf(message, sizeof(message), "軸の距離%.2f・カメラ%.2f の段%dの texel %.6f が目標 %.6f を超える", axial, cameraDistance, mip, texel, target);
                    Check(texel <= target, message);
                }
                if (mip + 1 < static_cast<int32_t>(settings.MipCount))
                {
                    const float coarser = VirtualShadowMapPointTexelMeters(settings, static_cast<uint32_t>(mip) + 1u, axial);
                    std::snprintf(message, sizeof(message), "軸の距離%.2f・カメラ%.2f で段%dより粗い段の texel %.6f が目標 %.6f 以下（最も粗い段でない）", axial,
                                  cameraDistance, mip, coarser, target);
                    Check(coarser > target, message);
                }
                std::snprintf(message, sizeof(message), "軸の距離%.2f でカメラ%.2f の段%dが手前の段%dより細かい（距離について単調でない）", axial, cameraDistance, mip, previousMip);
                Check(mip >= previousMip, message);
                previousMip = mip;
            }
        }

        // 軸の距離が遠いほど texel が粗くなるので、同じカメラの距離では段は細かくなる方へ単調
        for (float cameraDistance : {1.0f, 4.0f, 20.0f})
        {
            int32_t previousMip = static_cast<int32_t>(settings.MipCount);
            for (float axial = 0.06f; axial < 10.0f; axial *= 1.1f)
            {
                const int32_t mip = SelectVirtualShadowMapPointMip(settings, axial, cameraDistance, kFovYDegrees, kScreenHeight);
                std::snprintf(message, sizeof(message), "カメラ%.1f で軸の距離%.3f の段%dが手前の段%dより粗い", cameraDistance, axial, mip, previousMip);
                Check(mip <= previousMip, message);
                previousMip = mip;
            }
        }

        // 起動画面の電球（地面まで約 2 m・カメラ 4 m）の段 0 は約 1 mm、段 1 は約 2 mm
        Check(IsNear(VirtualShadowMapPointTexelMeters(settings, 0u, 2.0f), 2.0f * 2.0f / 4096.0f, 1.0e-7f), "段 0 の texel が 2z / 4096 と違う");
        Check(VirtualShadowMapPointPagesPerAxis(settings, 0u) == 32u && VirtualShadowMapPointPagesPerAxis(settings, 5u) == 1u,
              "1 段の一辺のページの数が 32 から 1 でない");

        // 値が不正なときは段を選ばない
        Check(SelectVirtualShadowMapPointMip(settings, -1.0f, 3.0f, kFovYDegrees, kScreenHeight) == -1, "負の軸の距離で段が選ばれた");
        Check(SelectVirtualShadowMapPointMip(settings, 1.0f, 0.0f, kFovYDegrees, kScreenHeight) == -1, "カメラの距離 0 で段が選ばれた");
        Check(SelectVirtualShadowMapPointMip(settings, 1.0f, 3.0f, 0.0f, kScreenHeight) == -1, "画角 0 で段が選ばれた");
        VirtualShadowMapPointSettings invalid;
        invalid.MipCount = 7u;
        Check(!IsValidVirtualShadowMapPointSettings(invalid), "最も粗い段がページの一辺より小さい設定が使える値になった");
        invalid = VirtualShadowMapPointSettings{};
        invalid.FaceResolution = 3000u;
        Check(!IsValidVirtualShadowMapPointSettings(invalid), "2 の冪でない解像度が使える値になった");
        Check(IsValidVirtualShadowMapPointSettings(VirtualShadowMapPointSettings{}), "既定の設定が使える値でない");
    }

    // (4) ページの座標は面の範囲に収まる（面の縁・角の向きを含む）
    void TestPageCoordinatesStayInsideFace()
    {
        const VirtualShadowMapPointLights lights = MakeLights(1u, 10u);
        char message[256];

        // 面の縁（2 成分が等しい）・角（3 成分が等しい）・軸の向きの組。符号の全組み合わせ
        const float values[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        uint32_t checkedDirections = 0u;
        for (float x : values)
        {
            for (float y : values)
            {
                for (float z : values)
                {
                    if (x == 0.0f && y == 0.0f && z == 0.0f)
                    {
                        continue;
                    }
                    const float length = std::sqrt(x * x + y * y + z * z);
                    const Math::Vector3 direction(x / length, y / length, z / length);
                    for (float distance : {0.4f, 2.0f, 9.5f})
                    {
                        for (float cameraDistance : {0.5f, 3.0f, 40.0f})
                        {
                            VirtualShadowMapPointReceiver located;
                            const bool bFound = LocateVirtualShadowMapPointReceiver(lights, 0u, PointAlong(direction, distance), cameraDistance,
                                                                                     kFovYDegrees, kScreenHeight, located);
                            std::snprintf(message, sizeof(message), "縁・角の向き(%.1f, %.1f, %.1f)・距離%.1f が面に入らない", x, y, z, distance);
                            Check(bFound, message);
                            if (!bFound)
                            {
                                continue;
                            }
                            ++checkedDirections;
                            const uint32_t pagesPerAxis = VirtualShadowMapPointPagesPerAxis(lights.Settings, located.Mip);
                            std::snprintf(message, sizeof(message), "向き(%.1f, %.1f, %.1f)のページ(%u, %u)が段%uの範囲 0〜%u の外", x, y, z, located.PageX,
                                          located.PageY, located.Mip, pagesPerAxis);
                            Check(located.PageX < pagesPerAxis && located.PageY < pagesPerAxis, message);
                            Check(std::fabs(located.NdcX) <= 1.0f && std::fabs(located.NdcY) <= 1.0f, "NDC が面の範囲の外");
                            // 面の縁は隣の面の同じ向きと、軸の距離の比が同じ（NDC が ±1）
                            const uint32_t expected = ExpectedFace(direction);
                            const float absolute[3] = {std::fabs(x), std::fabs(y), std::fabs(z)};
                            const float major = std::fmax(absolute[0], std::fmax(absolute[1], absolute[2]));
                            const uint32_t axis = expected / 2u;
                            std::snprintf(message, sizeof(message), "向き(%.1f, %.1f, %.1f)の面%uの主軸の成分が最大でない", x, y, z, located.Face);
                            Check(located.Face / 2u == axis || IsNear(absolute[located.Face / 2u], major, 1.0e-6f), message);
                        }
                    }
                }
            }
        }
        Check(checkedDirections > 1000u, "縁・角の向きを十分に調べていない");

        // 面の 4 つの角（NDC が ±1）は最後のページに収まる
        for (uint32_t face = 0; face < PointShadowFaceCount; ++face)
        {
            const float signs[2] = {-1.0f, 1.0f};
            for (float sx : signs)
            {
                for (float sy : signs)
                {
                    Math::Vector3 direction(0.0f, 0.0f, 0.0f);
                    const uint32_t axis = face / 2u;
                    const float majorSign = (face % 2u == 0u) ? 1.0f : -1.0f;
                    // 面ごとに他の 2 軸（x, y, z のうち主軸以外）の符号の組み合わせを全部調べる
                    const uint32_t other0 = (axis + 1u) % 3u;
                    const uint32_t other1 = (axis + 2u) % 3u;
                    float components[3] = {0.0f, 0.0f, 0.0f};
                    components[axis] = majorSign;
                    components[other0] = sx;
                    components[other1] = sy;
                    direction = Math::Vector3(components[0], components[1], components[2]);
                    VirtualShadowMapPointReceiver located;
                    const Math::Vector3 receiver = PointAlong(Math::Vector3(direction.x * 0.5f, direction.y * 0.5f, direction.z * 0.5f), 2.0f);
                    const bool bFound = LocateVirtualShadowMapPointReceiver(lights, 0u, receiver, 3.0f, kFovYDegrees, kScreenHeight, located);
                    std::snprintf(message, sizeof(message), "面%uの角(%.0f, %.0f)の受け手が面に入らない", face, sx, sy);
                    Check(bFound, message);
                    if (bFound)
                    {
                        const uint32_t pagesPerAxis = VirtualShadowMapPointPagesPerAxis(lights.Settings, located.Mip);
                        Check(located.PageX < pagesPerAxis && located.PageY < pagesPerAxis, "面の角のページが範囲の外");
                    }
                }
            }
        }
    }

    // (5) スライスの並びと、スライスの行が面の座標へ写すこと
    void TestSliceLayout()
    {
        const VirtualShadowMapPointLights lights = MakeLights(3u, 10u);
        Check(lights.LightCount == 3u, "3 灯が並ばない");
        Check(lights.SlicesPerLight == 36u && lights.SliceCount() == 108u, "スライスの数が 3 灯 × 6 面 × 6 段 = 108 でない");
        Check(lights.FirstSlice == 10u, "先頭のスライスの番号が 10 でない");
        Check(VirtualShadowMapPointSliceIndex(lights, 0u, 0u, 0u) == 10u, "先頭のスライスの番号が違う");
        Check(VirtualShadowMapPointSliceIndex(lights, 2u, 5u, 5u) == 10u + 107u, "最後のスライスの番号が違う");
        Check(VirtualShadowMapPointSliceIndex(lights, 1u, 2u, 3u) == 10u + (1u * 6u + 2u) * 6u + 3u, "灯 1・面 2・段 3 のスライスの番号が違う");

        // 最大 4 灯 × 36 + 太陽の 10 段 = 154 ≤ 256。上限を超える並びは作らない
        Check(MakeLights(4u, 10u).LightCount == 4u, "4 灯が並ばない");
        Check(MakeLights(4u, 112u).LightCount == 4u, "上限ちょうどの並びが作られない");
        Check(MakeLights(4u, 113u).LightCount == 0u, "上限を超える並びが作られた");

        GPUVsmSlice slices[10 + 108];
        std::memset(slices, 0xCD, sizeof(slices));
        Check(BuildVirtualShadowMapPointSlices(lights, slices) == 108u, "書いたスライスの数が 108 でない");
        char message[256];
        const uint32_t pagesPerMip[6] = {32u, 16u, 8u, 4u, 2u, 1u};
        for (uint32_t light = 0; light < lights.LightCount; ++light)
        {
            for (uint32_t face = 0; face < PointShadowFaceCount; ++face)
            {
                for (uint32_t mip = 0; mip < 6u; ++mip)
                {
                    const uint32_t index = VirtualShadowMapPointSliceIndex(lights, light, face, mip);
                    const GPUVsmSlice& slice = slices[index];
                    std::snprintf(message, sizeof(message), "スライス%u（灯%u・面%u・段%u）の欄が違う", index, light, face, mip);
                    Check(slice.extra[2] == VirtualShadowMapSliceProjectionPerspective && slice.origin[3] == static_cast<int32_t>(pagesPerMip[mip]) &&
                              slice.origin[2] == static_cast<int32_t>(index * 16384u) && slice.origin[0] == 0 && slice.origin[1] == 0 &&
                              IsNear(slice.info[0], 2.0f / static_cast<float>(pagesPerMip[mip]), 1.0e-7f) &&
                              IsNear(slice.info[1], 2.0f / static_cast<float>(4096u >> mip), 1.0e-9f),
                          message);
                }
            }
        }

        // 行が面の座標（sc, tc, 軸の距離）へ写すこと。LocateVirtualShadowMapPointReceiver の NDC・軸の距離と一致する
        DeterministicRandom random;
        for (uint32_t sample = 0; sample < 1000u; ++sample)
        {
            const uint32_t light = sample % lights.LightCount;
            const Math::Vector3 direction = RandomDirection(random);
            const Math::Vector3 receiver(lights.Position[light].x + direction.x * random.Range(0.3f, 9.9f),
                                         lights.Position[light].y + direction.y * random.Range(0.3f, 9.9f),
                                         lights.Position[light].z + direction.z * random.Range(0.3f, 9.9f));
            VirtualShadowMapPointReceiver located;
            if (!LocateVirtualShadowMapPointReceiver(lights, light, receiver, 3.0f, kFovYDegrees, kScreenHeight, located))
            {
                continue;
            }
            const GPUVsmSlice& slice = slices[located.Slice];
            const auto apply = [&receiver](const float* row) { return row[0] * receiver.x + row[1] * receiver.y + row[2] * receiver.z + row[3]; };
            const float x = apply(slice.axisX);
            const float y = apply(slice.axisY);
            const float z = apply(slice.axisZ);
            std::snprintf(message, sizeof(message), "灯%u・面%u の行が写す面の座標(%.4f, %.4f, %.4f)が NDC(%.4f, %.4f)・軸の距離%.4f と違う", light, located.Face,
                          x, y, z, located.NdcX, located.NdcY, located.AxialDistance);
            Check(z > 0.0f && IsNear(z, located.AxialDistance, 2.0e-3f) && IsNear(x / z, located.NdcX, 2.0e-4f) && IsNear(y / z, located.NdcY, 2.0e-4f), message);
        }

        // 灯の数・位置・Range・LightId が変わったときだけ違うと判定する
        VirtualShadowMapPointLights changed = lights;
        Check(!VirtualShadowMapPointLightsDiffer(lights, changed), "同じ並びが違うと判定された");
        changed.Position[1].x += 0.01f;
        Check(VirtualShadowMapPointLightsDiffer(lights, changed), "灯の位置の変化を検出しない");
        changed = lights;
        changed.Range[2] = 12.0f;
        Check(VirtualShadowMapPointLightsDiffer(lights, changed), "Range の変化を検出しない");
        changed = lights;
        changed.LightCount = 2u;
        Check(VirtualShadowMapPointLightsDiffer(lights, changed), "灯の数の変化を検出しない");
        changed = lights;
        changed.LightId[0] += 1u;
        Check(VirtualShadowMapPointLightsDiffer(lights, changed), "LightId の変化を検出しない");
        // 使わない灯（LightCount の後ろ）の値は比べない
        changed = lights;
        changed.Position[3].x = 123.0f;
        Check(!VirtualShadowMapPointLightsDiffer(lights, changed), "使わない灯の値の違いを変化と判定した");
    }

    // 前フレームの並びと比べて、引き継げないスライス（灯の識別子・位置・Range・並び・設定が違う灯のスライス）だけが true になる
    void TestSliceInvalidation()
    {
        char message[256];
        const VirtualShadowMapPointLights base = MakeLights(3u, 10u);
        bool invalid[VirtualShadowMapMaxSlices];
        const auto countRange = [&](uint32_t firstSlice, uint32_t count) {
            uint32_t flagged = 0;
            for (uint32_t index = firstSlice; index < firstSlice + count; ++index)
            {
                flagged += invalid[index] ? 1u : 0u;
            }
            return flagged;
        };

        // 同じ並び: 何も無効にしない
        Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &base, invalid) == 0u, "同じ並びでスライスが無効になった");

        // 灯 1 の位置・Range・識別子が変わる: 灯 1 の 36 スライスだけ
        for (uint32_t kind = 0; kind < 3u; ++kind)
        {
            VirtualShadowMapPointLights current = base;
            if (kind == 0u)
            {
                current.Position[1].z += 0.001f;
            }
            else if (kind == 1u)
            {
                current.Range[1] += 0.5f;
            }
            else
            {
                current.LightId[1] += 100u;
            }
            std::snprintf(message, sizeof(message), "灯 1 の変化（種類 %u）で無効になるスライスが灯 1 の 36 件だけでない", kind);
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &current, invalid) == 36u && countRange(10u + 36u, 36u) == 36u, message);
        }

        // 灯が入れ替わる（識別子・位置・Range は同じで番号だけが変わる）: 識別子で対応づけて、ページを新しい番号へ移す。無効にするスライスは無い
        {
            VirtualShadowMapPointLights swapped = base;
            std::swap(swapped.LightId[0], swapped.LightId[2]);
            std::swap(swapped.Position[0], swapped.Position[2]);
            std::swap(swapped.Range[0], swapped.Range[2]);
            VirtualShadowMapPointRemap remap;
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &swapped, invalid, &remap) == 0u,
                  "位置の変わらない灯の入れ替えで、スライスが無効になった（ページを移せば描き直しは要らない）");
            Check(remap.bMoves && remap.Source[0] == 2u && remap.Source[1] == 1u && remap.Source[2] == 0u,
                  "灯 0 と灯 2 の入れ替えで、領域 0 は前の領域 2 から、領域 2 は前の領域 0 から移すはず");

            // 入れ替わったうえで片方が動く: 動いた灯の新しい番号のスライスだけが無効になる
            VirtualShadowMapPointLights swappedAndMoved = swapped;
            swappedAndMoved.Position[0].x += 0.5f;
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &swappedAndMoved, invalid, &remap) == 36u && countRange(10u, 36u) == 36u &&
                      remap.Source[0] == 2u && remap.Source[2] == 0u,
                  "入れ替わって動いた灯は、新しい番号のスライスだけが無効になり、ページは移るはず");

            // 同じ識別子が 2 灯に現れても、前フレームの灯のページは 1 つの領域にしか移さない（物理ページの二重所有を避ける）
            VirtualShadowMapPointLights duplicated = base;
            duplicated.LightId[1] = duplicated.LightId[0];
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &duplicated, invalid, &remap) == 36u && remap.Source[0] == 0u && remap.Source[1] == 1u,
                  "同じ識別子の 2 灯目は対応づけず、前フレームの領域のまま無効にするはず");
        }

        // 先頭の灯が無くなる・先頭に灯が増える: 残る灯のページを詰め直す。移った灯の前の領域は空にする
        {
            VirtualShadowMapPointLights removedFirst = base;
            removedFirst.LightCount = 2u;
            for (uint32_t light = 0; light < 2u; ++light)
            {
                removedFirst.LightId[light] = base.LightId[light + 1u];
                removedFirst.Position[light] = base.Position[light + 1u];
                removedFirst.Range[light] = base.Range[light + 1u];
            }
            VirtualShadowMapPointRemap remap;
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &removedFirst, invalid, &remap) == 0u && remap.bMoves && remap.Source[0] == 1u &&
                      remap.Source[1] == 2u && remap.Source[2] == VirtualShadowMapPointNoSource,
                  "先頭の灯が無くなったとき、残る 2 灯のページを前へ詰め、末尾の領域を空にするはず");
            // 先頭に灯が増えると、既存の灯のページは後ろの領域へ移り、新しい灯の領域は空になる
            Check(BuildVirtualShadowMapPointSliceInvalidation(&removedFirst, &base, invalid, &remap) == 0u && remap.bMoves &&
                      remap.Source[0] == VirtualShadowMapPointNoSource && remap.Source[1] == 0u && remap.Source[2] == 1u,
                  "先頭に灯が増えたとき、既存の 2 灯のページを後ろの領域へ移し、先頭の領域を空にするはず");
        }

        // 灯が減る: 無くなった灯のスライス（前フレームのもの）が無効になる。増える: 新しい灯のスライスが無効になる
        {
            VirtualShadowMapPointLights fewer = base;
            fewer.LightCount = 2u;
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &fewer, invalid) == 36u && countRange(10u + 72u, 36u) == 36u, "無くなった灯 2 の 36 スライスだけが無効になるはず");
            Check(BuildVirtualShadowMapPointSliceInvalidation(&fewer, &base, invalid) == 36u && countRange(10u + 72u, 36u) == 36u, "増えた灯 2 の 36 スライスだけが無効になるはず");
        }

        // 点光源のページが無かった（null）フレームの前後: 灯のスライスをすべて無効にする
        Check(BuildVirtualShadowMapPointSliceInvalidation(nullptr, &base, invalid) == 108u, "前フレームに点光源が無かったとき、今フレームの 108 スライスが無効になるはず");
        Check(BuildVirtualShadowMapPointSliceInvalidation(&base, nullptr, invalid) == 108u, "今フレームに点光源が無いとき、前フレームの 108 スライスが無効になるはず");
        Check(BuildVirtualShadowMapPointSliceInvalidation(nullptr, nullptr, invalid) == 0u, "どちらも無ければ何も無効にしない");

        // 先頭の番号・設定が変わる: 前後どちらのスライスも無効にする
        {
            VirtualShadowMapPointLights moved = base;
            moved.FirstSlice = 14u;
            const uint32_t flagged = BuildVirtualShadowMapPointSliceInvalidation(&base, &moved, invalid);
            Check(flagged == 108u + 4u && countRange(10u, 112u) == flagged, "先頭の番号が変わったとき、前後のスライスの和集合（112 件）が無効になるはず");
            VirtualShadowMapPointLights coarser = base;
            coarser.Settings.FaceResolution = 2048u;
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &coarser, invalid) == 108u, "面の解像度が変わったとき、108 スライスが無効になるはず");
        }

        // 境界の箱を覆う球: 単精度へ丸めた中心から最も遠い隅までを半径にするので、中心の丸め誤差が大きい場所の薄い箱も覆う
        {
            NorvesLib::Core::Container::VariableArray<VirtualShadowMap::CasterBounds> bounds;
            VirtualShadowMap::CasterBounds thin;
            thin.Min[0] = 1048576.0f; thin.Min[1] = 0.0f; thin.Min[2] = 0.5f;
            thin.Max[0] = 1048576.125f; thin.Max[1] = 0.125f; thin.Max[2] = 0.5f;
            bounds.push_back(thin);
            VirtualShadowMap::CasterBounds ordinary;
            ordinary.Min[0] = -3.3f; ordinary.Min[1] = 0.1f; ordinary.Min[2] = 7.7f;
            ordinary.Max[0] = 2.9f; ordinary.Max[1] = 4.4f; ordinary.Max[2] = 9.1f;
            bounds.push_back(ordinary);
            NorvesLib::Core::Container::VariableArray<float> spheres;
            Check(VirtualShadowMap::BuildInvalidationSpheres(bounds, 8u, spheres) && spheres.size() == 8u, "境界の球が作れない");
            for (uint32_t box = 0; box < 2u; ++box)
            {
                const VirtualShadowMap::CasterBounds& source = bounds[box];
                const float* sphere = spheres.data() + box * 4u;
                bool bContained = true;
                for (uint32_t corner = 0; corner < 8u; ++corner)
                {
                    double distanceSquared = 0.0;
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        const double point = static_cast<double>(((corner >> axis) & 1u) != 0u ? source.Max[axis] : source.Min[axis]);
                        const double delta = point - static_cast<double>(sphere[axis]);
                        distanceSquared += delta * delta;
                    }
                    bContained = bContained && distanceSquared <= static_cast<double>(sphere[3]) * static_cast<double>(sphere[3]);
                }
                std::snprintf(message, sizeof(message), "境界の箱 %u の 8 つの隅が球の内側にない", box);
                Check(bContained, message);
            }
        }

        // 面の解像度の違いなど、ページの内容に関わらない BiasLevels の違いは無効にしない
        {
            VirtualShadowMapPointLights biased = base;
            biased.Settings.BiasLevels += 1.0f;
            Check(BuildVirtualShadowMapPointSliceInvalidation(&base, &biased, invalid) == 0u, "BiasLevels の違いでスライスが無効になった");
        }
    }
} // namespace

int main()
{
    std::printf("VirtualShadowMapPointTest start\n");

    TestFaceSelectionMatchesFaceMatrices();
    TestMipSelection();
    TestPageCoordinatesStayInsideFace();
    TestSliceLayout();
    TestSliceInvalidation();

    if (GFailureCount != 0)
    {
        std::printf("VirtualShadowMapPointTest failed: %d\n", GFailureCount);
        return 1;
    }
    std::printf("VirtualShadowMapPointTest passed\n");
    return 0;
}
