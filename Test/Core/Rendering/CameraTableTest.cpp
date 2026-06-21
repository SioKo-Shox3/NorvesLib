#include "Rendering/RenderingCoordinator.h"

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

    std::cout << "CameraTableTest passed\n";
    return 0;
}
