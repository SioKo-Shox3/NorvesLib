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
     * @brief トーンマッピングアルゴリズムの選択
     */
    enum class ToneMappingOperator : uint8_t
    {
        /** Reinhard トーンマッピング */
        Reinhard,

        /** ACES Filmic トーンマッピング（映画品質） */
        ACES,

        /** Uncharted2 トーンマッピング */
        Uncharted2,

        /** 露出ベースの単純なクランプ */
        Exposure
    };

    /**
     * @brief トーンマッピングパス設定
     */
    struct ToneMappingSettings
    {
        /** @brief 使用するトーンマッピングアルゴリズム */
        ToneMappingOperator Operator = ToneMappingOperator::ACES;

        /** @brief 露出値（Exposureモード/共通パラメータ） */
        float Exposure = 1.0f;

        /** @brief ガンマ補正値 */
        float Gamma = 2.2f;

        /** @brief 出力フォーマット（LDR） */
        RHI::Format OutputFormat = RHI::Format::R8G8B8A8_UNORM;
    };

    /**
     * @brief トーンマッピングパス（ポストプロセス）
     *
     * HDRシーンカラーをLDRに変換し、ガンマ補正を適用します。
     * PostProcessStackに追加して使用するポストプロセスパスです。
     *
     * 入力（SharedResourceRegistryから取得）:
     * - "SceneColor" : HDRライティング結果 (R16G16B16A16_FLOAT)
     *
     * 出力（SharedResourceRegistryに登録）:
     * - "ToneMappedColor" : LDR変換後のカラー (R8G8B8A8_UNORM)
     *
     * サポートするトーンマッピングアルゴリズム:
     * - Reinhard: シンプルで高速
     * - ACES Filmic: 映画品質、最もバランスが良い（デフォルト）
     * - Uncharted2: ゲームで広く使用
     * - Exposure: 露出ベースの単純なクランプ
     */
    class ToneMappingPass : public IViewPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings トーンマッピング設定
         */
        explicit ToneMappingPass(const ToneMappingSettings &settings = ToneMappingSettings{});

        /**
         * @brief デストラクタ
         */
        ~ToneMappingPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "ToneMappingPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        /**
         * @brief トーンマッピングアルゴリズムを変更
         * @param op 新しいアルゴリズム
         */
        void SetOperator(ToneMappingOperator op) { m_Settings.Operator = op; }

        /**
         * @brief 露出値を設定
         * @param exposure 露出値
         */
        void SetExposure(float exposure) { m_Settings.Exposure = exposure; }

        /**
         * @brief ガンマ値を設定
         * @param gamma ガンマ値
         */
        void SetGamma(float gamma) { m_Settings.Gamma = gamma; }

        /**
         * @brief 現在の設定を取得
         * @return トーンマッピング設定の参照
         */
        const ToneMappingSettings &GetSettings() const { return m_Settings; }

    private:
        // 設定
        ToneMappingSettings m_Settings;

        // 出力テクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_OutputTexture;

        // パイプラインリソース
        RHI::RenderPassPtr m_ToneMappingRenderPass;
        RHI::FramebufferPtr m_ToneMappingFramebuffer;
        RHI::PipelinePtr m_ToneMappingPipeline;
        RHI::ShaderPtr m_ToneMappingVertexShader;
        RHI::ShaderPtr m_ToneMappingFragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::DescriptorSetPtr m_ToneMappingDescriptorSet;
        RHI::SamplerPtr m_SceneColorSampler;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
