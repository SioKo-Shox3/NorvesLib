// 描画用に補間した剣の帯の連続性と寿命を CPU で反証する。
#include "Particle/SwordTrailRibbon.h"
#include "Scene/RenderTransformHistory.h"

#include <cmath>
#include <cstdint>
#include <iostream>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Particle;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFixedStep = 1.0 / 60.0;
    constexpr double kAngularSpeed = kPi / 2.0;
    constexpr float kPositionTolerance = 0.0001f;
    // 10 度に対して 0.1% の余裕を設け、浮動小数点の丸めだけを許す。
    constexpr double kAngleToleranceDegrees = 0.01;

    bool Check(bool bCondition, const char* item, const char* reason)
    {
        if (!bCondition)
        {
            std::cerr << item << ": " << reason << '\n';
        }
        return bCondition;
    }

#define REQUIRE(condition, reason) \
    do \
    { \
        if (!Check((condition), item, (reason))) \
        { \
            return false; \
        } \
    } while (false)

    Math::Transform BladePose(double angleRadians)
    {
        Math::Transform result;
        result.rotation = Math::Quaternion(Math::Vector3(0.0f, 0.0f, 1.0f), static_cast<float>(angleRadians));
        return result;
    }

    Math::Vector3 ExpectedTip(double angleRadians)
    {
        return Math::Vector3(static_cast<float>(-std::sin(angleRadians)),
            static_cast<float>(std::cos(angleRadians)), 0.0f);
    }

    bool IsFinite(const Math::Vector3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool Near(const Math::Vector3& left, const Math::Vector3& right)
    {
        return (left - right).Length() <= kPositionTolerance;
    }

    double AngleDegrees(const Math::Vector3& left, const Math::Vector3& right)
    {
        const double lx = left.x;
        const double ly = left.y;
        const double lz = left.z;
        const double rx = right.x;
        const double ry = right.y;
        const double rz = right.z;
        const double cx = ly * rz - lz * ry;
        const double cy = lz * rx - lx * rz;
        const double cz = lx * ry - ly * rx;
        return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), lx * rx + ly * ry + lz * rz) * 180.0 / kPi;
    }

    bool TestInterpolatedFrames()
    {
        const char* item = "1. 描画フレームの連続性";
        SwordTrailRibbonSettings settings;
        // この検査では古い点を消さず、フレームごとの追加を直接数える。
        settings.Lifetime = 10.0f;
        settings.MaxPoints = 512u;
        SwordTrailRibbon ribbon;
        REQUIRE(ribbon.Configure(settings), "帯を設定できない");
        Scene::RenderTransformHistory history;
        Math::Transform simulation = BladePose(0.0);
        REQUIRE(history.Reset(simulation), "補間履歴を初期化できない");
        Math::Transform rendered;
        REQUIRE(history.Evaluate(0.0f, rendered), "初期の描画変換を評価できない");
        ribbon.AddSample(rendered, 0.0);
        REQUIRE(ribbon.GetPointCount() == 1u, "初期の点がない");

        // 1 単位を 1/720 秒にすると、1/60・1/144・1/30 秒を整数で扱える。
        constexpr uint32_t kFixedUnits = 12u;
        constexpr uint32_t kFrameUnits[] = {12u, 5u, 24u};
        constexpr double kUnitsPerSecond = 720.0;
        const float maxTipDistance = static_cast<float>(kAngularSpeed / 30.0) + kPositionTolerance;
        uint32_t accumulator = 0u;
        uint32_t elapsedUnits = 0u;
        uint32_t simulationSteps = 0u;
        bool bObservedSteps[3] = {};
        bool bZeroStepAdded = false;
        for (uint32_t frame = 0u; frame < 144u; ++frame)
        {
            const uint32_t frameUnits = kFrameUnits[frame % 3u];
            accumulator += frameUnits;
            elapsedUnits += frameUnits;
            uint32_t stepsThisFrame = 0u;
            while (accumulator >= kFixedUnits)
            {
                REQUIRE(history.Prepare(simulation), "固定ステップ前の履歴を準備できない");
                ++simulationSteps;
                simulation = BladePose(static_cast<double>(simulationSteps) * kFixedStep * kAngularSpeed);
                REQUIRE(history.Capture(simulation), "固定ステップ後の変換を捕捉できない");
                accumulator -= kFixedUnits;
                ++stepsThisFrame;
            }
            REQUIRE(stepsThisFrame <= 2u, "固定ステップ回数の試験条件が違う");
            bObservedSteps[stepsThisFrame] = true;
            const float alpha = static_cast<float>(accumulator) / static_cast<float>(kFixedUnits);
            const double time = static_cast<double>(elapsedUnits) / kUnitsPerSecond;
            const uint32_t previousCount = ribbon.GetPointCount();
            ribbon.Tick(static_cast<float>(static_cast<double>(frameUnits) / kUnitsPerSecond));
            REQUIRE(history.Prepare(simulation) && history.Evaluate(alpha, rendered), "描画変換を評価できない");
            ribbon.AddSample(rendered, time);
            const uint32_t count = ribbon.GetPointCount();
            REQUIRE(count > 0u, "描画フレームの点列が空");
            if (stepsThisFrame == 0u)
            {
                REQUIRE(count > previousCount, "固定ステップ 0 回のフレームで動く剣の点が増えない");
                bZeroStepAdded = true;
            }

            // 一定角速度の解析値で、帯の先が描画用の補間位置にあることも確かめる。
            const double renderedSteps = simulationSteps == 0u ? 0.0 :
                static_cast<double>(simulationSteps - 1u) + static_cast<double>(alpha);
            REQUIRE(Near(ribbon.GetPoint(count - 1u).Tip, ExpectedTip(renderedSteps * kFixedStep * kAngularSpeed)),
                "帯の先が描画用の補間位置と違う");
            for (uint32_t index = 0u; index < count; ++index)
            {
                const auto& point = ribbon.GetPoint(index);
                REQUIRE(IsFinite(point.Base) && IsFinite(point.Tip) &&
                    std::isfinite(point.Time) && std::isfinite(point.Age), "点の位置・時刻・年齢が有限でない");
                if (index > 0u)
                {
                    const auto& previous = ribbon.GetPoint(index - 1u);
                    REQUIRE(point.Time > previous.Time, "点の時刻が古い順に増加しない");
                    REQUIRE((point.Tip - previous.Tip).Length() <= maxTipDistance, "隣の切っ先の距離が大きすぎる");
                    REQUIRE(Math::Vector3::Cross(previous.Tip - previous.Base, point.Tip - point.Base).z > 0.0f,
                        "帯の向きが止まるか巻き戻る");
                }
            }
            Rendering::RibbonTrailScene scene;
            ribbon.AppendDraw(scene);
            if (count >= 2u)
            {
                REQUIRE(scene.Vertices.size() == static_cast<size_t>(count) * 2u, "描画頂点が点の 2 倍でない");
                for (const auto& vertex : scene.Vertices)
                {
                    for (float value : vertex.Position)
                        REQUIRE(std::isfinite(value), "描画頂点の位置が有限でない");
                    for (float value : vertex.UV)
                        REQUIRE(std::isfinite(value), "描画頂点の UV が有限でない");
                    for (float value : vertex.Color)
                        REQUIRE(std::isfinite(value), "描画頂点の色が有限でない");
                }
            }
        }
        REQUIRE(bObservedSteps[0] && bObservedSteps[1] && bObservedSteps[2] && bZeroStepAdded,
            "固定ステップ 0・1・2 回と 0 回の点の追加を観測できない");
        return true;
    }

    bool TestLargeSwing()
    {
        const char* item = "2. 120 度の振り";
        SwordTrailRibbonSettings settings;
        settings.MaxBendDegrees = 10.0f;
        SwordTrailRibbon ribbon;
        REQUIRE(ribbon.Configure(settings), "帯を設定できない");
        Scene::RenderTransformHistory history;
        Math::Transform simulation = BladePose(0.0);
        Math::Transform rendered;
        REQUIRE(history.Reset(simulation) && history.Evaluate(0.0f, rendered), "初期の描画変換を評価できない");
        ribbon.AddSample(rendered, 0.0);
        REQUIRE(history.Prepare(simulation), "固定ステップ前の履歴を準備できない");
        simulation = BladePose(120.0 * kPi / 180.0);
        REQUIRE(history.Capture(simulation), "固定ステップ後の変換を捕捉できない");
        ribbon.Tick(static_cast<float>(kFixedStep));
        REQUIRE(history.Prepare(simulation) && history.Evaluate(1.0f, rendered), "描画変換を評価できない");
        ribbon.AddSample(rendered, kFixedStep);

        const uint32_t count = ribbon.GetPointCount();
        REQUIRE(count >= 13u, "120 度の振りに必要な点がない");
        REQUIRE(Near(ribbon.GetPoint(0u).Tip, ExpectedTip(0.0)) &&
            Near(ribbon.GetPoint(count - 1u).Tip, ExpectedTip(120.0 * kPi / 180.0)),
            "振りの始点か終点が違う");
        const double maxAngle = static_cast<double>(settings.MaxBendDegrees) + kAngleToleranceDegrees;
        for (uint32_t index = 1u; index < count; ++index)
        {
            const auto& previous = ribbon.GetPoint(index - 1u);
            const auto& point = ribbon.GetPoint(index);
            const Math::Vector3 previousBlade = previous.Tip - previous.Base;
            const Math::Vector3 blade = point.Tip - point.Base;
            REQUIRE(IsFinite(previousBlade) && IsFinite(blade) &&
                previousBlade.Length() > 0.00001f && blade.Length() > 0.00001f, "刃の向きが求められない");
            REQUIRE(AngleDegrees(previousBlade, blade) <= maxAngle, "隣り合う刃の向きの差が上限を超えた");
            if (index >= 2u)
            {
                const Math::Vector3 firstEdge = previous.Tip - ribbon.GetPoint(index - 2u).Tip;
                const Math::Vector3 secondEdge = point.Tip - previous.Tip;
                REQUIRE(IsFinite(firstEdge) && IsFinite(secondEdge) &&
                    firstEdge.Length() > 0.00001f && secondEdge.Length() > 0.00001f, "切っ先の縁の向きが求められない");
                REQUIRE(AngleDegrees(firstEdge, secondEdge) <= maxAngle, "切っ先の縁の折れ目が上限を超えた");
            }
        }
        return true;
    }

    bool TestLifetimeAndLimit()
    {
        const char* item = "3. 寿命と点数の上限";
        SwordTrailRibbonSettings settings;
        SwordTrailRibbon ribbon;
        REQUIRE(ribbon.Configure(settings), "帯を設定できない");
        Scene::RenderTransformHistory history;
        Math::Transform simulation = BladePose(0.0);
        Math::Transform rendered;
        REQUIRE(history.Reset(simulation) && history.Evaluate(0.0f, rendered), "初期の描画変換を評価できない");
        ribbon.AddSample(rendered, 0.0);
        for (uint32_t step = 1u; step <= 120u; ++step)
        {
            REQUIRE(history.Prepare(simulation), "固定ステップ前の履歴を準備できない");
            simulation = BladePose(static_cast<double>(step) * kFixedStep * kAngularSpeed);
            REQUIRE(history.Capture(simulation), "固定ステップ後の変換を捕捉できない");
            ribbon.Tick(static_cast<float>(kFixedStep));
            REQUIRE(history.Prepare(simulation) && history.Evaluate(1.0f, rendered), "描画変換を評価できない");
            ribbon.AddSample(rendered, static_cast<double>(step) * kFixedStep);
            REQUIRE(ribbon.GetPointCount() <= settings.MaxPoints, "点数が上限を超えた");
            for (uint32_t index = 0u; index < ribbon.GetPointCount(); ++index)
            {
                REQUIRE(std::isfinite(ribbon.GetPoint(index).Age) && ribbon.GetPoint(index).Age < settings.Lifetime,
                    "寿命に達した点が残った");
            }
        }
        REQUIRE(ribbon.GetPointCount() >= 2u, "寿命より長い振りの後に帯がない");
        Rendering::RibbonTrailScene scene;
        ribbon.AppendDraw(scene);
        const size_t previousVertices = scene.Vertices.size();
        const size_t previousDraws = scene.Draws.size();
        REQUIRE(previousVertices > 0u && previousDraws > 0u, "寿命切れ前の描画がない");
        ribbon.Tick(settings.Lifetime);
        REQUIRE(ribbon.GetPointCount() == 0u, "停止後に寿命を過ぎても点が消えない");
        ribbon.AppendDraw(scene);
        REQUIRE(scene.Vertices.size() == previousVertices && scene.Draws.size() == previousDraws,
            "点が消えた後に描画が追加された");

        SwordTrailRibbon uncapped;
        REQUIRE(uncapped.Configure(settings), "比較用の帯を設定できない");
        settings.MaxPoints = 8u;
        SwordTrailRibbon capped;
        REQUIRE(capped.Configure(settings), "小さい点数上限を設定できない");
        simulation = BladePose(0.0);
        REQUIRE(history.Reset(simulation) && history.Evaluate(0.0f, rendered), "上限確認の描画変換を評価できない");
        capped.AddSample(rendered, 0.0);
        uncapped.AddSample(rendered, 0.0);
        REQUIRE(history.Prepare(simulation), "上限確認の履歴を準備できない");
        simulation = BladePose(120.0 * kPi / 180.0);
        REQUIRE(history.Capture(simulation), "上限確認の変換を捕捉できない");
        capped.Tick(static_cast<float>(kFixedStep));
        uncapped.Tick(static_cast<float>(kFixedStep));
        REQUIRE(history.Prepare(simulation) && history.Evaluate(1.0f, rendered), "上限確認の描画変換を評価できない");
        capped.AddSample(rendered, kFixedStep);
        uncapped.AddSample(rendered, kFixedStep);
        REQUIRE(capped.GetPointCount() == settings.MaxPoints, "大きな振りで点数が上限にならない");
        REQUIRE(uncapped.GetPointCount() > settings.MaxPoints, "比較用の点数が上限を超えていない");
        // 最大 10 度刻みの最後の 8 点は、振りの最初の 1/3 より後にある。
        const double oldestAllowedTime = kFixedStep * (1.0 -
            static_cast<double>(settings.MaxPoints) * settings.MaxBendDegrees / 120.0);
        for (uint32_t index = 0u; index < capped.GetPointCount(); ++index)
        {
            const auto& point = capped.GetPoint(index);
            REQUIRE(std::isfinite(point.Time) && point.Time > oldestAllowedTime && point.Time <= kFixedStep,
                "点数の上限で新しい点が残らなかった");
            const auto& expected = uncapped.GetPoint(uncapped.GetPointCount() - settings.MaxPoints + index);
            REQUIRE(std::fabs(point.Time - expected.Time) < 1.0e-9 &&
                Near(point.Base, expected.Base) && Near(point.Tip, expected.Tip),
                "上限のある帯が比較用の新しい 8 点と一致しない");
            if (index > 0u)
                REQUIRE(point.Time > capped.GetPoint(index - 1u).Time, "残った点の時刻順が違う");
        }
        REQUIRE(std::fabs(capped.GetPoint(settings.MaxPoints - 1u).Time - kFixedStep) < 1.0e-9 &&
            Near(capped.GetPoint(settings.MaxPoints - 1u).Tip, ExpectedTip(120.0 * kPi / 180.0)),
            "最新の点が残らなかった");
        return true;
    }

#undef REQUIRE
} // 無名名前空間

int main()
{
    const bool bFrames = TestInterpolatedFrames();
    const bool bSwing = TestLargeSwing();
    const bool bLifetime = TestLifetimeAndLimit();
    return bFrames && bSwing && bLifetime ? 0 : 1;
}
