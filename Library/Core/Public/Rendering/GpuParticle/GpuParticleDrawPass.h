// 常駐プールの GPU の粒を TAA の前後で HDR の色へ重ねる。
#pragma once

#include "Rendering/GpuParticle/GpuParticleDraw.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderExtension/RenderExtensionHelpers.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"

namespace NorvesLib::Core::Rendering
{
    class RenderExtensionRegistry;

    /**
     * @brief 描画段に合う GPU の粒を深度テクスチャで判定して重ねる。
     * PreTAA の premultiplied の粒は被覆率を Scene.Reactive へ書く。
     */
    class GpuParticleDrawPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit GpuParticleDrawPass(uint32_t drawPass);
        ~GpuParticleDrawPass() override;

        const char* GetName() const override;
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        void ResetHandles();
        bool EnsureTarget(const RHI::TexturePtr& target);
        bool EnsureReactivePipeline();
        void ReportReactiveFailure(const char* reason);

        uint32_t m_DrawPass;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::SamplerPtr m_DepthSampler;
        RHI::SamplerPtr m_LinearSampler;
        RHI::TexturePtr m_BlackTexture;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_PremultipliedPipeline;
        RHI::PipelinePtr m_AdditivePipeline;
        ReactiveTargetBinder m_ReactiveTargetBinder;
        RHI::PipelinePtr m_ReactivePipeline;
        RHI::RenderPassPtr m_ReactiveRenderPass;
        const RHI::ITexture* m_ColorTarget = nullptr;
        RHI::Format m_TargetFormat = RHI::Format::UNKNOWN;
        RGBufferHandle m_PoolHandle;
        RGBufferHandle m_DrawListHandle;
        RGBufferHandle m_DrawArgsHandle;
        RGTextureHandle m_ColorHandle;
        RGTextureHandle m_DepthHandle;
        RGTextureHandle m_SkyHandle;
        bool m_bReactiveDeclared = false;
        bool m_bReportedReactiveFailure = false;
        bool m_bReportedInitializationFailure = false;
        bool m_bReportedCompactionUnavailable = false;
        bool m_bReportedIndirectPreparationFailure = false;
        bool m_bReportedIndirectFailure = false;
    };

    /** @brief TAA の前後へ粒の描画を生成・登録する。 */
    Container::TUniquePtr<IViewPass> CreateGpuParticleDrawPassPreTAA(void* context);
    Container::TUniquePtr<IViewPass> CreateGpuParticleDrawPassPostTAA(void* context);
    bool RegisterGpuParticleDrawExtensions(RenderExtensionRegistry& registry);
} // 名前空間 NorvesLib::Core::Rendering
