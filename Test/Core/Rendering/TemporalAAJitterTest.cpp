// TAA のジッタ列（Halton(2,3)）と、ジッタを掛けた投影のずらし量を確かめる。
//
// 期待値は Halton 列の手計算の値と、ジッタを掛けない投影で同じ点を投影した NDC との差で与える。
// - 列: 1〜8番目の基数2・3の radical inverse、8フレームで一巡、各点が画素内（-0.5〜0.5）で互いに異なる。
// - 投影: 透視・正射影で、どの深度の点も NDC が同じ量（2 × 画素 / 寸法）だけずれ、z と w は変わらない。
// - velocity: 現在と前のカメラに同じジッタを掛けると、2つの NDC の差はジッタを掛けないときと同じになる。
// - 履歴: 同じ Viewport・同じカメラが前に描いたフレームの履歴を使う。描画がフレームを飛ばしても（10 を描いて
//   11 を飛ばし 12 を描く）履歴は使い、カメラの動きは履歴のカメラから求め直し、物体の動きは経過時間の比で伸ばす。
//   このとき静止した点・等速で動く点が履歴の位置へ戻ること、パケットの velocity だけではずれること（対照）を確かめる。
//   TAA を掛けない2つ目の Viewport は1つ目の履歴を途切れさせず、履歴を書いた Viewport を TAA 無しで描いたら捨てる。
#include "Rendering/TemporalAA.h"
#include "Rendering/CameraViewConstants.h"
#include "Math/MatrixUtils.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

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

    bool IsNear(double lhs, double rhs, double tolerance)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    constexpr uint32_t Width = 1280u;
    constexpr uint32_t Height = 720u;

    CameraProxy MakeCamera(ProjectionType projection)
    {
        CameraProxy camera;
        camera.PositionX = 0.5f;
        camera.PositionY = 1.5f;
        camera.PositionZ = 5.0f;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = -1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.Projection = projection;
        camera.FieldOfView = 60.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 1000.0f;
        camera.OrthoWidth = 16.0f;
        camera.OrthoHeight = 9.0f;
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        return camera;
    }

    struct Projected
    {
        double NdcX = 0.0;
        double NdcY = 0.0;
        double ClipZ = 0.0;
        double ClipW = 0.0;
    };

    Projected Project(const CameraProxy& camera, const Math::Vector3& point)
    {
        const CameraViewConstants constants =
            CameraViewConstants::Build(camera, static_cast<float>(Width) / static_cast<float>(Height));
        const Math::Vector4 clip = Math::MatrixUtils::TransformPoint(constants.ViewProjectionMatrix, point);
        Projected projected;
        projected.NdcX = static_cast<double>(clip.x) / static_cast<double>(clip.w);
        projected.NdcY = static_cast<double>(clip.y) / static_cast<double>(clip.w);
        projected.ClipZ = clip.z;
        projected.ClipW = clip.w;
        return projected;
    }

    // Halton 列の1〜8番目（手計算）。
    constexpr double ExpectedHalton2[8] = {1.0 / 2.0, 1.0 / 4.0, 3.0 / 4.0, 1.0 / 8.0,
                                           5.0 / 8.0, 3.0 / 8.0, 7.0 / 8.0, 1.0 / 16.0};
    constexpr double ExpectedHalton3[8] = {1.0 / 3.0, 2.0 / 3.0, 1.0 / 9.0, 4.0 / 9.0,
                                           7.0 / 9.0, 2.0 / 9.0, 5.0 / 9.0, 8.0 / 9.0};

    void TestHaltonSequence()
    {
        Check(TemporalAAJitterSampleCount >= 8u && TemporalAAJitterSampleCount <= 16u, "ジッタ列は8点以上16点以下");
        for (uint32_t index = 1u; index <= 8u; ++index)
        {
            Check(IsNear(TemporalAAHalton(index, 2u), ExpectedHalton2[index - 1u], 1.0e-7), "Halton 基数2の値");
            Check(IsNear(TemporalAAHalton(index, 3u), ExpectedHalton3[index - 1u], 1.0e-7), "Halton 基数3の値");
        }
        Check(TemporalAAHalton(0u, 2u) == 0.0f, "0番目は0");
        Check(TemporalAAHalton(5u, 1u) == 0.0f, "基数1は0");
    }

    void TestJitterSequence()
    {
        for (uint64_t frame = 0u; frame < TemporalAAJitterSampleCount; ++frame)
        {
            const TemporalAAJitter jitter = ComputeTemporalAAJitter(frame, Width, Height);
            std::printf("frame=%llu jitter_px=(%+.6f, %+.6f) jitter_ndc=(%+.8f, %+.8f)\n",
                        static_cast<unsigned long long>(frame),
                        jitter.PixelX, jitter.PixelY, jitter.NdcX, jitter.NdcY);
            Check(IsNear(jitter.PixelX, ExpectedHalton2[frame] - 0.5, 1.0e-7), "画素のずらし量 x = Halton(2) - 0.5");
            Check(IsNear(jitter.PixelY, ExpectedHalton3[frame] - 0.5, 1.0e-7), "画素のずらし量 y = Halton(3) - 0.5");
            Check(std::fabs(jitter.PixelX) <= 0.5f && std::fabs(jitter.PixelY) <= 0.5f, "画素の中に収まる");
            Check(IsNear(jitter.NdcX, 2.0 * (ExpectedHalton2[frame] - 0.5) / Width, 1.0e-9), "NDC x = 2 × 画素 / 幅");
            Check(IsNear(jitter.NdcY, 2.0 * (ExpectedHalton3[frame] - 0.5) / Height, 1.0e-9), "NDC y = 2 × 画素 / 高さ");

            const TemporalAAJitter repeated = ComputeTemporalAAJitter(frame + TemporalAAJitterSampleCount, Width, Height);
            Check(repeated.PixelX == jitter.PixelX && repeated.PixelY == jitter.PixelY, "8フレームで一巡する");

            for (uint64_t other = 0u; other < frame; ++other)
            {
                const TemporalAAJitter previous = ComputeTemporalAAJitter(other, Width, Height);
                Check(!(previous.PixelX == jitter.PixelX && previous.PixelY == jitter.PixelY), "一巡の中の点は互いに異なる");
            }
        }
        // 1フレーム目の具体値（Halton(2,3) の1番目 = (1/2, 1/3)）。
        const TemporalAAJitter first = ComputeTemporalAAJitter(0u, Width, Height);
        Check(IsNear(first.PixelX, 0.0, 1.0e-7) && IsNear(first.PixelY, -1.0 / 6.0, 1.0e-7), "1フレーム目は (0, -1/6) 画素");
        Check(IsNear(first.NdcY, -0.000462963, 1.0e-9), "1フレーム目の NDC y は -1/6 × 2 / 720");

        const TemporalAAJitter empty = ComputeTemporalAAJitter(3u, 0u, 0u);
        Check(empty.NdcX == 0.0f && empty.NdcY == 0.0f, "寸法0では NDC のずらし量は0");
    }

    void CheckProjectionShift(ProjectionType projectionType, const char* label)
    {
        const Math::Vector3 points[] = {
            Math::Vector3(0.5f, 1.5f, 4.5f),      // 0.5 m 先の中央
            Math::Vector3(-1.2f, 2.3f, 0.0f),     // 5 m 先
            Math::Vector3(8.0f, -3.0f, -45.0f),   // 50 m 先
            Math::Vector3(-90.0f, 60.0f, -495.0f) // 500 m 先
        };
        for (uint64_t frame = 0u; frame < TemporalAAJitterSampleCount; ++frame)
        {
            const TemporalAAJitter jitter = ComputeTemporalAAJitter(frame, Width, Height);
            const CameraProxy plain = MakeCamera(projectionType);
            CameraProxy jittered = plain;
            ApplyTemporalAAJitter(jittered, jitter);
            for (const Math::Vector3& point : points)
            {
                const Projected base = Project(plain, point);
                const Projected shifted = Project(jittered, point);
                const double shiftPixelX = (shifted.NdcX - base.NdcX) * 0.5 * Width;
                const double shiftPixelY = (shifted.NdcY - base.NdcY) * 0.5 * Height;
                if (!IsNear(shiftPixelX, jitter.PixelX, 2.0e-3) || !IsNear(shiftPixelY, jitter.PixelY, 2.0e-3))
                {
                    std::printf("%s frame=%llu shift_px=(%+.6f, %+.6f) expected=(%+.6f, %+.6f)\n",
                                label, static_cast<unsigned long long>(frame),
                                shiftPixelX, shiftPixelY, jitter.PixelX, jitter.PixelY);
                }
                Check(IsNear(shiftPixelX, jitter.PixelX, 2.0e-3), "投影後の x のずれはどの深度でもジッタの画素数");
                Check(IsNear(shiftPixelY, jitter.PixelY, 2.0e-3), "投影後の y のずれはどの深度でもジッタの画素数");
                Check(IsNear(shifted.ClipZ, base.ClipZ, 1.0e-5 * (1.0 + std::fabs(base.ClipZ))), "ジッタはクリップの z を変えない");
                Check(IsNear(shifted.ClipW, base.ClipW, 1.0e-5 * (1.0 + std::fabs(base.ClipW))), "ジッタはクリップの w を変えない");
            }
        }
    }

    void TestProjectionShift()
    {
        CheckProjectionShift(ProjectionType::Perspective, "perspective");
        CheckProjectionShift(ProjectionType::Orthographic, "orthographic");

        // 例: 4フレーム目（Halton の4番目 = (1/8, 4/9)）の透視の中央の点。
        const TemporalAAJitter jitter = ComputeTemporalAAJitter(3u, Width, Height);
        CameraProxy jittered = MakeCamera(ProjectionType::Perspective);
        ApplyTemporalAAJitter(jittered, jitter);
        const Projected center = Project(jittered, Math::Vector3(0.5f, 1.5f, -45.0f));
        std::printf("perspective frame=3 center_shift_px=(%+.6f, %+.6f)\n",
                    center.NdcX * 0.5 * Width, center.NdcY * 0.5 * Height);
        Check(IsNear(center.NdcX * 0.5 * Width, -0.375, 1.0e-3), "中央の点は x へ -0.375 画素ずれる");
        Check(IsNear(center.NdcY * 0.5 * Height, 4.0 / 9.0 - 0.5, 1.0e-3), "中央の点は y へ -1/18 画素ずれる");
    }

    void TestZeroJitterKeepsProjection()
    {
        const CameraProxy camera = MakeCamera(ProjectionType::Perspective);
        const float aspect = static_cast<float>(Width) / static_cast<float>(Height);
        const Math::Matrix4x4 expected =
            Math::MatrixUtils::CreatePerspectiveFieldOfView(60.0f * (3.14159265f / 180.0f), aspect, 0.1f, 1000.0f);
        Check(Math::MatrixUtils::ApproxEqual(CameraViewConstants::BuildProjectionMatrix(camera, aspect), expected, 0.0f),
              "ジッタ0の投影はジッタの無い投影と同じ");
        const CameraViewConstants deviceless = CameraViewConstants::BuildForDevice(camera, aspect, nullptr);
        Check(Math::MatrixUtils::ApproxEqual(deviceless.ProjectionMatrix, expected, 0.0f),
              "デバイスなしの BuildForDevice もジッタの無い投影と同じ");
    }

    void TestVelocityExcludesJitter()
    {
        // 前のフレームから右へ 0.3 m・前へ 0.5 m 動いたカメラ。
        const CameraProxy current = MakeCamera(ProjectionType::Perspective);
        CameraProxy previous = current;
        previous.PositionX -= 0.3f;
        previous.PositionZ += 0.5f;
        const TemporalAAJitter jitter = ComputeTemporalAAJitter(5u, Width, Height);
        CameraProxy jitteredCurrent = current;
        CameraProxy jitteredPrevious = previous;
        ApplyTemporalAAJitter(jitteredCurrent, jitter);
        ApplyTemporalAAJitter(jitteredPrevious, jitter);

        const Math::Vector3 point(2.0f, 0.5f, -20.0f);
        const Projected plainCurrent = Project(current, point);
        const Projected plainPrevious = Project(previous, point);
        const Projected shiftedCurrent = Project(jitteredCurrent, point);
        const Projected shiftedPrevious = Project(jitteredPrevious, point);
        const double plainVelocityX = (plainCurrent.NdcX - plainPrevious.NdcX) * 0.5 * Width;
        const double plainVelocityY = (plainCurrent.NdcY - plainPrevious.NdcY) * 0.5 * Height;
        const double jitteredVelocityX = (shiftedCurrent.NdcX - shiftedPrevious.NdcX) * 0.5 * Width;
        const double jitteredVelocityY = (shiftedCurrent.NdcY - shiftedPrevious.NdcY) * 0.5 * Height;
        const double currentOnlyVelocityX = (shiftedCurrent.NdcX - plainPrevious.NdcX) * 0.5 * Width;
        std::printf("velocity_px plain=(%+.6f, %+.6f) both_jittered=(%+.6f, %+.6f) current_only_x=%+.6f\n",
                    plainVelocityX, plainVelocityY, jitteredVelocityX, jitteredVelocityY, currentOnlyVelocityX);
        Check(std::fabs(plainVelocityX) > 1.0, "カメラの動きで点が1画素以上動く");
        Check(IsNear(jitteredVelocityX, plainVelocityX, 2.0e-3) && IsNear(jitteredVelocityY, plainVelocityY, 2.0e-3),
              "現在と前のカメラへ同じジッタを掛けた velocity はジッタを含まない");
        Check(IsNear(currentOnlyVelocityX - plainVelocityX, jitter.PixelX, 2.0e-3),
              "現在のカメラだけにジッタを掛けると velocity にジッタが入る（比べる対照）");
    }

    // ゲームのフレーム番号 frame（時刻は frame × 10 ms）の問い合わせ。
    TemporalAAHistoryQuery MakeHistoryQuery(uint64_t frameNumber, uint32_t viewportId)
    {
        TemporalAAHistoryQuery query;
        query.FrameNumber = frameNumber;
        query.TotalTime = static_cast<double>(frameNumber) * 0.01;
        query.ViewportId = viewportId;
        query.CameraId = 7u;
        query.PreExposure = 1.0e-4f;
        query.bHasPreviousCamera = true;
        return query;
    }

    void TestHistoryDecision()
    {
        const CameraProxy camera = MakeCamera(ProjectionType::Perspective);
        TemporalAAHistoryTracker history;
        Check(history.Evaluate(MakeHistoryQuery(10u, 0u)) == TemporalAAHistoryDecision::NoHistory, "最初は履歴が無い");

        CameraProxy jitteredCamera = camera;
        ApplyTemporalAAJitter(jitteredCamera, ComputeTemporalAAJitter(3u, Width, Height));
        history.Record(MakeHistoryQuery(10u, 0u), jitteredCamera);
        Check(history.GetCamera().ProjectionJitterNdcX == 0.0f && history.GetCamera().ProjectionJitterNdcY == 0.0f,
              "履歴のカメラはジッタを外して覚える");
        Check(history.Evaluate(MakeHistoryQuery(11u, 0u)) == TemporalAAHistoryDecision::Reuse,
              "直前のフレームに同じ Viewport が書いた履歴は使う");
        Check(history.IsContiguous(MakeHistoryQuery(11u, 0u)), "直前のフレームなら連続");
        Check(history.ComputeObjectMotionScale(MakeHistoryQuery(11u, 0u), 0.01f) == 1.0f, "連続なら物体の動きの比は1");
        // 描画が 11 を飛ばして 12 を描く: 履歴は使い、カメラは履歴のカメラから求め直し、物体の動きは2倍に伸ばす。
        Check(history.Evaluate(MakeHistoryQuery(12u, 0u)) == TemporalAAHistoryDecision::Reuse,
              "フレームが飛んでも履歴は使う");
        Check(!history.IsContiguous(MakeHistoryQuery(12u, 0u)), "飛んだフレームは連続でない");
        Check(IsNear(history.ComputeObjectMotionScale(MakeHistoryQuery(12u, 0u), 0.01f), 2.0, 1.0e-5),
              "飛んだフレームの物体の動きの比は経過時間 / パケットの経過時間");
        Check(history.ComputeObjectMotionScale(MakeHistoryQuery(12u, 0u), 0.0f) == 1.0f,
              "パケットの経過時間が0なら比は1");
        Check(history.Evaluate(MakeHistoryQuery(10u, 0u)) == TemporalAAHistoryDecision::FrameNotAdvanced,
              "同じフレームをもう一度描くときは使わない");
        Check(history.Evaluate(MakeHistoryQuery(11u, 1u)) == TemporalAAHistoryDecision::ViewportChanged,
              "別の Viewport の履歴は使わない");

        TemporalAAHistoryQuery query = MakeHistoryQuery(11u, 0u);
        query.CameraId = 8u;
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::CameraChanged, "カメラが替わったら使わない");
        query = MakeHistoryQuery(11u, 0u);
        query.bHasPreviousCamera = false;
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::PreviousCameraMissing,
              "前のカメラが無ければ使わない");
        query = MakeHistoryQuery(11u, 0u);
        query.PreExposure = 0.0f;
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::InvalidExposure, "露出が0なら使わない");
        query.PreExposure = std::nanf("");
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::InvalidExposure, "露出が NaN なら使わない");
        query.PreExposure = INFINITY;
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::InvalidExposure, "露出が無限なら使わない");

        // 同じフレームの2つ目の Viewport（TAA を掛けない）は、1つ目の履歴を捨てない。
        history.NotifyViewportWithoutTemporalAA(1u);
        Check(history.Evaluate(MakeHistoryQuery(11u, 0u)) == TemporalAAHistoryDecision::Reuse,
              "TAA を掛けない別の Viewport は履歴を途切れさせない");
        // 履歴を書いた Viewport を TAA 無しで描いたら、その間の画像が履歴に入らないので捨てる。
        history.NotifyViewportWithoutTemporalAA(0u);
        Check(history.Evaluate(MakeHistoryQuery(11u, 0u)) == TemporalAAHistoryDecision::NoHistory,
              "履歴を書いた Viewport を TAA 無しで描いたら捨てる");

        history.Record(MakeHistoryQuery(11u, 0u), camera);
        history.Invalidate();
        Check(history.Evaluate(MakeHistoryQuery(12u, 0u)) == TemporalAAHistoryDecision::NoHistory,
              "捨てた後は履歴が無い");
    }

    void TestHistoryWithDroppedFrames()
    {
        // ゲームのフレーム 1〜30 のうち、RenderThread が 5・9・10・20 を描かずに捨てる。各フレームは
        // Viewport 0（TAA を掛ける）と Viewport 1（同じフレームの2つ目。TAA を掛けない）を描く。
        // TemporalAAPass と同じく、使えるかを決めてから、働いたフレームで履歴を書く。
        const auto isDropped = [](uint64_t frame) { return frame == 5u || frame == 9u || frame == 10u || frame == 20u; };
        const CameraProxy camera = MakeCamera(ProjectionType::Perspective);
        TemporalAAHistoryTracker history;
        uint32_t renderedCount = 0u;
        uint32_t reusedCount = 0u;
        uint32_t rebasedCount = 0u;
        bool bScalesMatch = true;
        for (uint64_t frame = 1u; frame <= 30u; ++frame)
        {
            if (isDropped(frame))
            {
                continue;
            }
            ++renderedCount;
            const TemporalAAHistoryQuery query = MakeHistoryQuery(frame, 0u);
            if (history.Evaluate(query) == TemporalAAHistoryDecision::Reuse)
            {
                ++reusedCount;
                const bool bContiguous = history.IsContiguous(query);
                rebasedCount += bContiguous ? 0u : 1u;
                Check(bContiguous == !isDropped(frame - 1u), "直前のフレームが捨てられたときだけ連続でない");
                // 捨てた数 + 1 フレーム分の時間が経っている（6 は 4 から 2、11 は 8 から 3、21 は 19 から 2）。
                const double expectedScale = frame == 11u ? 3.0 : (bContiguous ? 1.0 : 2.0);
                bScalesMatch = bScalesMatch && IsNear(history.ComputeObjectMotionScale(query, 0.01f), expectedScale, 1.0e-4);
            }
            history.Record(query, camera);
            // Viewport 1 は TAA を掛けない。
            history.NotifyViewportWithoutTemporalAA(1u);
        }
        std::printf("history rendered=%u reused=%u rebased=%u\n", renderedCount, reusedCount, rebasedCount);
        Check(renderedCount == 26u, "30 フレームのうち 26 を描く");
        Check(reusedCount == 25u, "最初の1回のほかは、捨てたフレームをまたいでも履歴を使う");
        Check(rebasedCount == 3u, "捨てた並びの直後（6・11・21）の3回は履歴のカメラから求め直す");
        Check(bScalesMatch, "物体の動きの比は履歴からの経過フレーム数");
    }

    // 点が view・projection の other カメラから current カメラへ動いた量（画素、currentUV - otherUV に同じ）。
    // TAA のシェーダーの CameraMotion と velocity の定義（(currentNdc - previousNdc) × 0.5）に合わせる。
    struct PixelMotion
    {
        double X = 0.0;
        double Y = 0.0;
    };

    PixelMotion MotionPx(const CameraProxy& current, const Math::Vector3& currentPoint,
                         const CameraProxy& other, const Math::Vector3& otherPoint)
    {
        const Projected a = Project(current, currentPoint);
        const Projected b = Project(other, otherPoint);
        return {(a.NdcX - b.NdcX) * 0.5 * Width, (a.NdcY - b.NdcY) * 0.5 * Height};
    }

    void TestHistoryReprojectionAcrossDroppedFrame()
    {
        // 履歴はフレーム 10、パケット 11 は捨てられ、12 を描く。カメラは毎フレーム右へ 0.2 m 動き、
        // 物体は毎フレーム +x へ 0.05 m 等速で動く。パケットの velocity は 11→12 の動き。
        CameraProxy camera10 = MakeCamera(ProjectionType::Perspective);
        CameraProxy camera11 = camera10;
        CameraProxy camera12 = camera10;
        camera11.PositionX += 0.2f;
        camera12.PositionX += 0.4f;
        const TemporalAAJitter jitter = ComputeTemporalAAJitter(6u, Width, Height);
        // 今のフレームのジッタを、今・パケットの前・履歴のカメラの3つへ掛ける（TemporalAAPass と同じ）。
        ApplyTemporalAAJitter(camera12, jitter);
        ApplyTemporalAAJitter(camera11, jitter);
        TemporalAAHistoryTracker history;
        history.Record(MakeHistoryQuery(10u, 0u), camera10);
        CameraProxy historyCamera = history.GetCamera();
        ApplyTemporalAAJitter(historyCamera, jitter);
        const TemporalAAHistoryQuery query = MakeHistoryQuery(12u, 0u);
        const float scale = history.ComputeObjectMotionScale(query, 0.01f);

        // 静止した点: カメラの動きだけ。
        const Math::Vector3 still(1.0f, 0.5f, -12.0f);
        const PixelMotion truth = MotionPx(camera12, still, historyCamera, still);
        const PixelMotion velocity = MotionPx(camera12, still, camera11, still);
        const PixelMotion packetCamera = MotionPx(camera12, still, camera11, still);
        const PixelMotion historyCameraMotion = MotionPx(camera12, still, historyCamera, still);
        const PixelMotion composed{historyCameraMotion.X + (velocity.X - packetCamera.X) * scale,
                                   historyCameraMotion.Y + (velocity.Y - packetCamera.Y) * scale};
        std::printf("still_px truth=(%+.4f, %+.4f) composed=(%+.4f, %+.4f) packet_velocity=(%+.4f, %+.4f)\n",
                    truth.X, truth.Y, composed.X, composed.Y, velocity.X, velocity.Y);
        Check(IsNear(composed.X, truth.X, 1.0e-3) && IsNear(composed.Y, truth.Y, 1.0e-3),
              "静止した点は、フレームが飛んでも履歴の位置へ戻る");
        Check(std::fabs(velocity.X - truth.X) > 1.0, "パケットの velocity だけでは履歴の位置から1画素以上ずれる（比べる対照）");

        // 等速で動く点: 物体の動きを経過時間の比で伸ばす。
        const Math::Vector3 moving10(-1.0f, 0.5f, -10.0f);
        const Math::Vector3 moving11(-0.95f, 0.5f, -10.0f);
        const Math::Vector3 moving12(-0.9f, 0.5f, -10.0f);
        const PixelMotion movingTruth = MotionPx(camera12, moving12, historyCamera, moving10);
        const PixelMotion movingVelocity = MotionPx(camera12, moving12, camera11, moving11);
        const PixelMotion movingPacketCamera = MotionPx(camera12, moving12, camera11, moving12);
        const PixelMotion movingHistoryCamera = MotionPx(camera12, moving12, historyCamera, moving12);
        const PixelMotion movingComposed{
            movingHistoryCamera.X + (movingVelocity.X - movingPacketCamera.X) * scale,
            movingHistoryCamera.Y + (movingVelocity.Y - movingPacketCamera.Y) * scale};
        std::printf("moving_px truth=(%+.4f, %+.4f) composed=(%+.4f, %+.4f) packet_velocity=(%+.4f, %+.4f)\n",
                    movingTruth.X, movingTruth.Y, movingComposed.X, movingComposed.Y, movingVelocity.X, movingVelocity.Y);
        Check(IsNear(movingComposed.X, movingTruth.X, 0.05) && IsNear(movingComposed.Y, movingTruth.Y, 0.05),
              "等速で動く点は、フレームが飛んでも履歴の位置の 0.05 画素以内へ戻る");
        Check(std::fabs(movingVelocity.X - movingTruth.X) > 1.0,
              "動く点もパケットの velocity だけでは1画素以上ずれる（比べる対照）");

        // 連続したフレームでは、履歴のカメラはパケットの前のカメラで比は1なので、velocity そのものになる。
        TemporalAAHistoryTracker contiguous;
        contiguous.Record(MakeHistoryQuery(11u, 0u), camera11);
        const float contiguousScale = contiguous.ComputeObjectMotionScale(query, 0.01f);
        const PixelMotion contiguousComposed{
            movingPacketCamera.X + (movingVelocity.X - movingPacketCamera.X) * contiguousScale,
            movingPacketCamera.Y + (movingVelocity.Y - movingPacketCamera.Y) * contiguousScale};
        Check(IsNear(contiguousComposed.X, movingVelocity.X, 1.0e-9) &&
                  IsNear(contiguousComposed.Y, movingVelocity.Y, 1.0e-9),
              "連続したフレームでは velocity での再投影と同じ");
    }
} // namespace

int main()
{
    TestHaltonSequence();
    TestJitterSequence();
    TestProjectionShift();
    TestZeroJitterKeepsProjection();
    TestVelocityExcludesJitter();
    TestHistoryDecision();
    TestHistoryWithDroppedFrames();
    TestHistoryReprojectionAcrossDroppedFrame();

    if (GFailureCount != 0)
    {
        std::printf("TemporalAAJitterTest: 失敗 %d 件\n", GFailureCount);
        return 1;
    }
    std::printf("TemporalAAJitterTest passed\n");
    return 0;
}
