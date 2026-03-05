#pragma once

#include "IViewPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

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
     * "ToneMappedColor"を入力としSharedResourceRegistryに結果を再登録する。
     */
    class FXAAPass : public IViewPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings FXAA設定
         */
        explicit FXAAPass(const FXAASettings& settings = FXAASettings{});

        /**
         * @brief デストラクタ
         */
        ~FXAAPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "FXAAPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        /**
         * @brief FXAA有効/無効を切り替え
         */
        void SetEnabled(bool bEnabled) { m_Settings.bEnabled = bEnabled; }

        /**
         * @brief 現在の設定を取得
         */
        const FXAASettings& GetSettings() const { return m_Settings; }

    private:
        // 設定
        FXAASettings m_Settings;

        // 出力テクスチャ
        RHI::TexturePtr m_OutputTexture;

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
        RHI::IDevice* m_Device = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
