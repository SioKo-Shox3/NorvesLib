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
    class SSRPass;

    /**
     * @brief ブルームパス設定
     */
    struct BloomSettings
    {
        /** @brief 輝度閾値（この値以上がブルームの対象） */
        float Threshold = 1.0f;

        /** @brief ブルーム強度（加算合成の乗数） */
        float Intensity = 1.5f;

        /** @brief ブラー半径（ピクセル単位） */
        float Radius = 3.0f;

        /** @brief ソフト閾値の膝（0-1、閾値付近のフォールオフ制御） */
        float SoftKnee = 0.5f;

        /** @brief 出力フォーマット（HDR、ToneMappingの前にかかるため） */
        RHI::Format OutputFormat = RHI::Format::R16G16B16A16_FLOAT;
    };

    /**
     * @brief ブルームポストプロセスパス
     *
     * HDRシーンカラーから高輝度部分を抽出し、ガウスぼかしを適用して
     * 元のシーンカラーに加算合成します。
     *
     * PostProcessStackに追加して使用するポストプロセスパスです。
     * ToneMappingPassの前に配置してください。
     *
     * 標準経路では RenderGraph named resource から入力を読み取り、
     * "BloomSceneColor" graph output として後段へ渡します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * 入力:
     * - "SceneColor" : HDRライティング結果 (R16G16B16A16_FLOAT)
     *
     * 出力:
     * - "BloomSceneColor" : ブルーム適用済みHDRカラー (R16G16B16A16_FLOAT)
     *   ※ legacy/fallback bridge では "SceneColor" を上書き登録する
     */
    class BloomPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings ブルーム設定
         */
        explicit BloomPass(const BloomSettings &settings = BloomSettings{});

        /**
         * @brief デストラクタ
         */
        ~BloomPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "BloomPass"; }

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
        void SetInputPass(const SSRPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetSceneColorHandle() const { return m_OutputHandle; }

        /**
         * @brief 輝度閾値を設定
         * @param threshold 閾値
         */
        void SetThreshold(float threshold) { m_Settings.Threshold = threshold; }

        /**
         * @brief ブルーム強度を設定
         * @param intensity 強度
         */
        void SetIntensity(float intensity) { m_Settings.Intensity = intensity; }

        /**
         * @brief ブラー半径を設定
         * @param radius 半径（ピクセル単位）
         */
        void SetRadius(float radius) { m_Settings.Radius = radius; }

        /**
         * @brief ソフト閾値の膝を設定
         * @param softKnee 膝の値（0-1）
         */
        void SetSoftKnee(float softKnee) { m_Settings.SoftKnee = softKnee; }

        /**
         * @brief 現在の設定を取得
         * @return ブルーム設定の参照
         */
        const BloomSettings &GetSettings() const { return m_Settings; }

    private:
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext &context,
                              const RHI::TexturePtr& sceneColorPtr,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext &context) const;

        // 設定
        BloomSettings m_Settings;

        // 出力テクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_OutputHandle;
        RGResourceHandle m_InputSceneColorHandle;

        // パイプラインリソース
        RHI::RenderPassPtr m_BloomRenderPass;
        RHI::FramebufferPtr m_BloomFramebuffer;
        RHI::PipelinePtr m_BloomPipeline;
        RHI::ShaderPtr m_BloomVertexShader;
        RHI::ShaderPtr m_BloomFragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::DescriptorSetPtr m_BloomDescriptorSet;
        RHI::SamplerPtr m_SceneColorSampler;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;
        const SSRPass* m_InputPass = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
