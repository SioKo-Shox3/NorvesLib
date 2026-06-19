#pragma once

#include "IViewPass.h"
#include "NeuralBRDFData.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief ライティングパス設定
     */
    struct LightingPassSettings
    {
        /** @brief 出力HDRカラーバッファのフォーマット */
        RHI::Format OutputFormat = RHI::Format::R16G16B16A16_FLOAT;

        /** @brief アンビエントライトの強度 */
        float AmbientIntensity = 0.1f;

        /** @brief アンビエントライトの色 */
        float AmbientColor[3] = {1.0f, 1.0f, 1.0f};

        /** @brief HDR環境マップパス（空の場合はIBL無効、フォールバックアンビエント使用） */
        Container::String EnvironmentMapPath;

        /** @brief IBL強度 */
        float IBLIntensity = 0.2f;

        /** @brief Neural BRDFウェイトファイルパス（空の場合は解析的BRDFを使用） */
        Container::String NeuralBRDFWeightPath;
    };

    /**
     * @brief ライティングパス（遅延ライティング）
     *
     * GBufferを入力としてフルスクリーン描画でPBRライティングを計算し、
     * HDRカラーバッファに出力するパス。
     *
     * 標準経路では RenderGraph named resource から入力を読み取り、graph output に出力します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * 入力:
     * - "GBuffer_Albedo"   : アルベド
     * - "GBuffer_Normal"   : ワールド法線
     * - "GBuffer_Material" : メタリック/ラフネス/AO
     * - "GBuffer_Emissive" : エミッシブ（HDR自発光）
     * - "GBuffer_Depth"    : 深度
     *
     * 出力:
     * - "SceneColor"       : HDRライティング結果 (R16G16B16A16_FLOAT)
     * - "SceneDepth"       : 深度のコピー（GBuffer_Depthのエイリアス）
     *
     * ライトデータはSceneViewのLightProxyから収集してUBOにパックします。
     */
    class SceneView;
    class GBufferPass;
    class SSAOPass;

    class LightingPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings ライティングパス設定
         */
        explicit LightingPass(const LightingPassSettings& settings = LightingPassSettings{});

        /**
         * @brief SceneViewを設定
         * @param sceneView SceneView参照（LightProxy取得用）
         */
        void SetSceneView(SceneView* sceneView) { m_SceneView = sceneView; }

        /**
         * @brief Legacy bridge fallback 用のGBuffer参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetGBufferPass(const GBufferPass* gbufferPass) { m_GBufferPass = gbufferPass; }

        /**
         * @brief Legacy bridge fallback 用のSSAO参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetSSAOPass(const SSAOPass* ssaoPass) { m_SSAOPass = ssaoPass; }

        /**
         * @brief デストラクタ
         */
        ~LightingPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "LightingPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        // ========================================
        // IRenderGraphPass実装
        // ========================================

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        // ========================================
        // 出力アクセス
        // ========================================

        /**
         * @brief HDRシーンカラーテクスチャを取得
         */
        RHI::ITexture* GetSceneColorTexture() const { return m_SceneColorTexture.get(); }
        RGResourceHandle GetSceneColorHandle() const { return m_SceneColorHandle.ToResourceHandle(); }

    private:
        /**
         * @brief ライト情報をGPUバッファにパック
         * @param context 描画コンテキスト
         * @param bShadowAvailable シャドウマップが利用可能か
         * @param bSSAOAvailable SSAOテクスチャが利用可能か
         */
        void UpdateLightBuffer(ViewRenderContext& context, bool bShadowAvailable, bool bSSAOAvailable);

        uint32_t ResolveLightingWidth(const ViewRenderContext& context) const;
        uint32_t ResolveLightingHeight(const ViewRenderContext& context) const;
        bool CreateLightingResources(uint32_t width, uint32_t height, ViewRenderContext& context);
        bool PrepareLightingOutput(uint32_t width,
                                   uint32_t height,
                                   const RHI::TexturePtr& sceneColorTexture,
                                   bool bUseRenderGraphInitialState,
                                   ViewRenderContext& context);
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
            AttachmentSignature SceneColor;
            bool bValid = false;
        };

        bool AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                       const AttachmentSignature& rhs) const;
        bool RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                       const RenderPassSignature& rhs) const;
        RenderPassSignature CreateLightingRenderPassSignature(uint32_t width,
                                                              uint32_t height,
                                                              const RHI::TexturePtr& sceneColorTexture,
                                                              bool bUseRenderGraphInitialState) const;
        bool EnsureLightingRenderPass(const RenderPassSignature& signature);
        bool EnsureLightingFramebuffer(uint32_t width,
                                       uint32_t height,
                                       const RHI::TexturePtr& sceneColorTexture);
        bool EnsureLightingDescriptorSet();
        bool EnsureLightingPipeline();
        void ExecuteWithInputs(ViewRenderContext& context,
                               const RHI::TexturePtr& albedoTexture,
                               const RHI::TexturePtr& normalTexture,
                               const RHI::TexturePtr& materialTexture,
                               const RHI::TexturePtr& depthTexture,
                               const RHI::TexturePtr& emissiveTexture,
                               const RHI::TexturePtr& ssaoTexture,
                               const RHI::TexturePtr& shadowMapTexture,
                               bool bRegisterLegacyOutputs);
        void RegisterOutputs(ViewRenderContext& context,
                             const RHI::TexturePtr& sceneColorTexture,
                             const RHI::TexturePtr& depthTexture) const;
        bool TryEnqueueNativeTransitionPass(ViewRenderContext& context) const;

        /**
         * @brief HDR環境マップをロード（ミップマップ付きRGBA16_FLOAT）
         * @param path 環境マップファイルパス
         * @return ロード成功時true
         */
        bool LoadEnvironmentMap(const Container::String& path);

        /**
         * @brief BRDF LUTをCPUで生成（split-sum近似）
         * @return 生成成功時true
         */
        bool GenerateBRDFLut();

        // 設定
        LightingPassSettings m_Settings;

        // 出力テクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_SceneColorTexture;
        RGTextureHandle m_SceneColorHandle;
        RGResourceHandle m_GBufferAlbedoHandle;
        RGResourceHandle m_GBufferNormalHandle;
        RGResourceHandle m_GBufferMaterialHandle;
        RGResourceHandle m_GBufferDepthHandle;
        RGResourceHandle m_GBufferEmissiveHandle;
        RGResourceHandle m_SSAOBlurredHandle;

        // ライティング用リソース
        RHI::RenderPassPtr m_LightingRenderPass;
        RHI::FramebufferPtr m_LightingFramebuffer;
        RHI::PipelinePtr m_LightingPipeline;
        RHI::ShaderPtr m_LightingVertexShader;
        RHI::ShaderPtr m_LightingFragmentShader;
        RHI::BufferPtr m_LightDataBuffer;
        RHI::BufferPtr m_LightArrayBuffer;
        RHI::DescriptorSetPtr m_LightingDescriptorSet;
        RHI::SamplerPtr m_GBufferSampler;

        // IBL (Image-Based Lighting) リソース
        RHI::TexturePtr m_EnvironmentTexture; ///< HDR環境マップ（equirectangular）
        RHI::TexturePtr m_BrdfLutTexture;     ///< BRDF LUT（split-sum近似）
        RHI::SamplerPtr m_IBLSampler;         ///< IBL用サンプラー（Linear + ミップマップ）
        uint32_t m_EnvironmentMipLevels = 1;  ///< 環境マップのミップレベル数
        bool m_bIBLAvailable = false;         ///< IBLリソースが利用可能か

        // Neural BRDF リソース
        NeuralBRDFData m_NeuralBRDFData;         ///< 学習済みBRDFデータ
        RHI::BufferPtr m_NeuralBRDFWeightBuffer; ///< GPU側重みStorageBuffer
        bool m_bNeuralBRDFAvailable = false;     ///< Neural BRDFが利用可能か

        // デバイス参照
        RHI::IDevice* m_Device = nullptr;

        // SceneView参照（LightProxy取得用）
        SceneView* m_SceneView = nullptr;
        const GBufferPass* m_GBufferPass = nullptr;
        const SSAOPass* m_SSAOPass = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bUsingRenderGraphResources = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
        uint32_t m_FramebufferWidth = 0;
        uint32_t m_FramebufferHeight = 0;
        RenderPassSignature m_RenderPassSignature;
    };

} // namespace NorvesLib::Core::Rendering
