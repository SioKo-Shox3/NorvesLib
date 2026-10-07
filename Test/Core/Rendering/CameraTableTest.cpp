#include "Rendering/RenderingCoordinator.h"
#include "Rendering/TemporalAA.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    CameraProxy MakeCamera(float positionX)
    {
        CameraProxy camera;
        camera.PositionX = positionX;
        camera.Viewport.Width = 640.0f;
        camera.Viewport.Height = 360.0f;
        return camera;
    }
}

int main()
{
    std::cout << "CameraTableTest start\n";

    {
        RenderingCoordinator coordinator;
        assert(coordinator.FindCamera(0) == nullptr);

        CameraProxy first = MakeCamera(10.0f);
        first.CameraId = 777;
        const uint64_t firstId = coordinator.RegisterCamera(first);
        assert(firstId == 1);

        const CameraProxy *storedFirst = coordinator.FindCamera(firstId);
        assert(storedFirst != nullptr);
        assert(storedFirst->CameraId == firstId);
        assert(storedFirst->PositionX == 10.0f);

        const uint64_t secondId = coordinator.RegisterCamera(first);
        assert(secondId == 2);
        assert(secondId != firstId);
        assert(coordinator.FindCamera(secondId)->CameraId == secondId);

        CameraProxy update = MakeCamera(25.0f);
        update.CameraId = 0;
        assert(coordinator.UpdateCamera(firstId, update));
        storedFirst = coordinator.FindCamera(firstId);
        assert(storedFirst != nullptr);
        assert(storedFirst->CameraId == firstId);
        assert(storedFirst->PositionX == 25.0f);

        assert(!coordinator.UpdateCamera(0, update));
        assert(!coordinator.UpdateCamera(999, update));
        assert(coordinator.FindCamera(0) == nullptr);
        assert(coordinator.FindCamera(999) == nullptr);
    }

    {
        RenderingCoordinator coordinator;
        coordinator.SetMainCamera(MakeCamera(1.0f));
        assert(coordinator.GetMainCamera().CameraId == 1);
        const CameraProxy *firstMain = coordinator.FindCamera(1);
        assert(firstMain != nullptr);
        assert(firstMain->CameraId == 1);
        assert(firstMain->PositionX == 1.0f);

        CameraProxy replacement = MakeCamera(2.0f);
        replacement.CameraId = 999;
        coordinator.SetMainCamera(replacement);
        assert(coordinator.GetMainCamera().CameraId == 1);
        const CameraProxy *updatedMain = coordinator.FindCamera(1);
        assert(updatedMain != nullptr);
        assert(updatedMain->CameraId == 1);
        assert(updatedMain->PositionX == 2.0f);
        assert(coordinator.FindCamera(2) == nullptr);
    }

    {
        // SetMainCamera へ別の CameraComponent のカメラを渡すと、登録 ID は同じでも TAA の履歴を捨てる。
        RenderingCoordinator coordinator;
        CameraProxy first = MakeCamera(1.0f);
        first.CameraId = 41; // CameraComponent の ID
        coordinator.SetMainCamera(first);
        assert(coordinator.GetMainCamera().SourceCameraId == 41);

        TemporalAAHistoryQuery query;
        query.FrameNumber = 10;
        query.bHasPreviousCamera = true;
        query.PreviousObjectStateFrameNumber = 9;
        query.bPreviousObjectStateComplete = true;
        SetTemporalAAHistoryCamera(query, coordinator.GetMainCamera());
        TemporalAAHistoryTracker history;
        history.Record(query, coordinator.GetMainCamera());

        const auto evaluateNextFrame = [&](uint64_t frameNumber)
        {
            query.FrameNumber = frameNumber;
            query.PreviousObjectStateFrameNumber = frameNumber - 1;
            SetTemporalAAHistoryCamera(query, coordinator.GetMainCamera());
            return history.Evaluate(query);
        };

        // 同じカメラが動いただけなら使う。
        first.PositionX = 1.5f;
        coordinator.SetMainCamera(first);
        assert(evaluateNextFrame(11) == TemporalAAHistoryDecision::Reuse);
        // GetMainCamera の値を渡し直しても同じカメラのまま。
        coordinator.SetMainCamera(coordinator.GetMainCamera());
        assert(coordinator.GetMainCamera().SourceCameraId == 41);
        assert(evaluateNextFrame(11) == TemporalAAHistoryDecision::Reuse);

        // 同じ位置の別のカメラへ切り替える。
        CameraProxy second = MakeCamera(1.5f);
        second.CameraId = 42;
        coordinator.SetMainCamera(second);
        const CameraProxy &switched = coordinator.GetMainCamera();
        assert(switched.CameraId == 1);
        assert(switched.SourceCameraId == 42);
        assert(coordinator.FindCamera(1)->SourceCameraId == 42);
        assert(evaluateNextFrame(11) == TemporalAAHistoryDecision::CameraChanged);
        assert(history.FindReprojectionCamera(0, switched.CameraId, switched.SourceCameraId, 12) == nullptr);
    }

    std::cout << "CameraTableTest passed\n";
    return 0;
}
