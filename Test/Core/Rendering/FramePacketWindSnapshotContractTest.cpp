// GameThread の風の値コピーと、風が SceneRevision を変えない契約を CPU で反証する。
#include "Rendering/FramePacket.h"
#include "Rendering/RenderWorld.h"
#include "RHI/DeviceCapabilities.h"

#include <cstdint>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;

// Library を変更せず、既存試験の friend を同一の定義で共有する。
namespace NorvesLib::Core::Rendering
{
    struct SkyOcclusionSnapshotContractTestAccess
    {
        static void SnapshotSceneParameters(RenderingCoordinator& coordinator, FramePacket& packet)
        {
            const NorvesLib::RHI::DeviceCapabilities capabilities{};
            coordinator.SnapshotSceneParameters(packet, capabilities);
        }
    };
}

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

    bool EqualWind(const WindParameters& a, const WindParameters& b)
    {
        // 構造体のパディングを比べず、公開された全項目を比較する。
        return a.bEnabled == b.bEnabled && a.DirectionYawDegrees == b.DirectionYawDegrees &&
               a.BaseSpeed == b.BaseSpeed && a.GustStrength == b.GustStrength &&
               a.GustFrequencyHz == b.GustFrequencyHz && a.GustScaleMeters == b.GustScaleMeters &&
               a.Turbulence == b.Turbulence;
    }

    void TestWindSnapshotAndSceneRevision()
    {
        RenderWorld renderWorld;
        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        const WindParameters defaults = MakeDefaultWindParameters();
        WindParameters windA = defaults;
        windA.bEnabled = true;
        windA.DirectionYawDegrees = 37.0f;
        windA.BaseSpeed = 7.5f;
        windA.GustStrength = 0.625f;
        windA.GustFrequencyHz = 0.25f;
        windA.GustScaleMeters = 17.0f;
        windA.Turbulence = 0.375f;
        WindParameters windB = defaults;
        windB.DirectionYawDegrees = 213.0f;
        windB.BaseSpeed = 22.0f;
        windB.GustStrength = 0.875f;
        windB.GustFrequencyHz = 2.0f;
        windB.GustScaleMeters = 123.0f;
        windB.Turbulence = 0.75f;

        FramePacket packetA;
        Expect(EqualWind(packetA.Scene.Wind, defaults), "既定のパケットは既定の風を持つ");
        renderWorld.SetWindParameters(windA);
        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packetA);
        Expect(EqualWind(packetA.Scene.Wind, windA), "RenderWorld で設定した全項目がパケット A へ届く");
        const uint64_t hashA = ComputeSceneRevisionHash(packetA);

        renderWorld.SetWindParameters(windB);
        Expect(EqualWind(packetA.Scene.Wind, windA), "GameThread の設定を変えてもパケット A は元の風を保持する");
        FramePacket packetB;
        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packetB);
        Expect(EqualWind(packetB.Scene.Wind, windB), "変更後の全項目がパケット B へ届く");
        Expect(!EqualWind(packetA.Scene.Wind, packetB.Scene.Wind), "比較するパケットの風は本当に異なる");
        Expect(EqualWind(packetA.Scene.Wind, windA), "次のパケットを作った後もパケット A の風は変わらない");
        const uint64_t hashB = ComputeSceneRevisionHash(packetB);
        Expect(hashA == hashB, "風だけを変えても SceneRevision のハッシュは変わらない");

        // 有限な他の項目を維持したまま、NaN は経路上で項目の既定値へ戻る。
        WindParameters invalid = windA;
        invalid.BaseSpeed = std::numeric_limits<float>::quiet_NaN();
        WindParameters expected = windA;
        expected.BaseSpeed = defaults.BaseSpeed;
        renderWorld.SetWindParameters(invalid);
        FramePacket sanitizedPacket;
        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, sanitizedPacket);
        Expect(EqualWind(sanitizedPacket.Scene.Wind, expected), "パケット経路は NaN の項目だけを既定値へ戻す");
        Expect(EqualWind(packetA.Scene.Wind, windA) && EqualWind(packetB.Scene.Wind, windB),
               "不正値を再設定しても保持済みの二つのパケットは変わらない");
        Expect(ComputeSceneRevisionHash(sanitizedPacket) == hashA, "正規化された風も SceneRevision を変えない");

        // 対照として静的環境光だけを変え、ハッシュの検査自体が機能することを確かめる。
        renderWorld.SetStaticEnvironmentIntensityScale(2.0f);
        FramePacket environmentPacket;
        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, environmentPacket);
        Expect(EqualWind(environmentPacket.Scene.Wind, sanitizedPacket.Scene.Wind), "対照パケットの風は同じ値に保つ");
        Expect(environmentPacket.Scene.StaticEnvironmentIntensityScale == 2.0f &&
                   sanitizedPacket.Scene.StaticEnvironmentIntensityScale != 2.0f,
               "対照パケットには異なる静的環境光の倍率が届く");
        const uint64_t environmentHash = ComputeSceneRevisionHash(environmentPacket);
        Expect(environmentHash != hashA, "対照の静的環境光を変えると SceneRevision は変わる");
        std::cout << "Snapshot: hash_a=" << hashA << " hash_b=" << hashB
                  << " environment_hash=" << environmentHash
                  << " base_speed_a=" << packetA.Scene.Wind.BaseSpeed
                  << " base_speed_b=" << packetB.Scene.Wind.BaseSpeed
                  << " sanitized_base_speed=" << sanitizedPacket.Scene.Wind.BaseSpeed << "\n";
    }
}

int main()
{
    TestWindSnapshotAndSceneRevision();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "FramePacketWindSnapshotContractTest: 風の値コピーとシーン更新番号の契約を確認しました\n";
    return 0;
}
