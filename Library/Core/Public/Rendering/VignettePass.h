#pragma once

#include "IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class ToneMappingPass;

    struct VignetteSettings
    {
        float Intensity = 0.3f;
        float Radius = 0.8f;
        float Softness = 0.5f;
        bool bEnabled = true;
        RHI::Format OutputFormat = RHI::Format::R8G8B8A8_UNORM;
    };

    class VignettePass : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit VignettePass(const VignetteSettings& settings = VignetteSettings{});
        ~VignettePass() override;

        const char* GetName() const override { return "VignettePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        void SetInputPass(const ToneMappingPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetToneMappedColorHandle() const { return m_OutputHandle; }
        RGTextureHandle GetToneMappedColorTextureHandle() const { return m_OutputTextureHandle; }

        void SetEnabled(bool bEnabled) { m_Settings.bEnabled = bEnabled; }
        void SetIntensity(float intensity) { m_Settings.Intensity = intensity; }
        void SetRadius(float radius) { m_Settings.Radius = radius; }
        void SetSoftness(float softness) { m_Settings.Softness = softness; }
        const VignetteSettings& GetSettings() const { return m_Settings; }

    private:
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext& context,
                              const RHI::TexturePtr& inputTexture,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext& context) const;

        VignetteSettings m_Settings;

        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_InputToneMappedHandle;
        RGTextureHandle m_OutputTextureHandle;
        RGResourceHandle m_OutputHandle;

        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::DescriptorSetPtr m_DescriptorSet;
        RHI::SamplerPtr m_LinearSampler;

        RHI::IDevice* m_Device = nullptr;
        const ToneMappingPass* m_InputPass = nullptr;

        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
