#include "Rendering/FramePacket.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    std::cout << "FramePacketManagerTest start\n";

    FramePacketManager manager;
    manager.Initialize();
    assert(manager.IsEmpty());

    FramePacket *packet = manager.AcquireForWrite();
    assert(packet != nullptr);
    assert(packet->GetState() == FramePacketState::Writing);
    packet->FrameNumber = 42;
    manager.FinishWrite(packet);
    assert(packet->GetState() == FramePacketState::Ready);
    assert(manager.GetReadyPacketCount() == 1);

    FramePacket *readPacket = manager.AcquireForRead();
    assert(readPacket == packet);
    assert(readPacket->GetState() == FramePacketState::Reading);
    manager.FinishRead(readPacket);
    assert(manager.IsEmpty());

    {
        FramePacket standalonePacket;
        ViewFrameSnapshot viewSnapshot;
        viewSnapshot.ViewId = 7;
        viewSnapshot.Priority = 3;
        ViewportSnapshot viewportSnapshot;
        viewportSnapshot.ViewId = 7;
        viewportSnapshot.ViewportId = 2;
        viewportSnapshot.RenderWidth = 640;
        viewportSnapshot.RenderHeight = 360;
        viewportSnapshot.PixelRect.Width = 640.0f;
        viewportSnapshot.PixelRect.Height = 360.0f;
        assert(viewportSnapshot.HasDrawableExtent());
        viewSnapshot.Viewports.push_back(viewportSnapshot);
        standalonePacket.Views.push_back(viewSnapshot);
        standalonePacket.Clear();
        assert(standalonePacket.Views.empty());
        assert(standalonePacket.DrawCommands.empty());
        assert(standalonePacket.OpaqueCommands.empty());
        assert(standalonePacket.TransparentCommands.empty());
    }

    packet = manager.AcquireForWrite();
    assert(packet != nullptr);
    manager.FinishWrite(packet);
    assert(manager.DrainUnconsumedPackets() == 1);
    assert(manager.IsEmpty());

    packet = manager.AcquireForWrite();
    assert(packet != nullptr);
    manager.FinishWrite(packet);
    assert(packet->CompareExchangeState(FramePacketState::Ready, FramePacketState::Queued));
    assert(manager.DrainUnconsumedPackets() == 0);
    assert(packet->GetState() == FramePacketState::Queued);
    assert(packet->CompareExchangeState(FramePacketState::Queued, FramePacketState::Reading));
    manager.FinishRead(packet);
    assert(manager.IsEmpty());

    std::cout << "FramePacketManagerTest passed\n";
    return 0;
}
