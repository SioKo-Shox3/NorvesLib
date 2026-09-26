// 点光源のキューブシャドウの6面の行列と、影を落とす点光源の選択を確かめる。
//
// 期待値はVulkanの仕様のキューブの面の選び方（Cube Map Face Selection）の表を手で書いたもの。
// 面fの行列（Vulkanのクリップ空間の補正後）で光源からの方向dの点を写すと、NDCのx・yが
// (sc/|ma|, tc/|ma|) になり、キューブの層fをその方向で引いたときの面内の位置と一致する。
#include "Rendering/PointShadowSnapshot.h"
#include "Math/MatrixUtils.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;
namespace CoreContainer = NorvesLib::Core::Container;

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

    // Vulkanの仕様の表: 面ごとの主軸（視線方向）と、sc・tcに対応するワールドの軸。
    struct VulkanCubeFace
    {
        const char* Name;
        Math::Vector3 MajorAxis;
        Math::Vector3 ScAxis;
        Math::Vector3 TcAxis;
    };

    const VulkanCubeFace GVulkanCubeFaces[PointShadowFaceCount] = {
        {"+X", Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, -1.0f), Math::Vector3(0.0f, -1.0f, 0.0f)},
        {"-X", Math::Vector3(-1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 1.0f), Math::Vector3(0.0f, -1.0f, 0.0f)},
        {"+Y", Math::Vector3(0.0f, 1.0f, 0.0f), Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 1.0f)},
        {"-Y", Math::Vector3(0.0f, -1.0f, 0.0f), Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 0.0f, -1.0f)},
        {"+Z", Math::Vector3(0.0f, 0.0f, 1.0f), Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f)},
        {"-Z", Math::Vector3(0.0f, 0.0f, -1.0f), Math::Vector3(-1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f)},
    };

    // VulkanDevice::AdjustProjectionForClipSpace(projection, false) と同じ補正（Zの反転、Yは反転しない）。
    Math::Matrix4x4 MakeVulkanFaceClipMatrix(const PointShadowFaceMatrices& faces, uint32_t faceIndex)
    {
        const Math::Matrix4x4 vulkanProjection =
            faces.Projection * Math::MatrixUtils::CreateScale(Math::Vector3(1.0f, 1.0f, -1.0f));
        return vulkanProjection * faces.Views[faceIndex];
    }

    struct ProjectedPoint
    {
        float NdcX = 0.0f;
        float NdcY = 0.0f;
        float Depth = 0.0f;
        float W = 0.0f;
    };

    ProjectedPoint Project(const Math::Matrix4x4& clipMatrix, const Math::Vector3& point)
    {
        const Math::Vector4 clip = Math::MatrixUtils::TransformPoint(clipMatrix, point);
        ProjectedPoint result;
        result.W = clip.w;
        if (clip.w != 0.0f)
        {
            result.NdcX = clip.x / clip.w;
            result.NdcY = clip.y / clip.w;
            result.Depth = clip.z / clip.w;
        }
        return result;
    }

    Math::Vector3 Add(const Math::Vector3& a, const Math::Vector3& b)
    {
        return Math::Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    Math::Vector3 Scale(const Math::Vector3& v, float s)
    {
        return Math::Vector3(v.x * s, v.y * s, v.z * s);
    }

    // 面ごとに、主軸の距離maと面内のずれ(sc, tc)の点が NDC (sc/ma, tc/ma) に写ることを確かめる。
    void TestFacesMatchVulkanCubeFaceSelection()
    {
        const Math::Vector3 lightPosition(1.5f, 2.0f, -3.0f);
        const float nearPlane = 0.05f;
        const float farPlane = 12.0f;
        const PointShadowFaceMatrices faces =
            BuildPointShadowFaceMatrices(lightPosition, nearPlane, farPlane);

        const float offsets[][3] = {
            // ma, sc, tc
            {2.0f, 0.0f, 0.0f},
            {2.0f, 1.0f, 0.5f},
            {3.0f, -2.1f, 1.8f},
            {4.0f, 3.6f, -3.6f},
            {0.5f, -0.25f, -0.4f},
        };

        char message[256];
        for (uint32_t faceIndex = 0; faceIndex < PointShadowFaceCount; ++faceIndex)
        {
            const VulkanCubeFace& face = GVulkanCubeFaces[faceIndex];
            const Math::Matrix4x4 clipMatrix = MakeVulkanFaceClipMatrix(faces, faceIndex);
            for (const auto& offset : offsets)
            {
                const float ma = offset[0];
                const float sc = offset[1];
                const float tc = offset[2];
                const Math::Vector3 direction = Add(Add(Scale(face.MajorAxis, ma), Scale(face.ScAxis, sc)),
                                                    Scale(face.TcAxis, tc));
                const ProjectedPoint projected = Project(clipMatrix, Add(lightPosition, direction));

                std::snprintf(message, sizeof(message),
                              "面%s（層%u）の(ma=%.2f, sc=%.2f, tc=%.2f)が光源の前にない（w=%.4f）",
                              face.Name, faceIndex, ma, sc, tc, projected.W);
                Check(projected.W > 0.0f, message);

                std::snprintf(message, sizeof(message),
                              "面%s（層%u）の(ma=%.2f, sc=%.2f, tc=%.2f)のNDCが(%.4f, %.4f)で、期待(%.4f, %.4f)と違う",
                              face.Name, faceIndex, ma, sc, tc, projected.NdcX, projected.NdcY,
                              sc / ma, tc / ma);
                Check(IsNear(projected.NdcX, sc / ma, 1.0e-4f) && IsNear(projected.NdcY, tc / ma, 1.0e-4f),
                      message);

                std::snprintf(message, sizeof(message),
                              "面%s（層%u）の深度%.4fが0から1の範囲の外", face.Name, faceIndex, projected.Depth);
                Check(projected.Depth >= 0.0f && projected.Depth <= 1.0f, message);
            }

            // near面・far面の上の点は深度0・1になる（Vulkanの深度範囲）。
            const ProjectedPoint nearPoint =
                Project(clipMatrix, Add(lightPosition, Scale(face.MajorAxis, nearPlane)));
            const ProjectedPoint farPoint =
                Project(clipMatrix, Add(lightPosition, Scale(face.MajorAxis, farPlane)));
            std::snprintf(message, sizeof(message),
                          "面%sのnear・farの深度が(%.5f, %.5f)で、(0, 1)と違う",
                          face.Name, nearPoint.Depth, farPoint.Depth);
            Check(IsNear(nearPoint.Depth, 0.0f, 1.0e-4f) && IsNear(farPoint.Depth, 1.0f, 1.0e-4f), message);

            // 主軸の反対側は描かない（光源の後ろ）。
            const ProjectedPoint behind =
                Project(clipMatrix, Add(lightPosition, Scale(face.MajorAxis, -2.0f)));
            std::snprintf(message, sizeof(message), "面%sの後ろの点がw>0になる", face.Name);
            Check(behind.W < 0.0f, message);
        }

        // 上方向の具体例: +Y面（真上を見る面）ではワールドの+Zが画像の下（NDCの+y）、
        // +X面ではワールドの+Yが画像の上（NDCの-y）、ワールドの+Zが画像の左（NDCの-x）。
        {
            const ProjectedPoint towardPlusZ = Project(MakeVulkanFaceClipMatrix(faces, 2u),
                                                       Add(lightPosition, Math::Vector3(0.0f, 2.0f, 1.0f)));
            Check(towardPlusZ.NdcY > 0.4f, "+Y面でワールドの+Z側が画像の下にない");
            const ProjectedPoint upward = Project(MakeVulkanFaceClipMatrix(faces, 0u),
                                                  Add(lightPosition, Math::Vector3(2.0f, 1.0f, 1.0f)));
            Check(upward.NdcY < -0.4f, "+X面でワールドの+Y側が画像の上にない");
            Check(upward.NdcX < -0.4f, "+X面でワールドの+Z側が画像の左にない");
        }
    }

    // どの方向も、主軸（成分の絶対値が最大の軸）の面の視錐台にちょうど入る（6面で隙間がない）。
    void TestEveryDirectionFallsInItsMajorAxisFace()
    {
        const Math::Vector3 lightPosition(-4.0f, 0.5f, 7.0f);
        const PointShadowFaceMatrices faces = BuildPointShadowFaceMatrices(lightPosition, 0.05f, 20.0f);
        const float samples[] = {-1.0f, -0.73f, -0.31f, 0.0f, 0.29f, 0.64f, 1.0f};
        char message[256];
        for (float x : samples)
        {
            for (float y : samples)
            {
                for (float z : samples)
                {
                    const float ax = std::fabs(x);
                    const float ay = std::fabs(y);
                    const float az = std::fabs(z);
                    if (ax < 0.01f && ay < 0.01f && az < 0.01f)
                    {
                        continue;
                    }
                    uint32_t faceIndex = 0;
                    if (ax >= ay && ax >= az)
                    {
                        faceIndex = x >= 0.0f ? 0u : 1u;
                    }
                    else if (ay >= az)
                    {
                        faceIndex = y >= 0.0f ? 2u : 3u;
                    }
                    else
                    {
                        faceIndex = z >= 0.0f ? 4u : 5u;
                    }
                    const Math::Vector3 direction = Scale(Math::Vector3(x, y, z), 5.0f);
                    const ProjectedPoint projected =
                        Project(MakeVulkanFaceClipMatrix(faces, faceIndex), Add(lightPosition, direction));
                    std::snprintf(message, sizeof(message),
                                  "方向(%.2f, %.2f, %.2f)が主軸の面%sの外（NDC %.4f, %.4f, w=%.4f）",
                                  x, y, z, GVulkanCubeFaces[faceIndex].Name,
                                  projected.NdcX, projected.NdcY, projected.W);
                    Check(projected.W > 0.0f &&
                              std::fabs(projected.NdcX) <= 1.0f + 1.0e-4f &&
                              std::fabs(projected.NdcY) <= 1.0f + 1.0e-4f,
                          message);
                }
            }
        }
    }

    LightProxy MakeShadowPointLight(uint64_t lightId, float x, float y, float z)
    {
        LightProxy light;
        light.LightId = lightId;
        light.Type = LightType::Point;
        light.PositionX = x;
        light.PositionY = y;
        light.PositionZ = z;
        light.Intensity = 100.0f;
        light.Range = 8.0f;
        light.bCastShadows = true;
        light.bVisible = true;
        return light;
    }

    // 影を落とす・表示中の点光源だけを、カメラから近い順に最大4灯選ぶ。
    void TestSnapshotSelectsNearestFourShadowPointLights()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeShadowPointLight(10u, 9.0f, 0.0f, 0.0f));   // 距離9
        LightProxy noShadow = MakeShadowPointLight(11u, 0.5f, 0.0f, 0.0f);
        noShadow.bCastShadows = false;                                     // 影を落とさない
        lights.push_back(noShadow);
        lights.push_back(MakeShadowPointLight(12u, 0.0f, 3.0f, 0.0f));   // 距離3
        LightProxy hidden = MakeShadowPointLight(13u, 0.0f, 0.0f, 1.0f);
        hidden.bVisible = false;                                           // 非表示
        lights.push_back(hidden);
        LightProxy spot = MakeShadowPointLight(14u, 0.0f, 0.0f, 1.5f);
        spot.Type = LightType::Spot;                                       // 点光源でない
        lights.push_back(spot);
        lights.push_back(MakeShadowPointLight(15u, 0.0f, 0.0f, -5.0f));  // 距離5
        lights.push_back(MakeShadowPointLight(16u, 0.0f, -7.0f, 0.0f));  // 距離7
        lights.push_back(MakeShadowPointLight(17u, 3.0f, 0.0f, 0.0f));   // 距離3（12と同じ、後に現れる）
        LightProxy dark = MakeShadowPointLight(18u, 0.2f, 0.0f, 0.0f);
        dark.Intensity = 0.0f;                                             // 光らない
        lights.push_back(dark);
        LightProxy tiny = MakeShadowPointLight(19u, 0.3f, 0.0f, 0.0f);
        tiny.Range = 0.01f;                                                // 範囲がnear面より小さい
        lights.push_back(tiny);

        CameraProxy camera;
        camera.PositionX = 0.0f;
        camera.PositionY = 0.0f;
        camera.PositionZ = 0.0f;

        PointShadowSnapshot snapshot;
        BuildPointShadowSnapshot(lights, &camera, snapshot);

        Check(snapshot.LightCount == 4u, "選んだ灯の数が4でない");
        const uint64_t expectedIds[4] = {12u, 17u, 15u, 16u};
        const uint32_t expectedIndices[4] = {2u, 7u, 5u, 6u};
        char message[256];
        for (uint32_t slot = 0; slot < 4u && slot < snapshot.LightCount; ++slot)
        {
            const PointShadowLightSnapshot& light = snapshot.Lights[slot];
            std::snprintf(message, sizeof(message),
                          "キューブ%uの灯がLightId=%llu・番号%uで、期待はLightId=%llu・番号%u",
                          slot,
                          static_cast<unsigned long long>(light.LightId), light.LightIndex,
                          static_cast<unsigned long long>(expectedIds[slot]), expectedIndices[slot]);
            Check(light.LightId == expectedIds[slot] && light.LightIndex == expectedIndices[slot], message);
            const LightProxy& proxy = lights[expectedIndices[slot]];
            Check(light.Position.x == proxy.PositionX && light.Position.y == proxy.PositionY &&
                      light.Position.z == proxy.PositionZ,
                  "選んだ灯の位置が光源と違う");
            Check(light.Range == 8.0f && light.NearPlane == PointShadowNearPlane,
                  "選んだ灯の範囲・near面が違う");

            // 面の行列は選んだ灯の位置から作られている（+X面の中心の点がNDCの原点・far=範囲で深度1）。
            const ProjectedPoint center = Project(
                MakeVulkanFaceClipMatrix(light.Faces, 0u),
                Add(light.Position, Math::Vector3(light.Range, 0.0f, 0.0f)));
            Check(IsNear(center.NdcX, 0.0f, 1.0e-4f) && IsNear(center.NdcY, 0.0f, 1.0e-4f) &&
                      IsNear(center.Depth, 1.0f, 1.0e-4f),
                  "選んだ灯の+X面の行列が灯の位置・範囲から作られていない");
        }

        // カメラが無いときは光源表の並び順で選ぶ。
        PointShadowSnapshot unordered;
        BuildPointShadowSnapshot(lights, nullptr, unordered);
        const uint64_t expectedUnorderedIds[4] = {10u, 12u, 15u, 16u};
        Check(unordered.LightCount == 4u, "カメラなしで選んだ灯の数が4でない");
        for (uint32_t slot = 0; slot < 4u && slot < unordered.LightCount; ++slot)
        {
            std::snprintf(message, sizeof(message),
                          "カメラなしのキューブ%uの灯がLightId=%llu（期待%llu）",
                          slot,
                          static_cast<unsigned long long>(unordered.Lights[slot].LightId),
                          static_cast<unsigned long long>(expectedUnorderedIds[slot]));
            Check(unordered.Lights[slot].LightId == expectedUnorderedIds[slot], message);
        }

        // 影を落とす点光源が無ければ空になり、前の選択は残らない。
        CoreContainer::VariableArray<LightProxy> withoutShadowLights;
        withoutShadowLights.push_back(noShadow);
        withoutShadowLights.push_back(spot);
        BuildPointShadowSnapshot(withoutShadowLights, &camera, snapshot);
        Check(snapshot.IsEmpty(), "影を落とす点光源が無いのに選択が空でない");
        Check(snapshot.Lights[0].LightId == 0u, "空の選択に前の灯が残っている");
    }

    // キャスターは光源の範囲の球と交わるものだけを描く（境界球が無効なら安全側で描く）。
    void TestCasterCullingByLightRangeSphere()
    {
        PointShadowLightSnapshot light;
        light.Position = Math::Vector3(10.0f, 0.0f, 0.0f);
        light.Range = 4.0f;

        BoundingSphere inside;
        inside.CenterX = 11.0f;
        inside.Radius = 0.5f;
        Check(PointShadowCasterIntersectsLight(inside, light), "範囲の中のキャスターを除いた");

        BoundingSphere touching;
        touching.CenterX = 15.0f;
        touching.Radius = 1.0f;
        Check(PointShadowCasterIntersectsLight(touching, light), "範囲の球に接するキャスターを除いた");

        BoundingSphere outside;
        outside.CenterX = 10.0f;
        outside.CenterZ = 6.0f;
        outside.Radius = 1.5f;
        Check(!PointShadowCasterIntersectsLight(outside, light), "範囲の外のキャスターを除いていない");

        BoundingSphere invalid;
        invalid.CenterX = 100.0f;
        invalid.Radius = 0.0f;
        Check(PointShadowCasterIntersectsLight(invalid, light), "境界球が無効なキャスターを除いた");
    }
} // namespace

int main()
{
    std::printf("PointShadowFaceMatricesTest start\n");

    TestFacesMatchVulkanCubeFaceSelection();
    TestEveryDirectionFallsInItsMajorAxisFace();
    TestSnapshotSelectsNearestFourShadowPointLights();
    TestCasterCullingByLightRangeSphere();

    if (GFailureCount != 0)
    {
        std::printf("PointShadowFaceMatricesTest failed: %d\n", GFailureCount);
        return 1;
    }
    std::printf("PointShadowFaceMatricesTest passed\n");
    return 0;
}
