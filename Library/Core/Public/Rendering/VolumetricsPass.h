#pragma once

#include "IViewPass.h"
#include "Rendering/VolumetricFog.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class VolumetricsPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~VolumetricsPass() override;

        const char* GetName() const override { return "VolumetricsPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        bool PrepareResources(const RHI::TexturePtr& sceneColorTexture);
        void EnqueueEmptyNativePass(ViewRenderContext& context,
                                    const RHI::TexturePtr& sceneColorTexture) const;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::SamplerPtr m_SceneDepthSampler;
        RHI::SamplerPtr m_SkyRadianceSampler;
        RHI::DescriptorSetPtr m_DescriptorSet;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_SkyAtmosphereRadianceHandle;
        RGResourceHandle m_CascadedShadowMapHandle;
        VolumetricFogParameters m_FogParameters;

        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
    };
} // namespace NorvesLib::Core::Rendering
