// カメラと注視点の間の切り抜き、および近接のフェードの可視度を確かめる。
#include "Rendering/SceneProxy.h"
#include "Rendering/ViewClearance.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

// 独自の既定コンストラクタを持つ Vector3 も、カメラの値としてコピーできる。
static_assert(std::is_trivially_copyable_v<Math::Vector3>);
static_assert(std::is_standard_layout_v<Math::Vector3>);
static_assert(std::is_trivially_copyable_v<ViewClearanceParameters>);
static_assert(std::is_trivially_copyable_v<CameraProxy>);

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

    ViewClearanceParameters MakeParameters()
    {
        ViewClearanceParameters parameters = MakeDefaultViewClearanceParameters();
        parameters.bEnabled = true;
        parameters.CameraPosition = Math::Vector3(1.0f, 2.0f, 3.0f);
        parameters.TargetPosition = Math::Vector3(1.0f, 2.0f, 13.0f);
        return parameters;
    }

    void TestClearance()
    {
        const ViewClearanceParameters parameters = MakeParameters();
        Check(ComputeViewClearanceVisibility(parameters, Math::Vector3(1.0f, 2.0f, 8.0f)) == 0.0f,
              "線分の中央の軸上は切り抜かれる");
        Check(ComputeViewClearanceVisibility(parameters, Math::Vector3(1.8f, 2.0f, 8.0f)) == 1.0f,
              "線分の中央で縁の外側は切り抜かれない");

        // 中央の半径は 0.45 m、縁の外端は 0.675 m。
        float previous = 0.0f;
        float middle = 0.0f;
        bool bMonotonic = true;
        bool bContinuous = true;
        for (int index = 0; index <= 100; ++index)
        {
            const float distance = 0.45f + 0.225f * static_cast<float>(index) / 100.0f;
            const float visibility = ComputeViewClearanceVisibility(
                parameters, Math::Vector3(1.0f + distance, 2.0f, 8.0f));
            if (index == 0)
            {
                Check(std::fabs(visibility) < 1.0e-5f, "縁の内端の可視度は 0");
            }
            else
            {
                bMonotonic = bMonotonic && visibility >= previous;
                bContinuous = bContinuous && visibility - previous < 0.02f;
            }
            if (index == 50)
            {
                middle = visibility;
            }
            previous = visibility;
        }
        Check(bMonotonic, "縁の可視度は単調に増加する");
        Check(bContinuous, "縁の隣り合う可視度の差は小さい");
        Check(std::fabs(middle - 0.5f) < 1.0e-5f && std::fabs(previous - 1.0f) < 1.0e-5f,
              "縁の中央と外端の可視度は 0.5 と 1");

        Check(ComputeViewClearanceVisibility(parameters, Math::Vector3(1.0f, 2.0f, 15.0f)) == 1.0f,
              "注視点より奥の軸上は切り抜かれない");
        Check(ComputeViewClearanceVisibility(parameters, Math::Vector3(1.0f, 2.0f, 1.0f)) == 1.0f,
              "近接のフェードの外にあるカメラの後ろは切り抜かれない");
    }

    void TestStrengthAndNearFade()
    {
        ViewClearanceParameters parameters = MakeParameters();
        parameters.Strength = 0.0f;
        Check(ComputeViewClearanceVisibility(parameters, Math::Vector3(1.0f, 2.0f, 8.0f)) == 1.0f,
              "強さ 0 では切り抜きの内側もそのまま");
        Check(ComputeViewClearanceVisibility(parameters, parameters.CameraPosition) == 1.0f,
              "強さ 0 ではカメラの近くもそのまま");

        parameters.Strength = 1.0f;
        const Math::Vector3 nearStart(1.0f, 2.0f, 3.0f - parameters.NearFadeStart);
        const Math::Vector3 nearEnd(1.0f, 2.0f, 3.0f - parameters.NearFadeEnd);
        Check(std::fabs(ComputeViewClearanceVisibility(parameters, nearStart)) < 1.0e-5f,
              "カメラの後ろの近接フェードの開始点は 0");
        Check(std::fabs(ComputeViewClearanceVisibility(parameters, nearEnd) - 1.0f) < 1.0e-5f,
              "カメラの後ろの近接フェードの終了点は 1");
    }

    void TestInvalidRadii()
    {
        const ViewClearanceParameters defaults = MakeDefaultViewClearanceParameters();
        ViewClearanceParameters parameters = MakeParameters();
        parameters.CameraRadius = std::numeric_limits<float>::quiet_NaN();
        parameters.TargetRadius = -1.0f;
        const ViewClearanceParameters sanitized = SanitizeViewClearanceParameters(parameters);
        Check(sanitized.CameraRadius == defaults.CameraRadius, "非有限の半径は既定値に戻る");
        Check(sanitized.TargetRadius == defaults.TargetRadius, "負の半径は既定値に戻る");
    }
}

int main()
{
    TestClearance();
    TestStrengthAndNearFade();
    TestInvalidRadii();

    if (GFailureCount != 0)
    {
        std::printf("ViewClearanceModelTest: 失敗 %d 件\n", GFailureCount);
        return 1;
    }
    std::printf("ViewClearanceModelTest passed\n");
    return 0;
}
