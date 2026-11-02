#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct CameraViewConstants;
    struct DistortionScene;
    class FrameResourceLane;

    /** @brief 半透明の描画後に画面空間の歪みを集め、シーンの色へ書き戻すパス */
    class DistortionPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~DistortionPass() override;

        const char* GetName() const override { return "DistortionPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /**
         * @brief ずらしの添付をゼロで消し、源を加算する（空の一覧でも消す）
         *
         * Initialize 後、frameResources が記録中に呼ぶ。両テクスチャは別の同寸法の資源とし、
         * offsetTarget は RG16F の RenderTarget、depthTexture は ShaderResource にして渡す。
         * 成功後は offsetTarget が ShaderResource、深度は変わらない。false なら描画を記録せず状態も変えない。
         * cameraConstants は深度の描画に使ったカメラの値、totalTime は経過秒とする。
         */
        bool RecordOffsets(const RHI::TexturePtr& offsetTarget, const RHI::TexturePtr& depthTexture,
                           RHI::ICommandList& commandList, FrameResourceLane& frameResources,
                           const DistortionScene& scene, const CameraViewConstants& cameraConstants, double totalTime);

        /**
         * @brief シーンの色をずらして一時の色へ集め、シーンの色へ混合せずに写す
         *
         * Initialize 後、frameResources が記録中に呼ぶ。両テクスチャは別の同寸法の資源とし、
         * sceneColorTarget は RenderTarget、RG16F の offsetTexture は ShaderResource にして渡す。
         * 成功後は両方が ShaderResource。一時の色も ShaderResource で終える。
         * false なら描画を記録せず、渡された資源の状態も変えない。
         */
        bool RecordComposite(const RHI::TexturePtr& sceneColorTarget, const RHI::TexturePtr& offsetTexture,
                             RHI::ICommandList& commandList, FrameResourceLane& frameResources);

    private:
        bool EnsureOffsetTarget(const RHI::TexturePtr& target);
        bool EnsureCompositeTarget(const RHI::TexturePtr& target);
        void LogInstanceCount(uint32_t count);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_OffsetVertexShader;
        RHI::ShaderPtr m_OffsetFragmentShader;
        RHI::ShaderPtr m_FullscreenVertexShader;
        RHI::ShaderPtr m_GatherShader;
        RHI::ShaderPtr m_CopyShader;
        RHI::SamplerPtr m_PointSampler;
        RHI::SamplerPtr m_LinearSampler;
        RHI::DescriptorSetLayoutPtr m_OffsetLayout;
        RHI::DescriptorSetLayoutPtr m_GatherLayout;
        RHI::DescriptorSetLayoutPtr m_CopyLayout;
        RHI::TexturePtr m_OffsetTexture;
        RHI::TexturePtr m_GatherTexture;
        RHI::RenderPassPtr m_OffsetRenderPass;
        RHI::RenderPassPtr m_ColorRenderPass;
        RHI::FramebufferPtr m_OffsetFramebuffer;
        RHI::FramebufferPtr m_GatherFramebuffer;
        RHI::FramebufferPtr m_CopyFramebuffer;
        RHI::PipelinePtr m_OffsetPipeline;
        RHI::PipelinePtr m_GatherPipeline;
        RHI::PipelinePtr m_CopyPipeline;
        const RHI::ITexture* m_OffsetTarget = nullptr;
        const RHI::ITexture* m_ColorTarget = nullptr;
        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_DepthHandle;
        uint32_t m_LoggedInstanceCount = 0;
    };
} // namespace NorvesLib::Core::Rendering
