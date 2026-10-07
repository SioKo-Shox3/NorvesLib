// TAA のジッタ列（Halton(2,3)）と、ジッタを掛けた投影のずらし量を確かめる。
//
// 期待値は Halton 列の手計算の値と、ジッタを掛けない投影で同じ点を投影した NDC との差で与える。
// - 列: 1〜8番目の基数2・3の radical inverse、8フレームで一巡、各点が画素内（-0.5〜0.5）で互いに異なる。
// - 投影: 透視・正射影で、どの深度の点も NDC が同じ量（2 × 画素 / 寸法）だけずれ、z と w は変わらない。
// - velocity: 現在と前のカメラに同じジッタを掛けると、2つの NDC の差はジッタを掛けないときと同じになる。
// - 履歴: 同じ Viewport・同じカメラが前に描いたフレームの履歴を、velocity の基準（物体の前の変換）が履歴の
//   フレームを指すときだけ使う。描画がフレームを飛ばしたとき（10 を描いて 11 を飛ばし 12 を描く）は、
//   RenderedObjectHistory がパケットの前の変換を 10 へ付け替え、前のカメラは履歴のカメラにする。動いて止まる・
//   動いて戻る・等速の物体と、カメラも動く場合で、velocity が履歴の位置へ戻ること、パケットの velocity のまま
//   ではずれること（対照）、付け替えなければ基準が食い違って履歴を使わないこと（対照）を確かめる。
//   TAA を掛けない2つ目の Viewport は1つ目の履歴を途切れさせず、履歴を書いた Viewport を TAA 無しで描いたら捨てる。
#include "Rendering/TemporalAA.h"
#include "Rendering/RenderedObjectHistory.h"
#include "Rendering/CameraViewConstants.h"
#include "Math/MatrixUtils.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

    // ゲームのフレーム番号 frameNumber の問い合わせ。velocity の物体の前の変換は previousObjectFrame を指す。
    TemporalAAHistoryQuery MakeHistoryQuery(uint64_t frameNumber, uint32_t viewportId, uint64_t previousObjectFrame)
    {
        TemporalAAHistoryQuery query;
        query.FrameNumber = frameNumber;
        query.ViewportId = viewportId;
        query.CameraId = 7u;
        query.PreExposure = 1.0e-4f;
        query.bHasPreviousCamera = true;
        query.PreviousObjectStateFrameNumber = previousObjectFrame;
        query.bPreviousObjectStateComplete = true;
        return query;
    }

    // 直前のフレームの変換を指す（連続したフレームの）問い合わせ。
    TemporalAAHistoryQuery MakeHistoryQuery(uint64_t frameNumber, uint32_t viewportId)
    {
        return MakeHistoryQuery(frameNumber, viewportId, frameNumber - 1u);
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
        Check(history.FindReprojectionCamera(0u, 7u, 0u, 11u) == nullptr,
              "連続したフレームではパケットの前のカメラをそのまま使う");

        // 描画が 11 を飛ばして 12 を描く: 物体の前の変換が履歴のフレーム 10 を指すときだけ使う。
        Check(history.Evaluate(MakeHistoryQuery(12u, 0u, 10u)) == TemporalAAHistoryDecision::Reuse,
              "フレームが飛んでも、物体の前の変換を履歴のフレームへ付け替えてあれば使う");
        Check(!history.IsContiguous(MakeHistoryQuery(12u, 0u, 10u)), "飛んだフレームは連続でない");
        Check(history.Evaluate(MakeHistoryQuery(12u, 0u, 11u)) == TemporalAAHistoryDecision::ObjectStateMismatch,
              "物体の前の変換が飛ばしたフレーム 11 のままなら使わない");
        TemporalAAHistoryQuery incomplete = MakeHistoryQuery(12u, 0u, 10u);
        incomplete.bPreviousObjectStateComplete = false;
        Check(history.Evaluate(incomplete) == TemporalAAHistoryDecision::ObjectStateMismatch,
              "付け替えきれなかった物体があれば使わない");
        const CameraProxy* reprojectionCamera = history.FindReprojectionCamera(0u, 7u, 0u, 12u);
        Check(reprojectionCamera != nullptr && reprojectionCamera->PositionX == camera.PositionX &&
                  reprojectionCamera->ProjectionJitterNdcX == 0.0f,
              "飛んだフレームの前のカメラは、履歴を書いたフレームのカメラ（ジッタなし）");
        Check(history.FindReprojectionCamera(1u, 7u, 0u, 12u) == nullptr, "別の Viewport には履歴のカメラを渡さない");

        Check(history.Evaluate(MakeHistoryQuery(10u, 0u)) == TemporalAAHistoryDecision::FrameNotAdvanced,
              "同じフレームをもう一度描くときは使わない");
        Check(history.Evaluate(MakeHistoryQuery(11u, 1u)) == TemporalAAHistoryDecision::ViewportChanged,
              "別の Viewport の履歴は使わない");

        TemporalAAHistoryQuery query = MakeHistoryQuery(11u, 0u);
        query.CameraId = 8u;
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::CameraChanged, "カメラが替わったら使わない");
        Check(history.FindReprojectionCamera(0u, 8u, 0u, 12u) == nullptr, "替わったカメラには履歴のカメラを渡さない");
        query = MakeHistoryQuery(11u, 0u);
        query.SourceCameraId = 42u;
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::CameraChanged,
              "登録 ID が同じでも GameThread 側のカメラが替わったら使わない");
        Check(history.FindReprojectionCamera(0u, 7u, 42u, 12u) == nullptr,
              "GameThread 側のカメラが替わったら履歴のカメラを渡さない");
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

    // ===== パケットの物体の前の変換の付け替え（RenderedObjectHistory） =====

    // 物体の x 座標ごとの World（y = 0.5、z = -10）。行列のビット列で x を引き戻せるように表で持つ。
    struct PlacedWorld
    {
        float X = 0.0f;
        Math::Matrix4x4 World;
    };

    Math::Matrix4x4 WorldAt(float x)
    {
        return Math::MatrixUtils::CreateTranslation(x, 0.5f, -10.0f);
    }

    // 行列 values が表のどの x の World か（無ければ NaN）。
    float PositionXOf(const float (&values)[16], const float* candidates, size_t candidateCount)
    {
        for (size_t index = 0; index < candidateCount; ++index)
        {
            const Math::Matrix4x4 world = WorldAt(candidates[index]);
            if (std::memcmp(world.values, values, sizeof(world.values)) == 0)
            {
                return candidates[index];
            }
        }
        return std::nanf("");
    }

    // GameThread と同じ形のパケット: 物体 A（非インスタンシング、ComponentId 101）と、同じメッシュの
    // 物体 B・C（インスタンシング2個、ComponentId 201・202）。PreviousWorld は直前のゲームのフレームの変換。
    struct ObjectTrack
    {
        float Current = 0.0f;
        float Previous = 0.0f;
    };

    void BuildPacket(FramePacket& packet, uint64_t frameNumber, ObjectTrack a, ObjectTrack b, ObjectTrack c)
    {
        packet.FrameNumber = frameNumber;
        packet.Scene.MeshProxies.clear();
        packet.DrawCommands.clear();
        packet.InstanceData.clear();
        const auto addProxy = [&](uint64_t objectId, uint64_t componentId, uint64_t meshId, ObjectTrack track)
        {
            MeshProxy proxy;
            proxy.ObjectId = objectId;
            proxy.ComponentId = componentId;
            proxy.MeshHandle.Id = meshId;
            proxy.WorldTransform = WorldAt(track.Current);
            proxy.PreviousWorldTransform = WorldAt(track.Previous);
            packet.Scene.MeshProxies.push_back(proxy);
            GPUSceneInstanceData data{};
            std::memcpy(data.World, proxy.WorldTransform.values, sizeof(data.World));
            std::memcpy(data.PreviousWorld, proxy.PreviousWorldTransform.values, sizeof(data.PreviousWorld));
            packet.InstanceData.push_back(data);
        };
        addProxy(1u, 101u, 5u, a);
        addProxy(2u, 201u, 6u, b);
        addProxy(3u, 202u, 6u, c);

        DrawCommand single;
        single.Draw.MeshHandle.Id = 5u;
        single.Draw.ObjectId = 1u;
        single.Draw.InstanceDataOffset = 0u;
        single.Draw.WorldMatrix = WorldAt(a.Current);
        packet.DrawCommands.push_back(single);
        DrawCommand instanced;
        instanced.Draw.MeshHandle.Id = 6u;
        instanced.Draw.ObjectId = 2u;
        instanced.Draw.bInstanced = true;
        instanced.Draw.InstanceCount = 2u;
        instanced.Draw.InstanceDataOffset = 1u;
        packet.DrawCommands.push_back(instanced);
    }

    void TestObjectHistoryAcrossDroppedFrame()
    {
        // 評価の再現: カメラは止まり、フレーム 10・11・12 で物体の点が動く。10 を描き、11 を飛ばして 12 を描く。
        // 物体 A は動いて止まる（-1.0 → -0.9 → -0.9）、B は動いて戻る（2.0 → 2.1 → 2.0）、C は等速（0.0 → 0.1 → 0.2）。
        // 止まった物体は GameThread のパケットでは velocity が0になり、履歴の位置（10）から外れる。
        const float positions[] = {-1.0f, -0.9f, 2.0f, 2.1f, 0.0f, 0.1f, 0.2f};
        const size_t positionCount = sizeof(positions) / sizeof(positions[0]);
        const CameraProxy camera = MakeCamera(ProjectionType::Perspective);
        const float x10[3] = {-1.0f, 2.0f, 0.0f};
        const float x11[3] = {-0.9f, 2.1f, 0.1f};
        const float x12[3] = {-0.9f, 2.0f, 0.2f};

        RenderedObjectHistory objectHistory;
        FramePacket packet10;
        BuildPacket(packet10, 10u, {x10[0], x10[0]}, {x10[1], x10[1]}, {x10[2], x10[2]});
        const RenderedObjectHistoryResult first = objectHistory.Apply(packet10, true);
        Check(!first.bRebased && first.PreviousFrameNumber == 9u, "最初のパケットは付け替えず、直前のフレームを指す");

        FramePacket packet12;
        BuildPacket(packet12, 12u, {x12[0], x11[0]}, {x12[1], x11[1]}, {x12[2], x11[2]});
        FramePacket unrebased;
        BuildPacket(unrebased, 12u, {x12[0], x11[0]}, {x12[1], x11[1]}, {x12[2], x11[2]});
        const RenderedObjectHistoryResult result = objectHistory.Apply(packet12, true);
        Check(result.bRebased && result.PreviousFrameNumber == 10u && result.bComplete &&
                  result.UnresolvedInstanceCount == 0u,
              "11 を飛ばした 12 は、物体の前の変換を最後に描いた 10 へ付け替える");

        const char* labels[3] = {"moved_then_stopped", "moved_then_returned", "constant_speed"};
        for (uint32_t index = 0; index < 3u; ++index)
        {
            const Math::Vector3 current(x12[index], 0.5f, -10.0f);
            const float rebasedPrevious =
                PositionXOf(packet12.InstanceData[index].PreviousWorld, positions, positionCount);
            const float packetPrevious =
                PositionXOf(unrebased.InstanceData[index].PreviousWorld, positions, positionCount);
            // velocity（画素）= 今の点の投影 − 前の変換の点の投影。履歴の位置へ戻すには 10 の点との差が要る。
            const PixelMotion truth = MotionPx(camera, current, camera, Math::Vector3(x10[index], 0.5f, -10.0f));
            const PixelMotion rebased =
                MotionPx(camera, current, camera, Math::Vector3(rebasedPrevious, 0.5f, -10.0f));
            const PixelMotion packetVelocity =
                MotionPx(camera, current, camera, Math::Vector3(packetPrevious, 0.5f, -10.0f));
            std::printf("%s velocity_px truth=%+.4f rebased=%+.4f packet=%+.4f\n",
                        labels[index], truth.X, rebased.X, packetVelocity.X);
            Check(rebasedPrevious == x10[index], "前の変換は最後に描いたフレーム 10 の変換");
            Check(IsNear(rebased.X, truth.X, 1.0e-6) && IsNear(rebased.Y, truth.Y, 1.0e-6),
                  "付け替えた velocity は履歴の位置へ戻す");
            Check(std::fabs(packetVelocity.X - truth.X) > 1.0,
                  "パケットの velocity のままでは履歴の位置から1画素以上ずれる（比べる対照）");
        }
        Check(PositionXOf(packet12.Scene.MeshProxies[0].PreviousWorldTransform.values, positions, positionCount) ==
                  x10[0],
              "MeshProxy の前の変換も 10 へ付け替える");

        // 13 は 12 の直後: 付け替えず、パケットの前の変換（12）をそのまま使う。
        FramePacket packet13;
        BuildPacket(packet13, 13u, {x12[0], x12[0]}, {x12[1], x12[1]}, {0.3f, x12[2]});
        const RenderedObjectHistoryResult contiguous = objectHistory.Apply(packet13, true);
        Check(!contiguous.bRebased && contiguous.PreviousFrameNumber == 12u, "連続したフレームは付け替えない");

        // TAA を選ばないカメラ（bRewriteOnGap = false）では、飛んでもパケットを変えず、直前のフレームを指す。
        FramePacket packet15;
        BuildPacket(packet15, 15u, {x12[0], x11[0]}, {x12[1], x11[1]}, {x12[2], x11[2]});
        const RenderedObjectHistoryResult untouched = objectHistory.Apply(packet15, false);
        Check(!untouched.bRebased && untouched.PreviousFrameNumber == 14u, "TAA を選ばなければ付け替えない");
        Check(PositionXOf(packet15.InstanceData[0].PreviousWorld, positions, positionCount) == x11[0],
              "TAA を選ばなければ前の変換はパケットのまま");

        // MeshProxy と照合できないインスタンスがあれば、付け替えきれなかったとして TAA に履歴を捨てさせる。
        FramePacket packet17;
        BuildPacket(packet17, 17u, {x12[0], x11[0]}, {x12[1], x11[1]}, {x12[2], x11[2]});
        std::memcpy(packet17.InstanceData[2].World, WorldAt(9.0f).values, sizeof(packet17.InstanceData[2].World));
        const RenderedObjectHistoryResult unresolved = objectHistory.Apply(packet17, true);
        Check(unresolved.bRebased && !unresolved.bComplete && unresolved.UnresolvedInstanceCount == 1u,
              "照合できないインスタンスがあれば付け替えきれなかったとする");
    }

    void TestObjectAndCameraHistoryAcrossDroppedFrame()
    {
        // カメラも物体も動く: 履歴 10、11 を飛ばして 12。カメラは毎フレーム右へ 0.2 m、物体 A は 11 で動いて止まる。
        // 前のカメラは履歴のカメラ（SceneView が FindReprojectionCamera で差し替える）、物体の前の変換は
        // RenderedObjectHistory が 10 へ付け替えたもの。velocity は履歴の位置へ正確に戻る。
        const float positions[] = {-1.0f, -0.9f, 2.0f, 2.1f, 0.0f, 0.1f, 0.2f};
        const size_t positionCount = sizeof(positions) / sizeof(positions[0]);
        CameraProxy camera10 = MakeCamera(ProjectionType::Perspective);
        CameraProxy camera11 = camera10;
        CameraProxy camera12 = camera10;
        camera11.PositionX += 0.2f;
        camera12.PositionX += 0.4f;
        const TemporalAAJitter jitter = ComputeTemporalAAJitter(6u, Width, Height);

        TemporalAAHistoryTracker history;
        RenderedObjectHistory objectHistory;
        FramePacket packet10;
        BuildPacket(packet10, 10u, {-1.0f, -1.0f}, {2.0f, 2.0f}, {0.0f, 0.0f});
        const RenderedObjectHistoryResult first = objectHistory.Apply(packet10, true);
        history.Record(MakeHistoryQuery(10u, 0u, first.PreviousFrameNumber), camera10);

        FramePacket packet12;
        BuildPacket(packet12, 12u, {-0.9f, -0.9f}, {2.0f, 2.1f}, {0.2f, 0.1f});
        const RenderedObjectHistoryResult result = objectHistory.Apply(packet12, true);
        const TemporalAAHistoryQuery query = MakeHistoryQuery(12u, 0u, result.PreviousFrameNumber);
        Check(history.Evaluate(query) == TemporalAAHistoryDecision::Reuse, "付け替えた 12 は 10 の履歴を使う");

        // 今のカメラと前のカメラ（履歴のカメラ）に同じジッタを掛ける（SceneView と同じ）。
        const CameraProxy* reprojectionCamera = history.FindReprojectionCamera(0u, 7u, 0u, 12u);
        Check(reprojectionCamera != nullptr, "飛んだフレームでは履歴のカメラを前のカメラにする");
        if (!reprojectionCamera)
        {
            return;
        }
        CameraProxy previousCamera = *reprojectionCamera;
        ApplyTemporalAAJitter(previousCamera, jitter);
        CameraProxy jitteredCamera12 = camera12;
        ApplyTemporalAAJitter(jitteredCamera12, jitter);
        CameraProxy jitteredCamera10 = camera10;
        ApplyTemporalAAJitter(jitteredCamera10, jitter);
        CameraProxy jitteredCamera11 = camera11;
        ApplyTemporalAAJitter(jitteredCamera11, jitter);

        const Math::Vector3 current(-0.9f, 0.5f, -10.0f);
        const float rebasedPrevious = PositionXOf(packet12.InstanceData[0].PreviousWorld, positions, positionCount);
        const PixelMotion truth =
            MotionPx(jitteredCamera12, current, jitteredCamera10, Math::Vector3(-1.0f, 0.5f, -10.0f));
        const PixelMotion velocity =
            MotionPx(jitteredCamera12, current, previousCamera, Math::Vector3(rebasedPrevious, 0.5f, -10.0f));
        const PixelMotion packetVelocity =
            MotionPx(jitteredCamera12, current, jitteredCamera11, Math::Vector3(-0.9f, 0.5f, -10.0f));
        std::printf("camera_and_object velocity_px truth=(%+.4f, %+.4f) rebased=(%+.4f, %+.4f) packet=(%+.4f, %+.4f)\n",
                    truth.X, truth.Y, velocity.X, velocity.Y, packetVelocity.X, packetVelocity.Y);
        Check(IsNear(velocity.X, truth.X, 1.0e-3) && IsNear(velocity.Y, truth.Y, 1.0e-3),
              "カメラと物体が動いても、付け替えた velocity は履歴の位置へ戻す");
        Check(std::fabs(packetVelocity.X - truth.X) > 1.0,
              "パケットの前のカメラと前の変換のままでは1画素以上ずれる（比べる対照）");
    }

    void TestHistoryWithDroppedFrames(bool bRewriteOnGap)
    {
        // ゲームのフレーム 1〜30 のうち、RenderThread が 5・9・10・20 を描かずに捨てる。各フレームは
        // Viewport 0（TAA を掛ける）と Viewport 1（同じフレームの2つ目。TAA を掛けない）を描く。
        // RenderingCoordinator と TemporalAAPass と同じく、パケットの前の変換を付け替えてから履歴を使えるかを決め、
        // 働いたフレームで履歴を書く。物体 C は毎フレーム 0.1 m 動く。
        const auto isDropped = [](uint64_t frame) { return frame == 5u || frame == 9u || frame == 10u || frame == 20u; };
        const CameraProxy camera = MakeCamera(ProjectionType::Perspective);
        TemporalAAHistoryTracker history;
        RenderedObjectHistory objectHistory;
        uint32_t renderedCount = 0u;
        uint32_t reusedCount = 0u;
        uint32_t rebasedCount = 0u;
        uint32_t mismatchCount = 0u;
        for (uint64_t frame = 1u; frame <= 30u; ++frame)
        {
            if (isDropped(frame))
            {
                continue;
            }
            ++renderedCount;
            FramePacket packet;
            const float x = 0.1f * static_cast<float>(frame);
            BuildPacket(packet, frame, {-1.0f, -1.0f}, {2.0f, 2.0f}, {x, x - 0.1f});
            const RenderedObjectHistoryResult objectState = objectHistory.Apply(packet, bRewriteOnGap);
            const TemporalAAHistoryQuery query = MakeHistoryQuery(frame, 0u, objectState.PreviousFrameNumber);
            const TemporalAAHistoryDecision decision = history.Evaluate(query);
            if (decision == TemporalAAHistoryDecision::Reuse)
            {
                ++reusedCount;
                const bool bContiguous = history.IsContiguous(query);
                rebasedCount += bContiguous ? 0u : 1u;
                Check(bContiguous == !isDropped(frame - 1u), "直前のフレームが捨てられたときだけ連続でない");
            }
            mismatchCount += decision == TemporalAAHistoryDecision::ObjectStateMismatch ? 1u : 0u;
            history.Record(query, camera);
            // Viewport 1 は TAA を掛けない。
            history.NotifyViewportWithoutTemporalAA(1u);
        }
        std::printf("history rewrite=%d rendered=%u reused=%u rebased=%u object_state_mismatch=%u\n",
                    bRewriteOnGap ? 1 : 0, renderedCount, reusedCount, rebasedCount, mismatchCount);
        Check(renderedCount == 26u, "30 フレームのうち 26 を描く");
        if (bRewriteOnGap)
        {
            Check(reusedCount == 25u, "最初の1回のほかは、捨てたフレームをまたいでも履歴を使う");
            Check(rebasedCount == 3u, "捨てた並びの直後（6・11・21）の3回は前の変換を付け替えて使う");
            Check(mismatchCount == 0u, "付け替えたので基準の食い違いは無い");
        }
        else
        {
            Check(reusedCount == 22u, "付け替えなければ捨てた並びの直後の3回は履歴を使わない（比べる対照）");
            Check(mismatchCount == 3u, "付け替えなければ基準が食い違う（比べる対照）");
        }
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
    TestObjectHistoryAcrossDroppedFrame();
    TestObjectAndCameraHistoryAcrossDroppedFrame();
    TestHistoryWithDroppedFrames(true);
    TestHistoryWithDroppedFrames(false);

    if (GFailureCount != 0)
    {
        std::printf("TemporalAAJitterTest: 失敗 %d 件\n", GFailureCount);
        return 1;
    }
    std::printf("TemporalAAJitterTest passed\n");
    return 0;
}
