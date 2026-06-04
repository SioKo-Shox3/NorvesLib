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
