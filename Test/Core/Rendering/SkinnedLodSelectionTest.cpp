// スキンの視錐台・CSM の選別と太陽の影の設定の無害化を CPU で検証する。
#include "Rendering/SkinnedLodSelection.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/CascadedShadowLightMatrices.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/SkySunLight.h"
#include "Rendering/SunShadowParameters.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;

namespace
{
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    bool NearlyEqual(float actual, float expected)
    {
        return std::abs(actual - expected) <= 1.0e-4f;
    }

    CameraProxy MakeCamera()
    {
        CameraProxy camera;
        camera.PositionX = 0.0f;
        camera.PositionY = 0.0f;
        camera.PositionZ = 0.0f;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = 1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.FieldOfView = 90.0f;
        camera.AspectRatio = 1.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 200.0f;
        camera.Projection = ProjectionType::Perspective;
        return camera;
    }

    DrawCommand MakeSkinnedDraw(const Math::Vector3& center)
    {
        DrawCommand command;
        command.Draw.PayloadKind = DrawPayloadKind::Skinned;
        command.Draw.WorldMatrix = Math::Matrix4x4::Identity;
        command.Skinned.bHasLocalBounds = true;
        command.Skinned.LocalBounds = Math::AABB::FromCenterExtents(
            center, Math::Vector3(0.5f, 0.5f, 0.5f));
        return command;
    }

    void TestViewSelection()
    {
        const auto constants = CameraViewConstants::BuildForDevice(MakeCamera(), 1.0f, nullptr);
        // 既存の CPU 選別と同じ、ZeroToOne のカリング用行列を使う。
        const auto frustum = MakeSkinnedViewFrustum(constants.CullingViewProjectionMatrix);
        Expect(frustum.bValid, "主カメラの視錐台が有効になる");

        const DrawCommand front = MakeSkinnedDraw(Math::Vector3(0.0f, 0.0f, 5.0f));
        const DrawCommand behind = MakeSkinnedDraw(Math::Vector3(0.0f, 0.0f, -5.0f));
        const DrawCommand edge = MakeSkinnedDraw(Math::Vector3(5.0f, 0.0f, 5.0f));
        Expect(IsSkinnedDrawInView(front, frustum), "前方のスキンの描画を残す");
        Expect(!IsSkinnedDrawInView(behind, frustum), "後方のスキンの描画を省く");
        Expect(IsSkinnedDrawInView(edge, frustum), "側面をまたぐスキンの描画を残す");

        // 後方プロキシから作った複数の描画を、判定で絞った実数を数える。
        const DrawCommand proxyDraws[] = {behind, behind};
        uint32_t selectedDrawCount = 0;
        for (const auto& draw : proxyDraws)
        {
            if (IsSkinnedDrawInView(draw, frustum))
            {
                ++selectedDrawCount;
            }
        }
        Expect(selectedDrawCount == 0u, "後方プロキシの描画の並びを絞ると描画数がゼロになる");

        DrawCommand unknownBounds = behind;
        unknownBounds.Skinned.bHasLocalBounds = false;
        Expect(IsSkinnedDrawInView(unknownBounds, frustum), "局所境界の無い描画を残す");
        DrawCommand mesh = behind;
        mesh.Draw.PayloadKind = DrawPayloadKind::Mesh;
        Expect(IsSkinnedDrawInView(mesh, frustum), "スキンでない描画を残す");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto invalidFrustum = MakeSkinnedViewFrustum(Math::MatrixUtils::CreateScale(nan));
        Expect(!invalidFrustum.bValid && IsSkinnedDrawInView(behind, invalidFrustum),
            "非有限の行列から作った無効な視錐台では描画を残す");

        DrawCommand transformed = front;
        transformed.Draw.WorldMatrix = Math::MatrixUtils::CreateScale(2.0f, 3.0f, 2.0f);
        transformed.Draw.WorldMatrix.SetTranslationRow(Math::Vector3(0.0f, 0.0f, -20.0f));
        Math::AABB worldBounds;
        const bool bHasWorldBounds = ComputeSkinnedWorldBounds(
            transformed.Skinned.LocalBounds, transformed.Draw.WorldMatrix, worldBounds);
        Expect(bHasWorldBounds, "平行移動と拡大後のワールド境界を計算できる");
        if (bHasWorldBounds)
        {
            const auto center = worldBounds.Center();
            const auto half = worldBounds.HalfExtents();
            Expect(NearlyEqual(center.x, 0.0f) && NearlyEqual(center.y, 0.0f) &&
                NearlyEqual(center.z, -10.0f) && NearlyEqual(half.x, 1.0f) &&
                NearlyEqual(half.y, 1.5f) && NearlyEqual(half.z, 1.0f),
                "局所中心(0,0,5)と半幅(0.5,0.5,0.5)へ平行移動と拡大が効く");
        }
        Expect(!IsSkinnedBoundsInView(transformed.Skinned.LocalBounds,
            transformed.Draw.WorldMatrix, frustum) && !IsSkinnedDrawInView(transformed, frustum),
            "局所では前方でもワールド変換で後方へ移した描画を省く");
    }

    BoundingSphere MakeCascadeSphere(const Math::Matrix4x4& inverseViewProjection, float ndcX)
    {
        const auto point = inverseViewProjection * Math::Vector4(ndcX, 0.0f, 0.5f, 1.0f);
        return BoundingSphere{point.x / point.w, point.y / point.w, point.z / point.w, 0.5f};
    }

    void TestCascadeSelection()
    {
        LightProxy sun;
        sun.LightId = SkySunLightId;
        sun.Type = LightType::Directional;
        sun.DirectionX = 0.35f;
        sun.DirectionY = -0.8f;
        sun.DirectionZ = 0.25f;
        sun.bCastShadows = true;
        sun.bVisible = true;
        sun.Intensity = 1.0f;
        Container::VariableArray<LightProxy> lights;
        lights.push_back(sun);
        const auto result = BuildCascadedShadowLightMatrices(
            &lights, MakeCamera(), MakeDefaultCascadedShadowMatrixSettings());
        Expect(result.bEnabled && result.Cascades[0].bEnabled, "斜めの太陽でカスケード0を構築できる");
        if (!result.bEnabled || !result.Cascades[0].bEnabled)
        {
            return;
        }

        const auto& cascade = result.Cascades[0];
        const auto inverseViewProjection = Math::MatrixUtils::Inverse(cascade.Projection * cascade.View);
        // NDC の X=0 は内側、X=3 は十分外側、X=1 は地図の縁の上。球の半径は各0.5 m。
        const CascadedShadowDrawBounds draws[] = {
            {MakeCascadeSphere(inverseViewProjection, 0.0f), true},
            {MakeCascadeSphere(inverseViewProjection, 3.0f), true},
            {MakeCascadeSphere(inverseViewProjection, 1.0f), true},
            {MakeCascadeSphere(inverseViewProjection, 3.0f), false}
        };
        for (uint32_t index = 0; index < 4u; ++index)
        {
            const auto& bounds = draws[index].Bounds;
            std::cout << "CSM 配置[" << index << "]: 中心=(" << bounds.CenterX << ","
                << bounds.CenterY << "," << bounds.CenterZ << "), 半径=" << bounds.Radius
                << ", 境界あり=" << draws[index].bHasBounds << "\n";
        }
        Expect(CascadedShadowCascadeMayContainCaster(cascade, draws[0].Bounds),
            "カスケード0の内側の球を残す");
        Expect(!CascadedShadowCascadeMayContainCaster(cascade, draws[1].Bounds),
            "カスケード0のXYの範囲から十分外れた球を省く");
        Expect(CascadedShadowCascadeMayContainCaster(cascade, draws[2].Bounds),
            "カスケード0の縁をまたぐ球を残す");

        Container::VariableArray<uint32_t> selectedIndices;
        SelectCascadedShadowCasterDraws(cascade, draws, 4u, selectedIndices);
        Expect(selectedIndices.size() == 3u, "内側・縁・境界不明の3描画だけを選ぶ");
        if (selectedIndices.size() == 3u)
        {
            Expect(selectedIndices[0] == 0u && selectedIndices[1] == 2u && selectedIndices[2] == 3u,
                "境界不明の描画を含めて添え字を入力順[0,2,3]で返す");
        }
        CascadedShadowCascade disabled = cascade;
        disabled.bEnabled = false;
        SelectCascadedShadowCasterDraws(disabled, draws, 4u, selectedIndices);
        Expect(selectedIndices.empty(), "無効なカスケードは既存の選択結果も空にする");
    }

    void TestSunShadowSanitize()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const SunShadowParameters inputs[] = {
            {nan, nan}, {-1.0f, -0.5f}, {0.0f, 0.0f}, {80.0f, 1.5f}, {80.0f, 0.5f}
        };
        const SunShadowParameters expected[] = {
            {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {80.0f, 1.0f}, {80.0f, 0.5f}
        };
        for (uint32_t index = 0; index < 5u; ++index)
        {
            const auto actual = SanitizeSunShadowParameters(inputs[index]);
            if (actual.MaxDistance != expected[index].MaxDistance || actual.SplitLambda != expected[index].SplitLambda)
            {
                ++g_FailureCount;
                std::cout << "失敗: 太陽の影の設定の無害化[" << index << "] 期待=("
                    << expected[index].MaxDistance << "," << expected[index].SplitLambda << "), 実際=("
                    << actual.MaxDistance << "," << actual.SplitLambda << ")\n";
            }
        }
    }
} // 名前空間

int main()
{
    TestViewSelection();
    TestCascadeSelection();
    TestSunShadowSanitize();
    if (g_FailureCount != 0)
    {
        std::cout << "SkinnedLodSelectionTest: 失敗数=" << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "SkinnedLodSelectionTest: 視錐台・CSM の選別と太陽の影の設定の無害化を確認しました\n";
    return 0;
}
