// GPU の実行先のエミッタを常駐のプールで生成・更新する描画の拡張のパス
#pragma once

#include "Rendering/GpuParticle/GpuParticleFrame.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"

namespace NorvesLib::Core::Rendering
{
    class RenderExtensionRegistry;

    /** @brief ビューとビューポートごとに粒のプールを保持し、生成と更新を行う。 */
    class GpuParticleSimulationPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        ~GpuParticleSimulationPass() override;

        const char* GetName() const override;
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        struct ViewResources
        {
            uint32_t ViewId = 0;
            uint32_t ViewportId = 0;
            uint64_t LastUsedFrameNumber = 0;
            RHI::BufferPtr Pool;
            RHI::BufferPtr Heads;
            RHI::BufferPtr DrawList;
            RHI::BufferPtr DrawArgs;
            uint32_t LastGeneration[kGpuParticleMaxEmitters] = {};
            double ParticleTime = 0.0;
            double UnscaledTime = 0.0;
            bool bHasTime = false;
            bool bNeedsClear = true;
        };

        ViewResources* FindOrCreateViewResources(const ViewRenderContext& context);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_UpdateShader;
        RHI::ShaderPtr m_SpawnShader;
        RHI::ShaderPtr m_CompactShader;
        RHI::PipelinePtr m_UpdatePipeline;
        RHI::PipelinePtr m_SpawnPipeline;
        RHI::PipelinePtr m_CompactPipeline;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::TexturePtr m_EmptyTexture;
        RHI::TexturePtr m_EmptyHeightTexture;
        RHI::SamplerPtr m_Sampler;
        Container::VariableArray<ViewResources> m_ViewResources;
        RGTextureHandle m_DepthHandle;
        RGTextureHandle m_NormalHandle;
        RGResourceHandle m_PoolHandle;
        RGResourceHandle m_HeadsHandle;
        RGResourceHandle m_DrawListHandle;
        RGResourceHandle m_DrawArgsHandle;
        bool m_bReportedClampedTime = false;
    };

    /** @brief GPU の粒の更新を GBuffer の段の最後へ登録する。 */
    Container::TUniquePtr<IViewPass> CreateGpuParticleSimulationPass(void* context);
    bool RegisterGpuParticleSimulationExtension(RenderExtensionRegistry& registry);
}
