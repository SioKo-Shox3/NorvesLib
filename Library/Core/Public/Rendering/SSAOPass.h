#pragma once

#include "Rendering/IViewPass.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief SSAOパス設定
     */
    struct SSAOSettings
    {
        /** @brief サンプリング半径（ワールド単位） */
        float Radius = 0.5f;

        /** @brief 深度バイアス（自己遮蔽回避） */
        float Bias = 0.025f;

        /** @brief AO強度（pow指数） */
        float Intensity = 2.0f;

        /** @brief 出力フォーマット（単チャンネル） */
        RHI::Format OutputFormat = RHI::Format::R8_UNORM;
    };

    /**
     * @brief SSAO（Screen-Space Ambient Occlusion）パス
     *
     * GBufferの深度と法線から、各ピクセル周辺の遮蔽度を計算します。
     * オブジェクト間のコンタクトシャドウや凹凸の陰影を動的に追加します。
     *
     * パイプラインの位置: GBuffer → **SSAO** → Lighting
     *
     * 入力（SharedResourceRegistryから取得）:
     * - "GBuffer_Depth" : 深度テクスチャ
     * - "GBuffer_Normal" : ワールド法線テクスチャ
     *
     * 出力（SharedResourceRegistryに登録）:
     * - "SSAO" : AO値テクスチャ (R8_UNORM)
     */
    class SSAOPass : public IViewPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings SSAO設定
         */
        explicit SSAOPass(const SSAOSettings& settings = SSAOSettings{});

        /**
         * @brief デストラクタ
         */
        ~SSAOPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "SSAOPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        void SetRadius(float radius) { m_Settings.Radius = radius; }
        void SetBias(float bias) { m_Settings.Bias = bias; }
        void SetIntensity(float intensity) { m_Settings.Intensity = intensity; }
        const SSAOSettings& GetSettings() const { return m_Settings; }

    private:
        /**
         * @brief サンプルカーネル（半球内のランダム方向）を生成
         */
        void GenerateKernel();

        /**
         * @brief 4x4ランダム回転ノイズテクスチャを生成
         */
        void GenerateNoiseTexture();

        // 設定
        SSAOSettings m_Settings;

        // SSAOパス用リソース
        RHI::TexturePtr m_SSAORawTexture;    // ブラー前の生AO
        RHI::TexturePtr m_SSAOBlurredTexture; // ブラー後の最終AO
        RHI::TexturePtr m_NoiseTexture;       // 4x4ランダムノイズ

        // SSAOパス
        RHI::RenderPassPtr m_SSAORenderPass;
        RHI::FramebufferPtr m_SSAOFramebuffer;
        RHI::PipelinePtr m_SSAOPipeline;
        RHI::ShaderPtr m_SSAOVertexShader;
        RHI::ShaderPtr m_SSAOFragmentShader;
        RHI::BufferPtr m_SSAOParamsBuffer;
        RHI::BufferPtr m_KernelBuffer;
        RHI::DescriptorSetPtr m_SSAODescriptorSet;

        // ブラーパス
        RHI::RenderPassPtr m_BlurRenderPass;
        RHI::FramebufferPtr m_BlurFramebuffer;
        RHI::PipelinePtr m_BlurPipeline;
        RHI::ShaderPtr m_BlurFragmentShader;
        RHI::BufferPtr m_BlurParamsBuffer;
        RHI::DescriptorSetPtr m_BlurDescriptorSet;

        // サンプラー
        RHI::SamplerPtr m_LinearClampSampler;
        RHI::SamplerPtr m_NearestRepeatSampler;

        // デバイス参照
        RHI::IDevice* m_Device = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;

        // カーネルデータ（CPU側保持）
        static constexpr int KERNEL_SIZE = 32;
        float m_KernelData[64 * 4]; // vec4 * 64（シェーダーは64確保、32使用）
    };

} // namespace NorvesLib::Core::Rendering
