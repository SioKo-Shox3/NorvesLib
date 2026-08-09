#include "Rendering/FramePacket.h"
#include <cassert>
#include <iostream>
#include <type_traits>

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
    packet->CaptureRequest = {42u, FrameCaptureSourceKind::SceneColor};
    manager.FinishWrite(packet);
    assert(packet->GetState() == FramePacketState::Ready);
    assert(manager.GetReadyPacketCount() == 1);

    FramePacket *readPacket = manager.AcquireForRead();
    assert(readPacket == packet);
    assert(readPacket->GetState() == FramePacketState::Reading);
    static_assert(std::is_same_v<decltype(readPacket->CaptureRequest), FrameCaptureRequestSnapshot>);
    assert(readPacket->CaptureRequest.RequestId == 42u);
    assert(readPacket->CaptureRequest.SourceKind == FrameCaptureSourceKind::SceneColor);
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
        standalonePacket.DrawCommands.push_back(DrawCommand::CreateDraw());
        standalonePacket.DrawCommands.push_back(DrawCommand::CreateDrawIndexed());
        standalonePacket.DrawCommands.push_back(DrawCommand::CreateDraw());
        DebugLineVertex debugLineVertex{};
        debugLineVertex.Position[0] = 1.0f;
        debugLineVertex.Color[3] = 1.0f;
        standalonePacket.DebugLineVertices.push_back(debugLineVertex);
        standalonePacket.OpaqueCommandRange = {0, 2};
        standalonePacket.TransparentCommandRange = {2, 1};
        standalonePacket.DrawCommandRange = {0, 3};
        standalonePacket.CaptureRequest = {42u, FrameCaptureSourceKind::SceneColor};

        DrawCommandView allCommands =
            DrawCommandView::FromRange(standalonePacket.DrawCommands, standalonePacket.DrawCommandRange);
        DrawCommandView opaqueCommands =
            DrawCommandView::FromRange(standalonePacket.DrawCommands, standalonePacket.OpaqueCommandRange);
        DrawCommandView transparentCommands =
            DrawCommandView::FromRange(standalonePacket.DrawCommands, standalonePacket.TransparentCommandRange);
        assert(allCommands.Data == standalonePacket.DrawCommands.data());
        assert(allCommands.Count == 3);
        assert(opaqueCommands.Data == standalonePacket.DrawCommands.data());
        assert(opaqueCommands.Count == 2);
        assert(transparentCommands.Data == standalonePacket.DrawCommands.data() + 2);
        assert(transparentCommands.Count == 1);

        standalonePacket.Clear();
        assert(standalonePacket.Views.empty());
        assert(standalonePacket.DrawCommands.empty());
        assert(standalonePacket.DebugLineVertices.empty());
        assert(standalonePacket.DrawCommandRange.IsEmpty());
        assert(standalonePacket.OpaqueCommandRange.IsEmpty());
        assert(standalonePacket.TransparentCommandRange.IsEmpty());
        assert(!standalonePacket.CaptureRequest.IsValid());
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
    packet->CaptureRequest = {77u, FrameCaptureSourceKind::SceneColor};
    assert(manager.DrainUnconsumedPackets() == 0);
    assert(packet->GetState() == FramePacketState::Queued);
    assert(packet->CompareExchangeState(FramePacketState::Queued, FramePacketState::Reading));
    assert(packet->CaptureRequest.RequestId == 77u);
    assert(packet->CaptureRequest.SourceKind == FrameCaptureSourceKind::SceneColor);
    manager.FinishRead(packet);
    assert(manager.IsEmpty());

    std::cout << "FramePacketManagerTest passed\n";
    return 0;
}
