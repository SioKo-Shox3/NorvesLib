#pragma once

#include "IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    class ToneMappingPass;

    /**
     * @brief FXAAパス設定
     */
    struct FXAASettings
    {
        /** @brief エッジ検出閾値（低いほど敏感） */
        float EdgeThreshold = 0.0312f;

        /** @brief 最小エッジ閾値（暗い領域での閾値） */
        float EdgeThresholdMin = 0.0625f;

        /** @brief サブピクセル品質（0.0〜1.0、高いほど品質向上） */
        float SubpixelQuality = 0.75f;

        /** @brief FXAA有効/無効 */
        bool bEnabled = true;

        /** @brief 出力フォーマット */
        RHI::Format OutputFormat = RHI::Format::R8G8B8A8_UNORM;
    };

    /**
     * @brief FXAA 3.11ポストプロセスパス
     *
     * ToneMappingPassの後に配置し、LDR画像に対してアンチエイリアシングを適用する。
     * 標準経路では "ToneMappedColor" named resource を読み取り、graph output として
     * "ToneMappedColor" を更新する。SharedResourceRegistry は legacy/fallback bridge の
     * 互換経路でのみ使用する。
     */
    class FXAAPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings FXAA設定
         */
        explicit FXAAPass(const FXAASettings &settings = FXAASettings{});

        /**
         * @brief デストラクタ
         */
        ~FXAAPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "FXAAPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        /**
         * @brief Legacy bridge fallback 用の入力パス参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetInputPass(const ToneMappingPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetToneMappedColorHandle() const { return m_OutputHandle; }
        RGTextureHandle GetToneMappedColorTextureHandle() const { return m_OutputTextureHandle; }

        /**
         * @brief FXAA有効/無効を切り替え
         */
        void SetEnabled(bool bEnabled) { m_Settings.bEnabled = bEnabled; }

        /**
         * @brief 現在の設定を取得
         */
        const FXAASettings &GetSettings() const { return m_Settings; }

    private:
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext &context,
                              const RHI::TexturePtr& inputTexture,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext &context) const;

    protected:
        // 設定
        FXAASettings m_Settings;

        // 出力テクスチャ
        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_InputToneMappedHandle;
        RGTextureHandle m_OutputTextureHandle;
        RGResourceHandle m_OutputHandle;

        // パイプラインリソース
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::DescriptorSetPtr m_DescriptorSet;
        RHI::SamplerPtr m_LinearSampler;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;
        const ToneMappingPass* m_InputPass = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
