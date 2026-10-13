#pragma once

#include "IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
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

    };

    /**
     * @brief FXAA 3.11ポストプロセスパス
     *
     * ToneMappingPassの後に配置し、display-linear画像に対してアンチエイリアシングを適用する。
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
        // 監査済み: Execute は全画面のパスを積むだけで自分でバリアを張らず、出力は render pass の finalState
        // （宣言の最終の状態 ShaderResource）へ移る。準備が整ったフレームだけ、計画した区間で Parallel に記録する。
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        // 区間ごとの記録の前に、render pass・framebuffer・パイプラインの作り直しを直列で済ませる。
        void PrepareParallelRecord(RenderGraphResources &resources, ViewRenderContext &context) override;

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
        RHI::SamplerPtr m_LinearSampler;
        // 共有の記述子セットのレイアウト（定数と記述子セットは Execute ごとにフレームの持ち主から借りる）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        // 持ち主の無い文脈（RenderingCoordinator を通さない検査）で作った使い回さない定数と記述子セット。
        // 提出の完了を知る手段が無いので、パスを手放すまで持つ
        VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;

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
