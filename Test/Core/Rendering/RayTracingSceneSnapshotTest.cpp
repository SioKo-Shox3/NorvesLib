#include "Engine/NorvesEngine.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RayTracingSceneSubsystem.h"
#include "Rendering/RenderResources.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RayTracingSceneSnapshotTest";

    bool IsNear(float actual, float expected)
    {
        return std::fabs(actual - expected) < 0.0001f;
    }

    void SetInstanceTransform(GPUSceneInstanceData& instance, float x, float y, float z)
    {
        instance.World[0] = 1.0f;
        instance.World[5] = 1.0f;
        instance.World[10] = 1.0f;
        instance.World[15] = 1.0f;
        instance.World[12] = x;
        instance.World[13] = y;
        instance.World[14] = z;
    }

    DrawCommand MakeMeshDraw(MeshDataHandle meshHandle)
    {
        DrawCommand command = DrawCommand::CreateDrawIndexed();
        command.Type = DrawCommandType::DrawIndexedInstanced;
        command.Draw.MeshHandle = meshHandle;
        command.Draw.IndexOffset = 0;
        command.Draw.IndexCount = 3;
        command.Draw.VertexOffset = 0;
        command.Draw.InstanceCount = 2;
        command.Draw.bInstanced = true;
        command.Draw.bCastShadow = true;
        command.Draw.MaterialBlendMode = BlendMode::Opaque;
        return command;
    }

    void SetOpaqueRange(FramePacket& packet)
    {
        packet.OpaqueCommandRange.First = 0;
        packet.OpaqueCommandRange.Count = static_cast<uint32_t>(packet.DrawCommands.size());
        packet.DrawCommandRange = packet.OpaqueCommandRange;
    }

    bool BuildAndSubmit(const RHI::DevicePtr& device,
                        RHI::ICommandList& commandList,
                        RayTracingSceneSubsystem& subsystem,
                        uint32_t frameSlot,
                        FramePacket& packet)
    {
        commandList.SetFrameIndex(frameSlot);
        commandList.Begin();
        const bool bBuilt = subsystem.BuildAccelerationStructures(
            device,
            commandList,
            frameSlot,
            packet);
        commandList.End();
        if (!bBuilt)
        {
            return false;
        }

        commandList.Submit(true);
        return true;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "環境変数でGPUテストがスキップされました");
        }

        String unavailableReason;
        if (!CanCreateVulkanDeviceForGpuTest(unavailableReason))
        {
            return ReportGpuTestSkip(TestName, unavailableReason.c_str());
        }

        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = false;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }
        if (!device->GetCapabilities().RayTracing.bAccelerationStructure)
        {
            return ReportGpuTestSkip(TestName, "加速構造を利用できません");
        }

        RenderResources renderResources;
        if (!renderResources.Initialize(device))
        {
            std::cerr << "描画リソースを初期化できませんでした\n";
            return 1;
        }

        constexpr MeshDataHandle meshHandle{1};
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
        constexpr uint32_t indices[3] = {0, 1, 2};
        if (!renderResources.Meshes().Register(meshHandle,
                                               vertices,
                                               sizeof(vertices),
                                               indices,
                                               3))
        {
            std::cerr << "契約fixtureのmeshを登録できませんでした\n";
            return 1;
        }

        RayTracingSceneSubsystem& subsystem = GEngine.GetRayTracingSceneSubsystem();
        FramePacket packet;
        packet.DrawCommands.push_back(MakeMeshDraw(meshHandle));
        packet.InstanceData.resize(2);
        SetInstanceTransform(packet.InstanceData[0], 2.0f, 0.0f, 0.0f);
        SetInstanceTransform(packet.InstanceData[1], 0.0f, 3.0f, 0.0f);
        SetOpaqueRange(packet);
        if (!subsystem.BuildFrameSnapshot(&renderResources.Meshes(), packet) ||
            packet.RayTracingScene.Instances.size() != 2)
        {
            std::cerr << "draw snapshotから2件のmesh instanceを構築できませんでした\n";
            return 1;
        }

        const RayTracingSceneInstanceSnapshot& firstInstance = packet.RayTracingScene.Instances[0];
        if (!firstInstance.SourceVertexBuffer || !firstInstance.SourceIndexBuffer ||
            firstInstance.IndexCount != 3 || firstInstance.VertexCount != 3 ||
            firstInstance.VertexStride != sizeof(Mesh3DVertex) ||
            !IsNear(firstInstance.Instance.transform[3], 2.0f) ||
            !IsNear(packet.RayTracingScene.Instances[1].Instance.transform[7], 3.0f))
        {
            std::cerr << "FramePacketにgeometryまたはinstance transformが正しくコピーされませんでした\n";
            return 1;
        }
        std::cout << "draw_snapshot_geometry_and_transforms=true\n";

        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList || !BuildAndSubmit(device, *commandList, subsystem, 0, packet))
        {
            std::cerr << "初回BLAS/TLAS buildを記録できませんでした\n";
            return 1;
        }
        if (!packet.RayTracingScene.TopLevel ||
            !packet.RayTracingScene.Instances[0].BottomLevel ||
            packet.RayTracingScene.Instances[0].BottomLevel !=
                packet.RayTracingScene.Instances[1].BottomLevel)
        {
            std::cerr << "mesh単位のBLASまたはinstance TLASが作成されませんでした\n";
            return 1;
        }

        const IAccelerationStructure* const initialBottomLevel =
            packet.RayTracingScene.Instances[0].BottomLevel.get();
        const IAccelerationStructure* const initialTopLevel = packet.RayTracingScene.TopLevel.get();
        TWeakPtr<IAccelerationStructure> bottomLevelLifetime =
            packet.RayTracingScene.Instances[0].BottomLevel;
        TWeakPtr<IAccelerationStructure> topLevelLifetime = packet.RayTracingScene.TopLevel;
        TWeakPtr<IBuffer> vertexBufferLifetime = firstInstance.SourceVertexBuffer;
        TWeakPtr<IBuffer> indexBufferLifetime = firstInstance.SourceIndexBuffer;
        std::cout << "initial_blas_tlas_created=true\n";

        packet.Clear();
        packet.DrawCommands.push_back(MakeMeshDraw(meshHandle));
        packet.InstanceData.resize(2);
        SetInstanceTransform(packet.InstanceData[0], 5.0f, 0.0f, 0.0f);
        SetInstanceTransform(packet.InstanceData[1], 0.0f, 6.0f, 0.0f);
        SetOpaqueRange(packet);
        if (!subsystem.BuildFrameSnapshot(&renderResources.Meshes(), packet) ||
            !IsNear(packet.RayTracingScene.Instances[0].Instance.transform[3], 5.0f) ||
            !IsNear(packet.RayTracingScene.Instances[1].Instance.transform[7], 6.0f) ||
            !BuildAndSubmit(device, *commandList, subsystem, 0, packet))
        {
            std::cerr << "更新snapshotからTLAS updateを記録できませんでした\n";
            return 1;
        }
        if (packet.RayTracingScene.Instances[0].BottomLevel.get() != initialBottomLevel ||
            packet.RayTracingScene.TopLevel.get() != initialTopLevel)
        {
            std::cerr << "同一geometry・instance数の更新で加速構造が再利用されませんでした\n";
            return 1;
        }
        std::cout << "tlas_update_reused_cached_structure=true\n";

        renderResources.Meshes().Unregister(meshHandle);
        subsystem.Shutdown();
        if (bottomLevelLifetime.expired() || topLevelLifetime.expired() ||
            vertexBufferLifetime.expired() || indexBufferLifetime.expired())
        {
            std::cerr << "FramePacketが読み取り完了前のRHI資源を保持できませんでした\n";
            return 1;
        }

        packet.Clear();
        commandList.reset();
        renderResources.Shutdown();
        device->WaitIdle();
        if (!bottomLevelLifetime.expired() || !topLevelLifetime.expired() ||
            !vertexBufferLifetime.expired() || !indexBufferLifetime.expired())
        {
            std::cerr << "packet clear後にmeshまたは加速構造の参照が残りました\n";
            return 1;
        }
        std::cout << "packet_and_subsystem_resource_lifetimes_released=true\n";
        return 0;
    }
}

int main()
{
    return RunTest();
}
