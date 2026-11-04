// 草の描画データの共有寿命と、草の差し替えが SceneRevision を変えない契約を確かめる。
#include "Rendering/FramePacket.h"
#include "Rendering/GrassFieldRenderData.h"
#include "Rendering/RenderWorld.h"
#include "RHI/DeviceCapabilities.h"

#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    struct GrassFieldSnapshotContractTestAccess
    {
        static void SnapshotSceneParameters(
            RenderingCoordinator& coordinator,
            FramePacket& packet,
            const NorvesLib::RHI::DeviceCapabilities& capabilities)
        {
            coordinator.SnapshotSceneParameters(packet, capabilities);
        }
    };
}

namespace
{
    constexpr uint32_t LayerAResolution = 64u;
    constexpr uint32_t LayerABytesPerChunk = LayerAResolution * LayerAResolution * 4u;
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message, const char* phase = nullptr)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message;
            if (phase != nullptr)
            {
                std::cout << "（" << phase << "）";
            }
            std::cout << "\n";
        }
    }

    Container::TSharedPtr<const GrassFieldRenderData> MakeGrassField(
        uint32_t chunkCount, uint8_t pixel, uint64_t revision)
    {
        auto field = Container::MakeShared<GrassFieldRenderData>();
        field->ContentRevision = revision;
        for (uint32_t index = 0; index < chunkCount; ++index)
        {
            GrassFieldChunkRenderData chunk;
            chunk.ChunkX = static_cast<int32_t>(index);
            chunk.LayerAByteOffset = index * LayerABytesPerChunk;
            field->Chunks.push_back(chunk);
        }
        field->LayerAPixels.resize(chunkCount * LayerABytesPerChunk, pixel);
        return field;
    }

    void ExpectGrassSnapshot(const FramePacket& packet,
                             const Container::TWeakPtr<const GrassFieldRenderData>& expectedField,
                             uint32_t expectedChunkCount,
                             uint8_t expectedPixel,
                             uint64_t expectedRevision,
                             const char* phase)
    {
        const Container::TSharedPtr<const GrassFieldRenderData> retainedField = expectedField.lock();
        Expect(static_cast<bool>(retainedField), "元の草データの共有所有権を保持する", phase);
        if (!retainedField)
        {
            return;
        }
        Expect(packet.GrassField.get() == retainedField.get(),
               "パケットは元の草と同じ実体を共有する", phase);
        if (packet.GrassField.get() != retainedField.get())
        {
            return;
        }

        const GrassFieldRenderData& field = *packet.GrassField;
        Expect(field.Chunks.size() == expectedChunkCount,
               "チャンク数を保持する", phase);
        Expect(field.LayerAResolution == LayerAResolution,
               "LayerA の解像度を保持する", phase);
        Expect(field.ContentRevision == expectedRevision,
               "内容の更新番号を保持する", phase);

        bool bPixelsMatch = field.LayerAPixels.size() == expectedChunkCount * LayerABytesPerChunk;
        for (const uint8_t pixel : field.LayerAPixels)
        {
            if (pixel != expectedPixel)
            {
                bPixelsMatch = false;
                break;
            }
        }
        Expect(bPixelsMatch, "LayerA の全画素を保持する", phase);
    }

    void TestGrassSnapshotAndSceneRevision()
    {
        RenderWorld renderWorld;
        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        const NorvesLib::RHI::DeviceCapabilities capabilities{};
        auto grassA = MakeGrassField(1u, 10u, 11u);
        auto grassB = MakeGrassField(2u, 200u, 22u);
        const Container::TWeakPtr<const GrassFieldRenderData> weakGrassA = grassA;
        const Container::TWeakPtr<const GrassFieldRenderData> weakGrassB = grassB;

        renderWorld.SetGrassField(grassA);
        Expect(coordinator.GetGrassField().get() == grassA.get(),
               "草 A が RenderWorld から coordinator へ渡る");
        FramePacket packetA;
        GrassFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packetA, capabilities);
        ExpectGrassSnapshot(packetA, weakGrassA, 1u, 10u, 11u, "草 A の設定直後");
        grassA.reset();

        renderWorld.SetGrassField(grassB);
        Expect(coordinator.GetGrassField().get() == grassB.get(),
               "草 B が RenderWorld から coordinator へ渡る");
        FramePacket packetB;
        GrassFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packetB, capabilities);
        ExpectGrassSnapshot(packetA, weakGrassA, 1u, 10u, 11u, "草 B への差し替え後のパケット 1");
        ExpectGrassSnapshot(packetB, weakGrassB, 2u, 200u, 22u, "草 B のパケット 2");
        grassB.reset();

        renderWorld.SetGrassField(nullptr);
        Expect(!coordinator.GetGrassField(), "草の解除が coordinator に反映される");
        FramePacket packetWithoutGrass;
        GrassFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, packetWithoutGrass, capabilities);
        Expect(!packetWithoutGrass.GrassField, "草の解除後のパケット 3 は空になる");
        ExpectGrassSnapshot(packetA, weakGrassA, 1u, 10u, 11u, "草の解除後のパケット 1");
        ExpectGrassSnapshot(packetB, weakGrassB, 2u, 200u, 22u, "草の解除後のパケット 2");

        const uint64_t hashA = ComputeSceneRevisionHash(packetA);
        Expect(ComputeSceneRevisionHash(packetB) == hashA,
               "草 A と草 B の差し替えで SceneRevision は変わらない");
        Expect(ComputeSceneRevisionHash(packetWithoutGrass) == hashA,
               "草の解除で SceneRevision は変わらない");

        // 草を同じに保ち、SceneRevision に含まれる静的環境光の倍率だけを変える。
        renderWorld.SetGrassField(packetA.GrassField);
        renderWorld.SetStaticEnvironmentIntensityScale(1.0f);
        FramePacket environmentBefore;
        GrassFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, environmentBefore, capabilities);
        renderWorld.SetStaticEnvironmentIntensityScale(2.0f);
        FramePacket environmentAfter;
        GrassFieldSnapshotContractTestAccess::SnapshotSceneParameters(coordinator, environmentAfter, capabilities);
        Expect(packetA.GrassField && environmentBefore.GrassField == packetA.GrassField &&
                   environmentAfter.GrassField == packetA.GrassField,
               "環境光の倍率を比べるパケットは同じ草を持つ");
        Expect(environmentBefore.Scene.StaticEnvironmentIntensityScale == 1.0f &&
                   environmentAfter.Scene.StaticEnvironmentIntensityScale == 2.0f,
               "環境光の倍率の変更が各パケットに反映される");
        Expect(ComputeSceneRevisionHash(environmentBefore) != ComputeSceneRevisionHash(environmentAfter),
               "静的環境光の倍率を変えると SceneRevision も変わる");
    }
}

int main()
{
    TestGrassSnapshotAndSceneRevision();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "GrassFieldSnapshotContractTest: 草の共有とシーン更新番号の契約を確認しました\n";
    return 0;
}
