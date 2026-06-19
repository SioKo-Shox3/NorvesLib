#pragma once

#include "IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    class FXAAPass;

    /**
     * @brief 最終画像アップスケール設定
     */
    struct UpscaleSettings
    {
        /** @brief 出力フォーマット */
        RHI::Format OutputFormat = RHI::Format::R8G8B8A8_UNORM;
    };

    /**
     * @brief 最終画像アップスケールパス
     *
     * ToneMappedColor を入力として受け取り、内部描画解像度が
     * スクリーン解像度より低い場合に高品質なアップスケールを適用します。
     * 標準経路では "PresentationColor" graph output として出力します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     */
    class UpscalePass : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit UpscalePass(const UpscaleSettings &settings = UpscaleSettings{});
        ~UpscalePass() override;

        const char *GetName() const override { return "UpscalePass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

        /**
         * @brief Legacy bridge fallback 用の入力パス参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetInputPass(const FXAAPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetPresentationColorHandle() const { return m_OutputHandle; }

    private:
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext &context,
                              const RHI::TexturePtr& inputTexture,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext &context) const;
        static bool NeedsUpscale(uint32_t renderWidth,
                                 uint32_t renderHeight,
                                 uint32_t screenWidth,
                                 uint32_t screenHeight);

        UpscaleSettings m_Settings;

        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_InputToneMappedHandle;
        RGResourceHandle m_OutputHandle;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::DescriptorSetPtr m_DescriptorSet;
        RHI::SamplerPtr m_LinearSampler;
        RHI::IDevice *m_Device = nullptr;
        const FXAAPass* m_InputPass = nullptr;
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
