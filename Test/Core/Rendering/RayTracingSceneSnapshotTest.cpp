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
#include <cstring>
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

    void SetPreviousInstanceTransform(GPUSceneInstanceData& instance,
                                      float x, float y, float z)
    {
        std::memcpy(instance.PreviousWorld, instance.World,
                    sizeof(instance.PreviousWorld));
        instance.PreviousWorld[12] = x;
        instance.PreviousWorld[13] = y;
        instance.PreviousWorld[14] = z;
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

    // ジオメトリの区画への書き込みを、実デバイスのコマンドで完了させる（フレームごとに GPU の完了を待つ）
    bool DrainGeometryUploads(const RHI::DevicePtr& device, RenderResources& resources, uint64_t& serial)
    {
        for (uint32_t frame = 0; frame < 64u && resources.MegaGeometry().HasPendingGpuUploads(); ++frame)
        {
            CommandListPtr uploadCommandList = device->CreateCommandList();
            if (!uploadCommandList)
            {
                return false;
            }
            resources.BeginRetireFrame(serial);
            uploadCommandList->Begin();
            resources.RecordTileUploads(*uploadCommandList);
            uploadCommandList->End();
            uploadCommandList->Submit(true);
            ++serial;
            resources.CommitRetireFrame(serial);
            resources.BeginRetireFrame(serial);
        }
        return !resources.MegaGeometry().HasPendingGpuUploads();
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
        SetPreviousInstanceTransform(packet.InstanceData[0], -2.0f, 0.0f, 0.0f);
        SetPreviousInstanceTransform(packet.InstanceData[1], 0.0f, -3.0f, 0.0f);
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
            !IsNear(packet.RayTracingScene.Instances[1].Instance.transform[7], 3.0f) ||
            !firstInstance.bHasPreviousTransform ||
            !IsNear(firstInstance.PreviousTransform[3], -2.0f) ||
            !IsNear(packet.RayTracingScene.Instances[1].PreviousTransform[7], -3.0f))
        {
            std::cerr << "FramePacketにgeometryまたはinstance transformが正しくコピーされませんでした\n";
            return 1;
        }
        std::cout << "draw_snapshot_geometry_and_transforms=true\n";

        // 影を落とさない不透明物体もinstanceとして含め、maskで区別する（影・DDGI・RTGIは
        // 影を落とす物体のbitだけを調べ、パストレーサーは全bitを調べる）。半透明は含めない。
        {
            FramePacket maskPacket;
            DrawCommand caster = MakeMeshDraw(meshHandle);
            caster.Draw.InstanceCount = 1;
            DrawCommand nonCaster = MakeMeshDraw(meshHandle);
            nonCaster.Draw.InstanceCount = 1;
            nonCaster.Draw.InstanceDataOffset = 1;
            nonCaster.Draw.bCastShadow = false;
            DrawCommand translucent = MakeMeshDraw(meshHandle);
            translucent.Draw.InstanceCount = 1;
            translucent.Draw.InstanceDataOffset = 2;
            translucent.Draw.MaterialBlendMode = BlendMode::Translucent;
            maskPacket.DrawCommands.push_back(caster);
            maskPacket.DrawCommands.push_back(nonCaster);
            maskPacket.DrawCommands.push_back(translucent);
            maskPacket.InstanceData.resize(3);
            SetInstanceTransform(maskPacket.InstanceData[0], 1.0f, 0.0f, 0.0f);
            SetInstanceTransform(maskPacket.InstanceData[1], 0.0f, 5.0f, 0.0f);
            SetInstanceTransform(maskPacket.InstanceData[2], 0.0f, 0.0f, 7.0f);
            SetOpaqueRange(maskPacket);
            if (!subsystem.BuildFrameSnapshot(&renderResources.Meshes(), maskPacket) ||
                maskPacket.RayTracingScene.Instances.size() != 2 ||
                maskPacket.RayTracingScene.Instances[0].Instance.mask !=
                    RayTracingInstanceMaskShadowCaster ||
                maskPacket.RayTracingScene.Instances[1].Instance.mask !=
                    RayTracingInstanceMaskNonShadowCaster ||
                !IsNear(maskPacket.RayTracingScene.Instances[1].Instance.transform[7], 5.0f))
            {
                std::cerr << "影を落とす/落とさない不透明物体のinstance maskが正しくありません\n";
                return 1;
            }
            std::cout << "draw_snapshot_shadow_caster_masks=true\n";
        }

        // 焼き込み済みのLOD階層（NVMESH v1）のメッシュは、光線にフォールバックの段の範囲を見せる。
        // クラスタ（段0）の範囲（先頭の3インデックス）ではなく、その後ろに置かれたフォールバック（6インデックス）が
        // 同じバッファの範囲としてBLASの入力になる。
        {
            Mesh3DVertex bakedVertices[7]{};
            for (uint32_t i = 0; i < 7u; ++i)
            {
                bakedVertices[i].Position[0] = static_cast<float>(i);
                bakedVertices[i].Position[1] = static_cast<float>(i % 2u);
                bakedVertices[i].Normal[2] = 1.0f;
            }
            // クラスタのインデックス（頂点の基点からの相対）の後ろに、全体の頂点の番号で書いたフォールバック
            constexpr uint32_t bakedIndices[9] = {0, 1, 2, 3, 4, 5, 3, 5, 6};
            MegaGeometry::MegaMeshCreateInfo createInfo;
            createInfo.VertexData = bakedVertices;
            createInfo.VertexDataSize = sizeof(bakedVertices);
            createInfo.VertexCount = 7;
            createInfo.VertexStride = sizeof(Mesh3DVertex);
            createInfo.IndexData = bakedIndices;
            createInfo.IndexCount = 9;
            createInfo.bBuildLODHierarchy = false;
            createInfo.bBakedLODHierarchy = true;
            createInfo.BakedLODLevelCount = 1;
            createInfo.FallbackIndexOffset = 3;
            createInfo.FallbackIndexCount = 6;
            createInfo.FallbackError = 0.5f;
            createInfo.TotalBounds = BoundingSphere{3.0f, 0.5f, 0.0f, 4.0f};
            MegaGeometry::MeshCluster cluster;
            cluster.IndexOffset = 0;
            cluster.IndexCount = 3;
            cluster.VertexOffset = 0;
            cluster.VertexCount = 3;
            cluster.Bounds = createInfo.TotalBounds;
            createInfo.Clusters.push_back(cluster);
            createInfo.DebugName = "BakedFallbackRT";

            const MegaGeometry::MegaMeshHandle megaHandle = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
            const MegaGeometry::MegaMeshGPUData* megaData = renderResources.MegaGeometry().GetMegaMeshGPUData(megaHandle);
            if (!megaHandle.IsValid() || !megaData || megaData->ShadowFirstIndex != 3u ||
                megaData->ShadowIndexCount != 6u)
            {
                std::cerr << "焼き込み済みメッシュの影・RTの範囲がフォールバックの段になっていません\n";
                return 1;
            }

            FramePacket megaPacket;
            MegaGeometryProxy proxy;
            proxy.ObjectId = 7;
            proxy.MegaMeshHandle = megaHandle;
            megaPacket.Scene.MegaGeometryProxies.push_back(proxy);

            // 区画への書き込みが GPU で完了するまでは、BLAS の入力にしない（同期の BLAS 構築が未書き込みの区画を読まない）
            if (!subsystem.BuildFrameSnapshot(&renderResources.Meshes(),
                                              megaPacket,
                                              nullptr,
                                              &renderResources.MegaGeometry()) ||
                !megaPacket.RayTracingScene.Instances.empty())
            {
                std::cerr << "書き込み前のメッシュがinstanceになっています\n";
                return 1;
            }
            uint64_t uploadSerial = 0;
            if (!DrainGeometryUploads(device, renderResources, uploadSerial))
            {
                std::cerr << "メッシュの区画への書き込みが完了しませんでした\n";
                return 1;
            }
            std::cout << "mega_instance_waits_for_upload=true\n";

            if (!subsystem.BuildFrameSnapshot(&renderResources.Meshes(),
                                              megaPacket,
                                              nullptr,
                                              &renderResources.MegaGeometry()) ||
                megaPacket.RayTracingScene.Instances.size() != 1)
            {
                std::cerr << "焼き込み済みメッシュのinstanceを構築できませんでした\n";
                return 1;
            }
            const RayTracingSceneInstanceSnapshot& megaInstance = megaPacket.RayTracingScene.Instances[0];
            if (megaInstance.IndexOffset != 3u || megaInstance.IndexCount != 6u ||
                megaInstance.VertexOffset != 0u || megaInstance.VertexCount != 7u ||
                megaInstance.SourceVertexBuffer != megaData->VertexBuffer ||
                megaInstance.SourceIndexBuffer != megaData->IndexBuffer ||
                megaInstance.VertexBufferOffsetBytes != megaData->VertexBufferOffsetBytes ||
                megaInstance.IndexBufferOffsetBytes != megaData->IndexBufferOffsetBytes ||
                megaInstance.MegaMeshId != megaHandle.Id)
            {
                std::cerr << "焼き込み済みメッシュのinstanceがフォールバックの範囲を指していません\n";
                return 1;
            }
            std::cout << "mega_instance_points_into_pool_region=true\n";

            CommandListPtr megaCommandList = device->CreateCommandList();
            if (!megaCommandList || !BuildAndSubmit(device, *megaCommandList, subsystem, 0, megaPacket) ||
                !megaPacket.RayTracingScene.TopLevel || !megaPacket.RayTracingScene.Instances[0].BottomLevel)
            {
                std::cerr << "焼き込み済みメッシュのフォールバックの範囲でBLAS/TLASを構築できませんでした\n";
                return 1;
            }
            std::cout << "baked_mesh_instance_uses_fallback_range=true\n";

            megaPacket.Clear();
            megaCommandList.reset();
            renderResources.MegaGeometry().ReleaseMegaMesh(megaHandle);
        }

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
