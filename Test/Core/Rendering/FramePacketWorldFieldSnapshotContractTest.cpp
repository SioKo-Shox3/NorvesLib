// 世界フィールド入力の値コピー、寿命、シーン更新番号からの独立を確かめる。
#include "Rendering/FramePacket.h"
#include "Rendering/RenderWorld.h"
#include "RHI/DeviceCapabilities.h"

#include <cstdint>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;

namespace NorvesLib::Core::Rendering
{
    struct FramePacketWorldFieldSnapshotContractTestAccess
    {
        static void SnapshotSceneParameters(RenderingCoordinator& coordinator, FramePacket& packet)
        {
            const NorvesLib::RHI::DeviceCapabilities capabilities{};
            coordinator.SnapshotSceneParameters(packet, capabilities);
        }

        static bool InputEmpty(const RenderingCoordinator& coordinator)
        {
            return coordinator.m_WorldFieldInteractors.empty();
        }

        static auto InputCapacity(const RenderingCoordinator& coordinator)
        {
            return coordinator.m_WorldFieldInteractors.capacity();
        }

        static bool EndFrameWithoutPacket(RenderingCoordinator& coordinator)
        {
            coordinator.m_bInitialized = true;
            const bool bNoPacket = coordinator.EndFrame() == nullptr;
            coordinator.m_bInitialized = false;
            return bNoPacket;
        }
    };
} // 名前空間 NorvesLib::Core::Rendering

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

    WorldFieldInteractor MakeInteractor(uint32_t index)
    {
        WorldFieldInteractor interactor;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const float value = static_cast<float>(index * 10u + axis);
            interactor.P0[axis] = value;
            interactor.P1[axis] = value + 1.0f;
            interactor.PrevP0[axis] = value + 2.0f;
            interactor.PrevP1[axis] = value + 3.0f;
        }
        interactor.Radius = 2.0f;
        interactor.PrevRadius = 1.0f;
        interactor.Strength = 0.5f;
        interactor.ChannelMask = WorldFieldChannelBend | WorldFieldChannelTrail;
        return interactor;
    }

    bool EqualInteractors(const WorldFieldInteractor& left, const WorldFieldInteractor& right)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (left.P0[axis] != right.P0[axis] || left.P1[axis] != right.P1[axis] ||
                left.PrevP0[axis] != right.PrevP0[axis] || left.PrevP1[axis] != right.PrevP1[axis])
            {
                return false;
            }
        }
        return left.Radius == right.Radius && left.PrevRadius == right.PrevRadius &&
               left.Strength == right.Strength && left.ChannelMask == right.ChannelMask;
    }

    void ExpectOriginalValues(const FramePacket& packet, const WorldFieldInteractor (&expected)[3])
    {
        Expect(packet.WorldFieldInteractors.size() == 3u, "パケットが入力を 3 件保持する");
        if (packet.WorldFieldInteractors.size() != 3u)
        {
            return;
        }
        for (uint32_t index = 0; index < 3u; ++index)
        {
            Expect(EqualInteractors(packet.WorldFieldInteractors[index], expected[index]),
                   "各入力の全フィールドが順番どおりに保持される");
        }
    }

    void TestSnapshotAndSceneRevision()
    {
        RenderWorld renderWorld;
        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        WorldFieldInteractor original[3] = {MakeInteractor(1u), MakeInteractor(2u), MakeInteractor(3u)};
        const WorldFieldInteractor expected[3] = {original[0], original[1], original[2]};
        WorldFieldInteractor replaced = MakeInteractor(9u);

        renderWorld.SetWorldFieldInteractors({&replaced, 1u});
        renderWorld.SetWorldFieldInteractors(original);
        original[0].P0[0] = -100.0f;
        const auto inputCapacity = FramePacketWorldFieldSnapshotContractTestAccess::InputCapacity(coordinator);

        FramePacket packet;
        packet.WorldFieldInteractors.reserve(8u);
        const auto packetCapacity = packet.WorldFieldInteractors.capacity();
        FramePacketWorldFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packet);
        ExpectOriginalValues(packet, expected);
        Expect(FramePacketWorldFieldSnapshotContractTestAccess::InputEmpty(coordinator),
               "コピー後は coordinator の入力が空になる");
        Expect(FramePacketWorldFieldSnapshotContractTestAccess::InputCapacity(coordinator) == inputCapacity &&
                   packet.WorldFieldInteractors.capacity() == packetCapacity,
               "コピー後も入力側とパケット側の容量を保持する");

        FramePacket nextPacket;
        nextPacket.WorldFieldInteractors.push_back(replaced);
        const auto nextPacketCapacity = nextPacket.WorldFieldInteractors.capacity();
        FramePacketWorldFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, nextPacket);
        Expect(nextPacket.WorldFieldInteractors.empty(), "次のパケットでは以前の入力が消える");
        Expect(nextPacket.WorldFieldInteractors.capacity() == nextPacketCapacity,
               "空の入力でもパケットの容量を保持する");
        Expect(ComputeSceneRevisionHash(packet) == ComputeSceneRevisionHash(nextPacket),
               "世界フィールド入力の有無で SceneRevision は変わらない");

        original[1] = MakeInteractor(20u);
        replaced = MakeInteractor(30u);
        coordinator.SetWorldFieldInteractors({&replaced, 1u});
        ExpectOriginalValues(packet, expected);
        Expect(FramePacketWorldFieldSnapshotContractTestAccess::InputCapacity(coordinator) == inputCapacity,
               "次の入力でも coordinator の容量を再利用する");
        coordinator.SetWorldFieldInteractors({});
        FramePacketWorldFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, nextPacket);
        Expect(nextPacket.WorldFieldInteractors.empty(), "同じフレームの空入力で直前の入力を置き換える");
        ExpectOriginalValues(packet, expected);
    }

    void TestSanitization()
    {
        RenderWorld renderWorld;
        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        WorldFieldInteractor inputs[40];
        for (uint32_t index = 0; index < 40u; ++index)
        {
            inputs[index] = MakeInteractor(index);
        }
        inputs[0].Radius = 100.0f;
        inputs[0].Strength = 2.0f;
        inputs[0].ChannelMask = 0xffffffffu;
        renderWorld.SetWorldFieldInteractors(inputs);
        FramePacket packet;
        FramePacketWorldFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packet);
        Expect(packet.WorldFieldInteractors.size() == WorldFieldMaxInteractors,
               "40 件の入力は先頭 32 件に制限される");
        if (packet.WorldFieldInteractors.size() == WorldFieldMaxInteractors)
        {
            Expect(packet.WorldFieldInteractors[0].Radius == 50.0f &&
                       packet.WorldFieldInteractors[0].Strength == 1.0f &&
                       packet.WorldFieldInteractors[0].ChannelMask == WorldFieldChannelAll,
                   "半径、強さ、チャンネルの無害化がパケットへ反映される");
            Expect(EqualInteractors(packet.WorldFieldInteractors.back(), inputs[31]),
                   "32 件目が最後に残る");
        }

        inputs[7].P1[0] = std::numeric_limits<float>::quiet_NaN();
        renderWorld.SetWorldFieldInteractors(inputs);
        FramePacketWorldFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packet);
        Expect(packet.WorldFieldInteractors.size() == WorldFieldMaxInteractors - 1u,
               "先頭 32 件の NaN を除き、33 件目以降は追加しない");
        if (packet.WorldFieldInteractors.size() == WorldFieldMaxInteractors - 1u)
        {
            Expect(EqualInteractors(packet.WorldFieldInteractors[7], inputs[8]) &&
                       EqualInteractors(packet.WorldFieldInteractors.back(), inputs[31]),
                   "不正な入力だけを除き、残りの順番を保つ");
        }
        Expect(FramePacketWorldFieldSnapshotContractTestAccess::InputEmpty(coordinator),
               "無害化した入力もコピー後に消える");
    }

    void TestSkippedPacket()
    {
        RenderWorld renderWorld;
        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        WorldFieldInteractor input = MakeInteractor(1u);
        renderWorld.SetWorldFieldInteractors({&input, 1u});
        Expect(!FramePacketWorldFieldSnapshotContractTestAccess::InputEmpty(coordinator),
               "パケットを確保できなかったフレームにも入力を受け付ける");
        Expect(FramePacketWorldFieldSnapshotContractTestAccess::EndFrameWithoutPacket(coordinator),
               "パケットが無いフレームは空で終了する");
        Expect(FramePacketWorldFieldSnapshotContractTestAccess::InputEmpty(coordinator),
               "パケットが無いフレームの入力を持ち越さない");
        FramePacket nextPacket;
        FramePacketWorldFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, nextPacket);
        Expect(nextPacket.WorldFieldInteractors.empty(), "次のパケットに古い入力を載せない");
    }
}

int main()
{
    TestSnapshotAndSceneRevision();
    TestSanitization();
    TestSkippedPacket();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "FramePacketWorldFieldSnapshotContractTest: 世界フィールド入力の契約を確認しました\n";
    return 0;
}
