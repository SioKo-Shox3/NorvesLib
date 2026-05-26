#pragma once

#include "IViewPass.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
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
     * 出力は SharedResourceRegistry に "PresentationColor" として登録されます。
     */
    class UpscalePass : public IViewPass
    {
    public:
        explicit UpscalePass(const UpscaleSettings &settings = UpscaleSettings{});
        ~UpscalePass() override;

        const char *GetName() const override { return "UpscalePass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

    private:
        UpscaleSettings m_Settings;

        RHI::TexturePtr m_OutputTexture;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::DescriptorSetPtr m_DescriptorSet;
        RHI::SamplerPtr m_LinearSampler;
        RHI::IDevice *m_Device = nullptr;
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
