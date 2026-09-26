#include "Rendering/DDGIVolume.h"
#include "Rendering/FramePacket.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RayTracingSceneSubsystem.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "Test/Core/Rendering/RenderingValidation/GpuTestEnvironment.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <type_traits>

using namespace NorvesLib::Core::Rendering;

namespace NorvesLib::Core::Rendering
{
    struct DDGISnapshotContractTestAccess
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

static_assert(std::is_trivially_copyable_v<DDGIVolumeParameters>);
static_assert(std::is_trivially_copyable_v<RayTracingHitMaterialSnapshot>);
static_assert(std::is_standard_layout_v<RayTracingHitMaterialSnapshot>);

namespace
{
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    int g_FailureCount = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    bool IsNear(float actual, float expected)
    {
        return std::fabs(actual - expected) <= 1.0e-6f;
    }

    DDGIVolumeParameters MakeActiveVolume()
    {
        DDGIVolumeParameters parameters = MakeDefaultDDGIVolumeParameters();
        parameters.bEnabled = true;
        parameters.Origin = NorvesLib::Math::Vector3(5.0f, -2.0f, 9.0f);
        parameters.ProbeSpacing = NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f);
        parameters.ProbeCountX = 2u;
        parameters.ProbeCountY = 2u;
        parameters.ProbeCountZ = 1u;
        return parameters;
    }

    void ExpectDefaultDisabled(const DDGIVolumeParameters& parameters)
    {
        const DDGIVolumeParameters defaults = MakeDefaultDDGIVolumeParameters();
        Expect(!parameters.bEnabled, "代替値でDDGIボリュームを無効化する");
        Expect(parameters.Origin == defaults.Origin &&
                   parameters.ProbeSpacing == defaults.ProbeSpacing &&
                   parameters.ProbeCountX == defaults.ProbeCountX &&
                   parameters.ProbeCountY == defaults.ProbeCountY &&
                   parameters.ProbeCountZ == defaults.ProbeCountZ,
               "代替値は有限な既定ボリュームへ戻る");
    }

    void TestRenderWorldVolumeValueSnapshotAndFallback()
    {
        DDGIVolumeParameters input = MakeActiveVolume();
        RenderWorld renderWorld;
        renderWorld.SetDDGIVolumeParameters(input);

        RenderingCoordinator& coordinator = renderWorld.GetRenderingCoordinator();
        const DDGIVolumeParameters configured = coordinator.GetDDGIVolumeParameters();
        Expect(IsDDGIVolumeValid(configured),
               "RenderWorldの設定が有効なボリューム値として保存される");
        Expect(configured.Origin == input.Origin &&
                   configured.ProbeSpacing == input.ProbeSpacing &&
                   configured.ProbeCountX == input.ProbeCountX &&
                   configured.ProbeCountY == input.ProbeCountY &&
                   configured.ProbeCountZ == input.ProbeCountZ,
               "RenderWorldのボリューム設定を値コピーする");

        input.Origin.x = 100.0f;
        Expect(IsNear(configured.Origin.x, 5.0f),
               "呼出元変更後もRenderWorld側の値は独立する");

        NorvesLib::RHI::DeviceCapabilities supportedCapabilities;
        supportedCapabilities.RayTracing.bAccelerationStructure = true;
        supportedCapabilities.RayTracing.bRayQuery = true;
        FramePacket packet;
        DDGISnapshotContractTestAccess::SnapshotSceneParameters(
            coordinator, packet, supportedCapabilities);
        Expect(IsNear(packet.Scene.DDGIVolume.Origin.x, 5.0f),
               "本番scene snapshot builderがRenderWorld設定をFramePacketへ値コピーする");
        Expect(packet.Scene.DDGIVolume.bEnabled &&
                   packet.Scene.DDGIVolume.ProbeSpacing == configured.ProbeSpacing &&
                   packet.Scene.DDGIVolume.ProbeCountX == configured.ProbeCountX &&
                   packet.Scene.DDGIVolume.ProbeCountY == configured.ProbeCountY &&
                   packet.Scene.DDGIVolume.ProbeCountZ == configured.ProbeCountZ,
               "ASとray query両対応時は設定値を維持する");

        DDGIVolumeParameters updatedInput = MakeActiveVolume();
        updatedInput.Origin.x = 15.0f;
        updatedInput.ProbeSpacing = NorvesLib::Math::Vector3(5.0f, 6.0f, 7.0f);
        updatedInput.ProbeCountX = 3u;
        renderWorld.SetDDGIVolumeParameters(updatedInput);

        const DDGIVolumeParameters configuredAfterUpdate =
            coordinator.GetDDGIVolumeParameters();
        FramePacket updatedPacket;
        DDGISnapshotContractTestAccess::SnapshotSceneParameters(
            coordinator, updatedPacket, supportedCapabilities);
        Expect(IsNear(configuredAfterUpdate.Origin.x, 15.0f) &&
                   configuredAfterUpdate.ProbeSpacing == updatedInput.ProbeSpacing &&
                   configuredAfterUpdate.ProbeCountX == updatedInput.ProbeCountX,
               "RenderWorld再設定後のvolume値をCoordinatorが受け取る");
        Expect(packet.Scene.DDGIVolume.Origin == configured.Origin &&
                   packet.Scene.DDGIVolume.ProbeSpacing == configured.ProbeSpacing &&
                   packet.Scene.DDGIVolume.ProbeCountX == configured.ProbeCountX,
               "Coordinator再設定後も既存FramePacketは旧volume値を保持する");
        Expect(IsNear(updatedPacket.Scene.DDGIVolume.Origin.x, 15.0f) &&
                   updatedPacket.Scene.DDGIVolume.ProbeSpacing == updatedInput.ProbeSpacing &&
                   updatedPacket.Scene.DDGIVolume.ProbeCountX == updatedInput.ProbeCountX,
               "新しいFramePacketには再設定後のvolume値をコピーする");
        NorvesLib::RHI::DeviceCapabilities noAccelerationStructure;
        noAccelerationStructure.RayTracing.bRayQuery = true;
        FramePacket noAccelerationStructurePacket;
        DDGISnapshotContractTestAccess::SnapshotSceneParameters(
            coordinator, noAccelerationStructurePacket, noAccelerationStructure);
        ExpectDefaultDisabled(noAccelerationStructurePacket.Scene.DDGIVolume);

        NorvesLib::RHI::DeviceCapabilities noRayQuery;
        noRayQuery.RayTracing.bAccelerationStructure = true;
        FramePacket noRayQueryPacket;
        DDGISnapshotContractTestAccess::SnapshotSceneParameters(
            coordinator, noRayQueryPacket, noRayQuery);
        ExpectDefaultDisabled(noRayQueryPacket.Scene.DDGIVolume);

        packet.Clear();
        ExpectDefaultDisabled(packet.Scene.DDGIVolume);
        updatedPacket.Clear();
        ExpectDefaultDisabled(updatedPacket.Scene.DDGIVolume);
    }

    DrawCommand MakeMeshDraw(MeshDataHandle meshHandle, MaterialHandle materialHandle)
    {
        DrawCommand command = DrawCommand::CreateDrawIndexed();
        command.Draw.MeshHandle = meshHandle;
        command.Draw.MaterialHandle = materialHandle;
        command.Draw.IndexCount = 3u;
        command.Draw.bCastShadow = true;
        command.Draw.MaterialBlendMode = BlendMode::Opaque;
        return command;
    }

    void SetOpaqueRange(FramePacket& packet)
    {
        packet.OpaqueCommandRange.First = 0u;
        packet.OpaqueCommandRange.Count =
            static_cast<std::uint32_t>(packet.DrawCommands.size());
        packet.DrawCommandRange = packet.OpaqueCommandRange;
    }

    int TestRayTracingMaterialValueSnapshot()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip("DDGISnapshotContractTest",
                                     "環境変数でGPUテストがスキップされました");
        }

        NorvesLib::Core::Container::String unavailableReason;
        if (!CanCreateVulkanDeviceForGpuTest(unavailableReason))
        {
            return ReportGpuTestSkip("DDGISnapshotContractTest", unavailableReason.c_str());
        }

        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = false;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip("DDGISnapshotContractTest", "Vulkanデバイスを利用できません");
        }

        MaterialCreateData defaultMaterial;
        Expect(defaultMaterial.BaseColor[0] == 1.0f &&
                   defaultMaterial.BaseColor[1] == 1.0f &&
                   defaultMaterial.BaseColor[2] == 1.0f &&
                   defaultMaterial.BaseColor[3] == 1.0f,
               "BaseColor既定値は白で既存描画の係数を変えない");

        RenderResources resources;
        if (!resources.Initialize(device))
        {
            std::cout << "失敗: 材質snapshot契約用の描画リソースを初期化する\n";
            return 1;
        }

        constexpr MeshDataHandle meshHandle{1u};
        Mesh3DVertex vertices[3]{};
        vertices[0].Position[0] = -1.0f;
        vertices[0].Position[1] = -1.0f;
        vertices[1].Position[0] = 1.0f;
        vertices[1].Position[1] = -1.0f;
        vertices[2].Position[1] = 1.0f;
        for (Mesh3DVertex& vertex : vertices)
        {
            vertex.Normal[2] = 1.0f;
        }
        constexpr std::uint32_t indices[3] = {0u, 1u, 2u};
        if (!resources.Meshes().Register(meshHandle, vertices, sizeof(vertices), indices, 3u))
        {
            std::cout << "失敗: 材質snapshot契約用のmeshを登録する\n";
            return 1;
        }

        MaterialCreateData createInfo;
        createInfo.BaseColor[0] = 0.25f;
        createInfo.BaseColor[1] = 0.5f;
        createInfo.BaseColor[2] = 0.75f;
        createInfo.BaseColor[3] = 0.8f;
        createInfo.EmissiveColor[0] = 0.25f;
        createInfo.EmissiveColor[1] = 0.5f;
        createInfo.EmissiveColor[2] = 0.75f;
        createInfo.EmissiveLuminanceNits = 12.0f;
        const MaterialHandle materialHandle = resources.Materials().Create(createInfo);
        Expect(materialHandle.IsValid(), "リニア材質値を持つマテリアルを作成する");
        if (!materialHandle.IsValid())
        {
            return 1;
        }

        createInfo.BaseColor[0] = 0.9f;
        createInfo.EmissiveColor[0] = 0.9f;

        FramePacket packet;
        packet.DrawCommands.push_back(MakeMeshDraw(meshHandle, materialHandle));
        SetOpaqueRange(packet);

        RayTracingSceneSubsystem subsystem;
        Expect(subsystem.BuildFrameSnapshot(
                   &resources.Meshes(), packet, &resources.Materials()),
               "DrawCommandからray tracing scene snapshotを構築する");
        Expect(packet.RayTracingScene.Instances.size() == 1u,
               "有効なDrawCommandから1件のray instanceを作る");
        if (packet.RayTracingScene.Instances.size() != 1u)
        {
            return 1;
        }

        const RayTracingHitMaterialSnapshot initialSnapshot =
            packet.RayTracingScene.Instances[0].Material;
        Expect(IsNear(initialSnapshot.BaseColor[0], 0.25f) &&
                   IsNear(initialSnapshot.BaseColor[1], 0.5f) &&
                   IsNear(initialSnapshot.BaseColor[2], 0.75f) &&
                   IsNear(initialSnapshot.BaseColor[3], 0.8f),
               "DrawCommandのMaterialHandleからリニアBaseColorをFramePacketへ値コピーする");
        Expect(IsNear(initialSnapshot.EmissiveLuminanceNits, 12.0f),
               "DrawCommandのMaterialHandleからemissive luminanceをFramePacketへ値コピーする");

        const MaterialResourceData* materialData = resources.Materials().GetData(materialHandle);
        Expect(materialData != nullptr, "保存した材質値を取得する");
        if (materialData == nullptr)
        {
            return 1;
        }
        const float expectedEmissiveRed = materialData->EmissiveColor[0];
        const float expectedEmissiveGreen = materialData->EmissiveColor[1];
        const float expectedEmissiveBlue = materialData->EmissiveColor[2];
        Expect(IsNear(initialSnapshot.EmissiveColor[0], expectedEmissiveRed) &&
                   IsNear(initialSnapshot.EmissiveColor[1], expectedEmissiveGreen) &&
                   IsNear(initialSnapshot.EmissiveColor[2], expectedEmissiveBlue),
               "FramePacketへシーンリニアのemissive色を値コピーする");

        MaterialCreateData updatedInfo;
        updatedInfo.BaseColor[0] = 0.9f;
        updatedInfo.BaseColor[1] = 0.8f;
        updatedInfo.BaseColor[2] = 0.7f;
        updatedInfo.BaseColor[3] = 0.6f;
        updatedInfo.EmissiveColor[0] = 0.75f;
        updatedInfo.EmissiveColor[1] = 0.5f;
        updatedInfo.EmissiveColor[2] = 0.25f;
        updatedInfo.EmissiveLuminanceNits = 24.0f;
        Expect(resources.Materials().Update(materialHandle, updatedInfo),
               "元材質を更新する");
        Expect(IsNear(packet.RayTracingScene.Instances[0].Material.BaseColor[0], 0.25f) &&
                   IsNear(packet.RayTracingScene.Instances[0].Material.EmissiveLuminanceNits, 12.0f),
               "元材質の更新後もFramePacketの既存snapshot値を保持する");

        Expect(subsystem.BuildFrameSnapshot(
                   &resources.Meshes(), packet, &resources.Materials()),
               "更新後の材質から次のray tracing scene snapshotを構築する");
        const RayTracingHitMaterialSnapshot updatedSnapshot =
            packet.RayTracingScene.Instances[0].Material;
        Expect(IsNear(updatedSnapshot.BaseColor[0], 0.9f) &&
                   IsNear(updatedSnapshot.BaseColor[3], 0.6f) &&
                   IsNear(updatedSnapshot.EmissiveLuminanceNits, 24.0f),
               "次のFramePacket snapshotは更新後のlinear材質値を保持する");

        resources.Materials().Release(materialHandle);
        Expect(resources.Materials().GetData(materialHandle) == nullptr,
               "元マテリアル解放後にresource storeから値が消える");
        Expect(IsNear(packet.RayTracingScene.Instances[0].Material.BaseColor[0],
                      updatedSnapshot.BaseColor[0]) &&
                   IsNear(packet.RayTracingScene.Instances[0].Material.EmissiveColor[0],
                          updatedSnapshot.EmissiveColor[0]) &&
                   IsNear(packet.RayTracingScene.Instances[0].Material.EmissiveLuminanceNits,
                          updatedSnapshot.EmissiveLuminanceNits),
               "元マテリアル解放後もFramePacketのlinear材質値を保持する");

        resources.Meshes().Unregister(meshHandle);
        subsystem.Shutdown();
        packet.Clear();
        Expect(packet.RayTracingScene.Instances.size() == 0u && packet.DrawCommands.empty(),
               "packet clear後にray-hit材質snapshotが残らない");
        resources.Shutdown();
        device->WaitIdle();
        return 0;
    }
}

int main()
{
    TestRenderWorldVolumeValueSnapshotAndFallback();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }

    const int materialTestResult = TestRayTracingMaterialValueSnapshot();
    if (materialTestResult != 0)
    {
        return materialTestResult;
    }

    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }

    std::cout << "DDGISnapshotContractTest: 本番snapshot経路の値所有契約を確認しました\n";
    return 0;
}