// 空の遮蔽の値コピーと、専用の版が SceneRevision から独立する契約を確かめる。
#include "Rendering/FramePacket.h"
#include "Rendering/RenderWorld.h"
#include "RHI/DeviceCapabilities.h"

#include <cstdint>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;

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

    void ExpectOccluder(const FramePacket& packet, uint32_t index, const SkyOccluderProxy& expected)
    {
        Expect(index < packet.Scene.SkyOccluders.size(), "指定した順番に遮蔽物が存在する");
        if (index < packet.Scene.SkyOccluders.size())
        {
            Expect(AreEqualSkyOccluderProxy(packet.Scene.SkyOccluders[index], expected),
                   "遮蔽物の値が登録順に届く");
        }
    }

    void TestSkyOcclusionSnapshotAndSceneRevision()
    {
        RenderWorld renderWorld;
        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        FramePacket packet;
        const SkyOcclusionParameters defaults = MakeDefaultSkyOcclusionParameters();
        Expect(!packet.Scene.SkyOcclusion.bEnabled &&
                   AreEqualSkyOcclusionParameters(packet.Scene.SkyOcclusion, defaults),
               "既定のパケットの空の遮蔽は無効で既定値を持つ");
        Expect(packet.Scene.SkyOccluders.empty() && packet.Scene.SkyOcclusionRevision == 0u,
               "既定のパケットは遮蔽物を持たず、専用の版はゼロ");

        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packet);
        const uint64_t sceneHash = ComputeSceneRevisionHash(packet);
        uint64_t expectedRevision = 0u;
        auto snapshot = [&](bool bChanged)
        {
            if (bChanged)
            {
                ++expectedRevision;
            }
            SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packet);
            Expect(packet.Scene.SkyOcclusionRevision == expectedRevision,
                   "実際の変更だけが空の遮蔽の版を一度進める");
            Expect(ComputeSceneRevisionHash(packet) == sceneHash,
                   "空の遮蔽の変更は SceneRevision のハッシュを変えない");
            Expect(packet.Scene.SkyOccluders.size() == coordinator.GetSkyOccluderCount(),
                   "保持中の遮蔽物の数がパケットに届く");
        };
        snapshot(false);

        SkyOcclusionParameters parameters = defaults;
        parameters.bEnabled = true;
        parameters.Strength = 2.0f;
        parameters.DiffuseExponent = 3.0f;
        parameters.SpecularStrength = 0.5f;
        parameters.OccluderBounceColorR = 0.6f;
        parameters.ReceiverHeightMeters = 0.75f;
        parameters.GroundHeightMeters = -2.0f;
        parameters.LevelCount = 4u;
        parameters.BaseTexelMeters = 1.0f;
        parameters.Resolution = 128u;
        parameters.MaxHorizonMeters = 128.0f;
        parameters.bUseCenterOverride = true;
        parameters.CenterOverrideX = 12.0f;
        parameters.CenterOverrideZ = -15.0f;
        renderWorld.SetSkyOcclusionParameters(parameters);
        snapshot(true);
        Expect(AreEqualSkyOcclusionParameters(coordinator.GetSkyOcclusionParameters(), parameters) &&
                   AreEqualSkyOcclusionParameters(packet.Scene.SkyOcclusion, parameters),
               "RenderWorld で設定した値が coordinator とパケットへそのまま届く");
        renderWorld.SetSkyOcclusionParameters(parameters);
        snapshot(false);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        SkyOcclusionParameters invalidParameters = parameters;
        invalidParameters.Strength = nan;
        invalidParameters.DiffuseExponent = -1.0f;
        invalidParameters.Resolution = 33u;
        invalidParameters.CenterOverrideX = nan;
        SkyOcclusionParameters expectedParameters = parameters;
        expectedParameters.Strength = defaults.Strength;
        expectedParameters.DiffuseExponent = defaults.DiffuseExponent;
        expectedParameters.Resolution = defaults.Resolution;
        expectedParameters.CenterOverrideX = defaults.CenterOverrideX;
        renderWorld.SetSkyOcclusionParameters(invalidParameters);
        snapshot(true);
        Expect(AreEqualSkyOcclusionParameters(packet.Scene.SkyOcclusion, expectedParameters),
               "非有限値と範囲外の値は各欄の既定値に戻って届く");
        renderWorld.SetSkyOcclusionParameters(invalidParameters);
        snapshot(false);
        renderWorld.SetSkyOcclusionParameters(expectedParameters);
        snapshot(false);

        SkyOccluderProxy occluderA = MakeDefaultSkyOccluderProxy();
        occluderA.CenterX = 8.0f;
        occluderA.CenterY = 4.0f;
        occluderA.HalfExtentY = 3.0f;
        SkyOccluderProxy occluderB = MakeDefaultSkyOccluderProxy();
        occluderB.CenterZ = -10.0f;
        occluderB.HalfExtentX = 4.0f;
        occluderB.Opacity = 0.6f;
        SetSkyOccluderShape(occluderB, SkyOccluderShape::Box);
        const SkyOccluderHandle handleA = renderWorld.RegisterSkyOccluder(occluderA);
        Expect(handleA != 0u, "最初の登録は有効なハンドルを返す");
        snapshot(true);
        const SkyOccluderHandle handleB = renderWorld.RegisterSkyOccluder(occluderB);
        Expect(handleB != 0u && handleB != handleA, "次の登録は異なる有効なハンドルを返す");
        snapshot(true);
        Expect(packet.Scene.SkyOccluders.size() == 2u, "二つの遮蔽物が届く");
        ExpectOccluder(packet, 0u, occluderA);
        ExpectOccluder(packet, 1u, occluderB);
        FramePacket retainedPacket;
        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, retainedPacket);

        occluderA.CenterY = 6.0f;
        renderWorld.UpdateSkyOccluder(handleA, occluderA);
        snapshot(true);
        ExpectOccluder(packet, 0u, occluderA);
        ExpectOccluder(packet, 1u, occluderB);
        Expect(retainedPacket.Scene.SkyOccluders[0].CenterY == 4.0f &&
                   retainedPacket.Scene.SkyOcclusionRevision + 1u == packet.Scene.SkyOcclusionRevision,
               "以前のパケットは更新前の遮蔽物と版を値で保持する");
        renderWorld.UpdateSkyOccluder(handleA, occluderA);
        snapshot(false);
        // 予約欄と未知の印だけが異なる入力も、正規化後に同じなら版を進めない。
        SkyOccluderProxy equivalent = occluderA;
        equivalent.Flags |= 1u << 31u;
        equivalent.Reserved[0] = 99u;
        renderWorld.UpdateSkyOccluder(handleA, equivalent);
        snapshot(false);
        ExpectOccluder(packet, 0u, occluderA);

        renderWorld.UnregisterSkyOccluder(handleA);
        snapshot(true);
        Expect(packet.Scene.SkyOccluders.size() == 1u, "一つ解除すると残りは一つになる");
        ExpectOccluder(packet, 0u, occluderB);
        const SkyOccluderHandle handleC = renderWorld.RegisterSkyOccluder(equivalent);
        Expect(handleC != 0u && handleC != handleA && handleC != handleB,
               "解除済みのハンドルは再利用しない");
        snapshot(true);
        ExpectOccluder(packet, 0u, occluderB);
        ExpectOccluder(packet, 1u, occluderA);
        renderWorld.UnregisterSkyOccluder(handleA);
        renderWorld.UpdateSkyOccluder(handleA, occluderB);
        renderWorld.UnregisterSkyOccluder(0u);
        snapshot(false);
        Expect(packet.Scene.SkyOccluders.size() == 2u, "古いハンドルは新しい遮蔽物を変更も解除もしない");

        SkyOccluderProxy invalidOccluder = occluderA;
        invalidOccluder.HalfExtentX = nan;
        Expect(renderWorld.RegisterSkyOccluder(invalidOccluder) == 0u,
               "正規化後も無効な遮蔽物は登録しない");
        snapshot(false);
        renderWorld.UpdateSkyOccluder(handleC, invalidOccluder);
        snapshot(true);
        Expect(packet.Scene.SkyOccluders.size() == 1u, "無効な値への更新は解除として届く");
        ExpectOccluder(packet, 0u, occluderB);
        renderWorld.UnregisterSkyOccluder(handleB);
        snapshot(true);
        Expect(packet.Scene.SkyOccluders.empty(), "最後の解除は再利用パケットにも空の一覧を渡す");

        packet.Clear();
        Expect(AreEqualSkyOcclusionParameters(packet.Scene.SkyOcclusion, defaults) &&
                   packet.Scene.SkyOccluders.empty() && packet.Scene.SkyOcclusionRevision == 0u,
               "パケットをクリアすると空の遮蔽の全欄が既定に戻る");
        snapshot(false);
        Expect(AreEqualSkyOcclusionParameters(packet.Scene.SkyOcclusion, expectedParameters),
               "再利用後も保持していた設定がパケットへ届く");
        ExpectOccluder(retainedPacket, 1u, occluderB);

        renderWorld.SetStaticEnvironmentIntensityScale(2.0f);
        SkyOcclusionSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packet);
        Expect(packet.Scene.StaticEnvironmentIntensityScale == 2.0f &&
                   ComputeSceneRevisionHash(packet) != sceneHash,
               "対照の静的環境光の倍率は SceneRevision のハッシュを変える");
        Expect(packet.Scene.SkyOcclusionRevision == expectedRevision,
               "静的環境光の倍率は空の遮蔽の版を変えない");
    }
}

int main()
{
    TestSkyOcclusionSnapshotAndSceneRevision();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "SkyOcclusionSnapshotContractTest: 空の遮蔽の値コピーと独立した版の契約を確認しました\n";
    return 0;
}
