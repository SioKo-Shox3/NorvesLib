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
    class BloomPass;

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

        // ========================================
        // Vignette（周辺減光）
        // ========================================

        /** @brief ビネット強度（0=無効、0.3=微妙、0.6=強め） */
        float VignetteIntensity = 0.3f;

        /** @brief ビネット内側半径 */
        float VignetteRadius = 0.8f;

        /** @brief ビネットのフォールオフ柔らかさ */
        float VignetteSoftness = 0.5f;

        // ========================================
        // Color Grading
        // ========================================

        /** @brief カラーフィルター（RGB） */
        float ColorFilter[3] = {1.0f, 1.0f, 1.0f};

        /** @brief カラーフィルター強度 */
        float ColorFilterIntensity = 1.0f;

        /** @brief コントラスト（1.0=デフォルト） */
        float Contrast = 1.05f;

        /** @brief 彩度（1.0=デフォルト） */
        float Saturation = 1.1f;

        /** @brief 明度オフセット */
        float Brightness = 0.0f;

        /** @brief 色温度シフト（-1〜+1、0=ニュートラル） */
        float Temperature = 0.0f;
    };

    /**
     * @brief トーンマッピングパス（ポストプロセス）
     *
     * HDRシーンカラーをLDRに変換し、ガンマ補正を適用します。
     * PostProcessStackに追加して使用するポストプロセスパスです。
     *
     * 標準経路では RenderGraph named resource から入力を読み取り、
     * "ToneMappedColor" graph output として後段へ渡します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * 入力:
     * - "SceneColor" : HDRライティング結果 (R16G16B16A16_FLOAT)
     *
     * 出力:
     * - "ToneMappedColor" : LDR変換後のカラー (R8G8B8A8_UNORM)
     *
     * サポートするトーンマッピングアルゴリズム:
     * - Reinhard: シンプルで高速
     * - ACES Filmic: 映画品質、最もバランスが良い（デフォルト）
     * - Uncharted2: ゲームで広く使用
     * - Exposure: 露出ベースの単純なクランプ
     */
    class ToneMappingPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings トーンマッピング設定
         */
        explicit ToneMappingPass(const ToneMappingSettings& settings = ToneMappingSettings{});

        /**
         * @brief デストラクタ
         */
        ~ToneMappingPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "ToneMappingPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        /**
         * @brief Legacy bridge fallback 用の入力パス参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetInputPass(const BloomPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetToneMappedColorHandle() const { return m_OutputHandle; }

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
        const ToneMappingSettings& GetSettings() const { return m_Settings; }

    private:
        struct AttachmentSignature
        {
            RGAttachmentKind Kind = RGAttachmentKind::Color;
            RHI::Format Format = RHI::Format::UNKNOWN;
            RHI::AttachmentLoadOp LoadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp StoreOp = RHI::AttachmentStoreOp::Store;
            RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
            RHI::ResourceState FinalState = RHI::ResourceState::Undefined;
            RHI::ITexture* Target = nullptr;
            uint32_t Width = 0;
            uint32_t Height = 0;
            bool bDepthReadOnly = false;
        };

        struct RenderPassSignature
        {
            AttachmentSignature Output;
            bool bValid = false;
        };

        bool AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                       const AttachmentSignature& rhs) const;
        bool RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                       const RenderPassSignature& rhs) const;
        RenderPassSignature CreateToneMappingRenderPassSignature(uint32_t width,
                                                                 uint32_t height,
                                                                 const RHI::TexturePtr& outputTexture,
                                                                 bool bUseRenderGraphInitialState) const;
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext& context,
                              const RHI::TexturePtr& sceneColorPtr,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext& context) const;

        // 設定
        ToneMappingSettings m_Settings;

        // 出力テクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_OutputHandle;
        RGResourceHandle m_InputSceneColorHandle;

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
        const BloomPass* m_InputPass = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
        RenderPassSignature m_RenderPassSignature;
    };

} // namespace NorvesLib::Core::Rendering
