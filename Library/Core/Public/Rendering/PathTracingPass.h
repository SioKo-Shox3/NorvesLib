// FramePacketのRTスナップショットを使う独立パストレーシングパス。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class PathTracingPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "PathTracingPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        uint32_t GetAccumulatedSampleCount() const;
        RHI::TexturePtr GetAccumulatedTexture() const;

    private:
        struct FrameResources
        {
            uint32_t FrameIndex = UINT32_MAX;
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParametersBuffer;
            RHI::BufferPtr InstanceBuffer;
            uint64_t InstanceBufferCapacity = 0u;
            RHI::AccelerationStructurePtr MotionTopLevel;
            uint32_t MotionInstanceCapacity = 0u;
        };

        struct History
        {
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            uint32_t Width = 0u;
            uint32_t Height = 0u;
            uint32_t CurrentIndex = 1u;
            uint32_t SampleCount = 0u;
            uint64_t SceneRevision = 0u;
            uint64_t LightRevision = 0u;
            uint64_t CameraSignature = 0u;
            uint64_t GeometrySignature = 0u;
            uint64_t SkySignature = 0u;
            bool bSkyValid = false;
            RHI::TexturePtr Textures[2];
            RHI::ResourceState TextureStates[2] = {
                RHI::ResourceState::Undefined, RHI::ResourceState::Undefined};
            Container::VariableArray<FrameResources> FrameSlots;
        };

        History* FindOrCreateHistory(const ViewRenderContext& context, uint32_t width, uint32_t height);
        FrameResources* FindOrCreateFrameResources(const ViewRenderContext& context,
                                                  History& history);
        bool PrepareInstances(const ViewRenderContext& context,
                              FrameResources& frameResources);

        Container::VariableArray<History> m_Histories;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_RayGenerationShader;
        RHI::ShaderPtr m_MissShader;
        RHI::ShaderPtr m_ClosestHitShader;
        RHI::SamplerPtr m_Sampler;
        RGTextureHandle m_OutputHandle;
        RGTextureHandle m_SkyRadianceHandle;
        RGTextureHandle m_SkyTransmittanceHandle;
        RGTextureHandle m_SunDiskHandle;
        uint32_t m_ActiveHistoryIndex = UINT32_MAX;
        uint32_t m_ActiveFrameResourceIndex = UINT32_MAX;
        uint32_t m_TargetIndex = 0u;
        uint64_t m_DeclaredCameraSignature = 0u;
        uint64_t m_DeclaredGeometrySignature = 0u;
        uint64_t m_DeclaredSkySignature = 0u;
        bool m_bPrepared = false;
    };
}
