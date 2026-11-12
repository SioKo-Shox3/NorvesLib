// 真上からの雨の遮蔽を深度画像へ描き、描画グラフとビューへ公開する。
#pragma once

#include "Container/PointerTypes.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"

namespace NorvesLib::Core::Rendering
{
    struct OverheadHeightMapPassState;
    struct OverheadHeightMapWindow;

    class OverheadHeightMapPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        OverheadHeightMapPass();
        ~OverheadHeightMapPass() override;

        const char* GetName() const override { return "OverheadHeightMapPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        void PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        friend struct OverheadHeightMapVulkanTestAccess;

        bool EnsureTargets();
        void CollectOccluders(const ViewRenderContext& context, const OverheadHeightMapWindow& window);
        bool PrepareRedraw(const ViewRenderContext& context, const OverheadHeightMapWindow& window);
        void PublishContextResources(RenderGraphResources& resources, ViewRenderContext& context) const;

        Container::TUniquePtr<OverheadHeightMapPassState> m_StateData;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::TexturePtr m_Texture;
        RHI::SamplerPtr m_Sampler;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::DescriptorSetPtr m_PreparedDescriptorSet;
        RGTextureHandle m_HeightMapHandle;
        FrameResourceGeneration m_LastUpdateSubmission;
    };
} // namespace NorvesLib::Core::Rendering
