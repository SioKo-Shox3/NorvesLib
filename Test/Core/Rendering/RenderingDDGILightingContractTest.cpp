#include "Rendering/ViewRenderContext.h"

#include "Container/Containers.h"
#include "RHI/RHITypes.h"
#include "Rendering/FramePacket.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RTGIContract.h"

#define private public
#include "Rendering/DDGIProbePass.h"
#undef private

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    class RTGIContractTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        RTGIContractTexture(uint32_t width, uint32_t height, NorvesLib::RHI::Format format)
            : m_Width(width)
            , m_Height(height)
            , m_Format(format)
        {
        }

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetDepth() const override { return 1u; }
        uint32_t GetMipLevels() const override { return 1u; }
        uint32_t GetArraySize() const override { return 1u; }
        NorvesLib::RHI::Format GetFormat() const override { return m_Format; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override
        {
            return NorvesLib::RHI::ResourceUsage::ShaderRead;
        }
        bool IsCubemap() const override { return false; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

    private:
        uint32_t m_Width = 0u;
        uint32_t m_Height = 0u;
        NorvesLib::RHI::Format m_Format = NorvesLib::RHI::Format::UNKNOWN;
    };

    NorvesLib::RHI::TexturePtr MakeRTGIContractTexture(uint32_t width,
                                                        uint32_t height,
                                                        NorvesLib::RHI::Format format)
    {
        return NorvesLib::RHI::MakeShared<RTGIContractTexture>(width, height, format);
    }

    void TestDDGIAtlasPublicationReset()
    {
        PhysicalLightingResources resources;
        resources.Begin(17u, 3u, 5u);
        assert(resources.bActive);
        assert(!resources.bDDGIAtlasPublished);
        assert(resources.DDGIProbeCount == 0u);
        assert(!resources.DDGIIrradianceAtlas);
        assert(!resources.DDGIDistanceAtlas);

        resources.PublishDDGIAtlas({}, {}, 4u, true);
        assert(!resources.bDDGIAtlasPublished);
        assert(resources.DDGIProbeCount == 0u);
        assert(!resources.DDGIIrradianceAtlas);
        assert(!resources.DDGIDistanceAtlas);

        resources.DDGIProbeCount = 4u;
        resources.bDDGIAtlasPublished = true;
        resources.Begin(18u, 3u, 5u);
        assert(!resources.bDDGIAtlasPublished);
        assert(resources.DDGIProbeCount == 0u);
        assert(!resources.DDGIIrradianceAtlas);
        assert(!resources.DDGIDistanceAtlas);
    }

    void TestStaleAtlasInvalidation()
    {
        DDGIProbePass probePass;
        DDGIProbePass::FrameResources previousFrame;
        previousFrame.FrameIndex = 1u;
        previousFrame.ViewId = 3u;
        previousFrame.ViewportId = 5u;
        previousFrame.FrameNumber = 16u;
        previousFrame.AtlasProbeCount = 4u;
        previousFrame.bAtlasValid = true;
        probePass.m_FrameResources.push_back(previousFrame);

        ViewRenderContext failedContext;
        failedContext.FrameIndex = 1u;
        failedContext.FrameNumber = 18u;
        failedContext.PhysicalLighting.ViewId = 3u;
        failedContext.PhysicalLighting.ViewportId = 5u;
        assert(!probePass.Execute(failedContext));
        assert(!probePass.GetIrradianceAtlas(1u, 3u, 5u));
        assert(!probePass.GetDistanceAtlas(1u, 3u, 5u));
        assert(probePass.GetAtlasProbeCount(1u, 3u, 5u) == 0u);
    }

    MeshProxy MakeSceneRevisionMeshProxy(uint64_t objectId,
                                         uint64_t meshId,
                                         uint64_t materialId)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = objectId + 100u;
        proxy.MeshHandle = MeshDataHandle{meshId};
        proxy.SubMeshCount = 1u;
        proxy.SubMeshes[0].IndexStart = 3u;
        proxy.SubMeshes[0].IndexCount = 6u;
        proxy.SubMeshes[0].VertexStart = 1u;
        proxy.SubMeshes[0].MaterialIndex = 0u;
        proxy.MaterialCount = 1u;
        proxy.Materials[0] = MaterialHandle{materialId};
        proxy.MaterialBlendModes[0] = BlendMode::Opaque;
        proxy.CustomData[0] = 0.25f;
        return proxy;
    }

    SkinnedMeshProxy MakeSceneRevisionSkinnedMeshProxy(uint64_t objectId,
                                                       uint64_t meshId,
                                                       uint64_t generation,
                                                       uint64_t materialId)
    {
        SkinnedMeshProxy proxy;
        proxy.MeshHandle = SkinnedMeshHandle{meshId, generation};
        proxy.Material = MaterialHandle{materialId};
        proxy.ObjectId = objectId;
        proxy.ComponentId = objectId + 200u;
        proxy.bCastShadow = true;
        proxy.bHasAnimatedBounds = true;
        proxy.bVisible = true;
        return proxy;
    }

    void PopulateSceneRevisionPacket(FramePacket& packet)
    {
        packet.Scene.MeshProxies.push_back(MakeSceneRevisionMeshProxy(1u, 11u, 21u));
        packet.Scene.MeshProxies.push_back(MakeSceneRevisionMeshProxy(2u, 12u, 22u));
        packet.Scene.SkinnedMeshProxies.push_back(
            MakeSceneRevisionSkinnedMeshProxy(3u, 31u, 1u, 41u));
        packet.Scene.SkinnedMeshProxies.push_back(
            MakeSceneRevisionSkinnedMeshProxy(4u, 32u, 1u, 42u));

        DrawCommand meshCommand = DrawCommand::CreateDrawIndexed();
        meshCommand.Draw.PayloadKind = DrawPayloadKind::Mesh;
        meshCommand.Draw.MeshHandle = MeshDataHandle{11u};
        meshCommand.Draw.MaterialHandle = MaterialHandle{21u};
        meshCommand.Draw.SortDepth = 2.0f;
        meshCommand.SortKey = 2u;
        packet.DrawCommands.push_back(meshCommand);

        DrawCommand boardCommand = DrawCommand::CreateDraw();
        boardCommand.Draw.PayloadKind = DrawPayloadKind::Board;
        boardCommand.Draw.SortDepth = 1.0f;
        boardCommand.SortKey = 1u;
        packet.DrawCommands.push_back(boardCommand);

        DrawCommand secondBoardCommand = DrawCommand::CreateDraw();
        secondBoardCommand.Draw.PayloadKind = DrawPayloadKind::Board;
        secondBoardCommand.Draw.SortDepth = 3.0f;
        secondBoardCommand.SortKey = 3u;
        packet.DrawCommands.push_back(secondBoardCommand);

        RayTracingSceneInstanceSnapshot firstInstance;
        firstInstance.MeshHandle = MeshDataHandle{11u};
        firstInstance.IndexOffset = 0u;
        firstInstance.IndexCount = 6u;
        firstInstance.Instance.customIndex = 1u;
        packet.RayTracingScene.Instances.push_back(firstInstance);
        RayTracingSceneInstanceSnapshot secondInstance;
        secondInstance.MeshHandle = MeshDataHandle{12u};
        secondInstance.IndexOffset = 3u;
        secondInstance.IndexCount = 9u;
        secondInstance.Instance.customIndex = 2u;
        packet.RayTracingScene.Instances.push_back(secondInstance);
    }

    void TestSceneRevisionIsCompositionOnly()
    {
        FramePacket baselinePacket;
        PopulateSceneRevisionPacket(baselinePacket);
        const uint64_t baselineHash = ComputeSceneRevisionHash(baselinePacket);

        FramePacket movedAndCulledPacket;
        PopulateSceneRevisionPacket(movedAndCulledPacket);
        movedAndCulledPacket.Scene.MainCamera.PositionX += 50.0f;
        movedAndCulledPacket.Scene.MainCamera.PositionZ -= 25.0f;
        MeshProxy& movedMesh = movedAndCulledPacket.Scene.MeshProxies[0];
        movedMesh.WorldTransform.values[0] += 13.0f;
        movedMesh.PreviousWorldTransform.values[1] -= 7.0f;
        movedMesh.WorldBounds.CenterX += 22.0f;
        movedMesh.SortDepth = 99.0f;
        movedMesh.SortKey = 0xFFFFFFFFu;
        SkinnedMeshProxy& movedSkinned = movedAndCulledPacket.Scene.SkinnedMeshProxies[0];
        movedSkinned.WorldTransform.values[0] += 5.0f;
        movedAndCulledPacket.DrawCommands.clear();
        movedAndCulledPacket.RayTracingScene.Instances.pop_back();
        assert(ComputeSceneRevisionHash(movedAndCulledPacket) == baselineHash);

        FramePacket reorderedPacket;
        PopulateSceneRevisionPacket(reorderedPacket);
        MeshProxy firstMesh = reorderedPacket.Scene.MeshProxies[0];
        reorderedPacket.Scene.MeshProxies[0] = reorderedPacket.Scene.MeshProxies[1];
        reorderedPacket.Scene.MeshProxies[1] = firstMesh;
        SkinnedMeshProxy firstSkinned = reorderedPacket.Scene.SkinnedMeshProxies[0];
        reorderedPacket.Scene.SkinnedMeshProxies[0] = reorderedPacket.Scene.SkinnedMeshProxies[1];
        reorderedPacket.Scene.SkinnedMeshProxies[1] = firstSkinned;
        DrawCommand firstBoardCommand = reorderedPacket.DrawCommands[1];
        reorderedPacket.DrawCommands[1] = reorderedPacket.DrawCommands[2];
        reorderedPacket.DrawCommands[2] = firstBoardCommand;
        RayTracingSceneInstanceSnapshot firstInstance = reorderedPacket.RayTracingScene.Instances[0];
        reorderedPacket.RayTracingScene.Instances[0] = reorderedPacket.RayTracingScene.Instances[1];
        reorderedPacket.RayTracingScene.Instances[1] = firstInstance;
        assert(ComputeSceneRevisionHash(reorderedPacket) == baselineHash);

        FramePacket addedPacket;
        PopulateSceneRevisionPacket(addedPacket);
        addedPacket.Scene.MeshProxies.push_back(MakeSceneRevisionMeshProxy(5u, 15u, 25u));
        assert(ComputeSceneRevisionHash(addedPacket) != baselineHash);

        FramePacket removedPacket;
        PopulateSceneRevisionPacket(removedPacket);
        removedPacket.Scene.MeshProxies.pop_back();
        assert(ComputeSceneRevisionHash(removedPacket) != baselineHash);

        FramePacket meshChangedPacket;
        PopulateSceneRevisionPacket(meshChangedPacket);
        meshChangedPacket.Scene.MeshProxies[0].MeshHandle = MeshDataHandle{99u};
        assert(ComputeSceneRevisionHash(meshChangedPacket) != baselineHash);

        FramePacket materialChangedPacket;
        PopulateSceneRevisionPacket(materialChangedPacket);
        materialChangedPacket.Scene.SkinnedMeshProxies[0].Material = MaterialHandle{99u};
        assert(ComputeSceneRevisionHash(materialChangedPacket) != baselineHash);

        FramePacket environmentChangedPacket;
        PopulateSceneRevisionPacket(environmentChangedPacket);
        environmentChangedPacket.Scene.SkyAtmosphere.SunAltitudeDegrees += 1.0f;
        assert(ComputeSceneRevisionHash(environmentChangedPacket) != baselineHash);
    }

    void TestRTGIFallbackContract()
    {
        NorvesLib::RHI::DeviceCapabilities supportedCapabilities;
        supportedCapabilities.RayTracing.bAccelerationStructure = true;
        supportedCapabilities.RayTracing.bRayQuery = true;
        supportedCapabilities.bBufferDeviceAddress = true;
        supportedCapabilities.bShaderInt64 = true;
        const RTGIRayQueryCapability rayQueryCapability =
            MakeRTGIRayQueryCapability(supportedCapabilities);
        assert(rayQueryCapability.IsUsable());

        RTGIResult diffuseResult;
        assert(diffuseResult.BounceCount == RTGIDiffuseBounceCount);
        assert(diffuseResult.bDiffuse);
        diffuseResult.BounceCount = 2u;
        assert(!diffuseResult.IsComplete());

        supportedCapabilities.RayTracing.bRayQuery = false;
        assert(!MakeRTGIRayQueryCapability(supportedCapabilities).IsUsable());

        RTGIFallbackInputs disabledInputs;
        disabledInputs.bRTGIEnabled = false;
        disabledInputs.bDDGIAvailable = true;
        disabledInputs.bIBLAvailable = true;
        RTGIFallbackDecision decision = ResolveRTGIIndirectLighting(disabledInputs);
        assert(decision.Source == RTGIIndirectLightingSource::DDGI);
        assert(decision.Reason == RTGIFallbackReason::Disabled);
        assert(decision.bUsedFallback);

        RTGIFallbackInputs noTlasInputs;
        noTlasInputs.bRTGIEnabled = true;
        noTlasInputs.bTLASAvailable = false;
        noTlasInputs.Capability = rayQueryCapability;
        noTlasInputs.bIBLAvailable = true;
        decision = ResolveRTGIIndirectLighting(noTlasInputs);
        assert(decision.Source == RTGIIndirectLightingSource::IBL);
        assert(decision.Reason == RTGIFallbackReason::TLASUnavailable);

        RTGIResourcePublication publication;
        publication.Configure(rayQueryCapability, true, true, 12u, 7u, 11u);

        RTGIResult completeResult;
        completeResult.DiffuseIndirectRadiance =
            MakeRTGIContractTexture(4u, 4u, RTGIDiffuseIndirectRadianceFormat);
        completeResult.State = NorvesLib::RHI::ResourceState::ShaderResource;
        completeResult.Width = 4u;
        completeResult.Height = 4u;
        completeResult.FrameNumber = 12u;
        completeResult.SceneRevision = 7u;
        completeResult.LightRevision = 11u;
        completeResult.bValid = true;

        RTGIHistoryResources completeHistory;
        completeHistory.FrameNumber = 12u;
        completeHistory.bValid = true;
        completeHistory.Current.Radiance =
            MakeRTGIContractTexture(4u, 4u, RTGIDiffuseIndirectRadianceFormat);
        completeHistory.Current.Age =
            MakeRTGIContractTexture(4u, 4u, RTGIHistoryAgeFormat);
        completeHistory.Current.Confidence =
            MakeRTGIContractTexture(4u, 4u, RTGIHistoryConfidenceFormat);
        completeHistory.Current.State = NorvesLib::RHI::ResourceState::ShaderResource;
        completeHistory.Current.Width = 4u;
        completeHistory.Current.Height = 4u;
        completeHistory.Current.SceneRevision = 7u;
        completeHistory.Current.LightRevision = 11u;
        completeHistory.Current.bValid = true;
        completeHistory.Current.AgeFrames = 0u;
        completeHistory.History.Radiance =
            MakeRTGIContractTexture(4u, 4u, RTGIDiffuseIndirectRadianceFormat);
        completeHistory.History.Age =
            MakeRTGIContractTexture(4u, 4u, RTGIHistoryAgeFormat);
        completeHistory.History.Confidence =
            MakeRTGIContractTexture(4u, 4u, RTGIHistoryConfidenceFormat);
        completeHistory.History.State = NorvesLib::RHI::ResourceState::ShaderResource;
        completeHistory.History.Width = 4u;
        completeHistory.History.Height = 4u;
        completeHistory.History.SceneRevision = 6u;
        completeHistory.History.LightRevision = 11u;
        completeHistory.History.bValid = true;
        completeHistory.History.AgeFrames = 1u;

        publication.PublishResult(completeResult, completeHistory);
        assert(publication.History.IsComplete());
        assert(publication.History.HasHistoryRevisionMismatch(7u, 11u));
        assert(publication.bPublished);
        decision = publication.Resolve(false, false);
        assert(decision.Source == RTGIIndirectLightingSource::RTGI);
        assert(decision.Reason == RTGIFallbackReason::None);
        assert(!decision.bUsedFallback);

        publication.Configure(rayQueryCapability, true, true, 12u, 8u, 11u);
        publication.PublishResult(completeResult, completeHistory);
        assert(!publication.bPublished);
        decision = publication.Resolve(false, true);
        assert(decision.Source == RTGIIndirectLightingSource::IBL);
        assert(decision.Reason == RTGIFallbackReason::ResourceUnavailable);

        RTGIResult incompleteResult;
        RTGIHistoryResources incompleteHistory;
        publication.PublishResult(incompleteResult, incompleteHistory);
        assert(!publication.bPublished);
        decision = publication.Resolve(false, false);
        assert(decision.Source == RTGIIndirectLightingSource::Raster);
        assert(decision.Reason == RTGIFallbackReason::ResourceUnavailable);

        FramePacket packet;
        packet.SceneRevision = 7u;
        packet.LightRevision = 11u;
        packet.bRTGIEnabled = false;
        assert(packet.SceneRevision == 7u && packet.LightRevision == 11u);
        assert(!packet.HasCompleteRayTracingScene());
        packet.Clear();
        assert(packet.SceneRevision == 0u && packet.LightRevision == 0u);
        assert(packet.bRTGIEnabled);
    }
}

int main()
{
    TestDDGIAtlasPublicationReset();
    TestStaleAtlasInvalidation();
    TestSceneRevisionIsCompositionOnly();
    TestRTGIFallbackContract();
    std::cout << "DDGI照明契約テスト: 合格\n";
    return 0;
}
