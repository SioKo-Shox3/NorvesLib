#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    struct CompositePassRequest
    {
        RHI::TexturePtr SceneTexture;
        RHI::TexturePtr CanvasTexture;
        RHI::ShaderPtr VertexShader;
        RHI::ShaderPtr PixelShader;
        RHI::DescriptorSetPtr DescriptorSet;
        RHI::SamplerPtr Sampler;
    };

    struct CompositePassResult
    {
        bool bPublishedComposite = false;
        bool bImportedCanvas = false;
        bool bScenePassthrough = true;
        RHI::TexturePtr SceneTexture;
        RHI::TexturePtr CanvasTexture;
        RHI::TexturePtr OutputTexture;
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        RHI::PipelinePtr Pipeline;
    };

    /**
     * @brief シーンの上にキャンバスをアルファ合成し、Composite.Color として出力する。
     *
     * 出力はキャンバスと同じ画面寸法とし、RenderScale を適用したシーンを UV で画面全体に引き伸ばす。
     * キャンバスが無い場合はシーンをそのまま Composite.Color として出力する。
     */
    class CompositePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override
        {
            return "CompositePass";
        }

        void SetRequest(const CompositePassRequest& request);
        void ResetResult();
        void ReleaseRetainedResources();

        const CompositePassResult& GetLastResult() const
        {
            return m_Result;
        }

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        struct RetainedFrameResources
        {
            RHI::TexturePtr OutputTexture;
            RHI::RenderPassPtr RenderPass;
            RHI::FramebufferPtr Framebuffer;
            RHI::PipelinePtr Pipeline;
        };

        void RetainResultResources();

        CompositePassRequest m_Request;
        CompositePassResult m_Result;
        RGResourceHandle m_SceneHandle;
        RGResourceHandle m_CanvasHandle;
        RGResourceHandle m_OutputHandle;
        Container::VariableArray<RetainedFrameResources> m_RetainedFrameResources;
    };

} // namespace NorvesLib::Core::Rendering
