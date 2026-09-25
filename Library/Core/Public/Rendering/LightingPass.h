#pragma once

#include "IViewPass.h"
#include "NeuralBRDFData.h"
#include "Rendering/DDGIProbePass.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RayTracingShadowPass.h"
#include "Rendering/RTGIContract.h"
#include "Rendering/SkyAtmosphere.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    struct GPULightingParams;

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

        /** @brief 環境放射輝度のスケール [cd/m²] */
        float EnvironmentLuminanceScaleNits = 1.0f;

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
     * ライトデータはSceneViewのLightProxyから収集してSSBOにパックします。
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
         * @brief legacy/fallback bridge としてSharedResourceRegistryへ登録するか
         *
         * 既定は互換のためtrue。production deferred pipelineでは RenderGraph named resource
         * を主経路にするためfalseにします。
         */
        void SetRegisterLegacyBridge(bool bRegister)
        {
            m_bRegisterLegacyBridge = bRegister;
        }

        /**
         * @brief 検証captureへ渡す直近フレームのRTGI資源を取得する
         *
         * 指定フレームでRTGIが成功した場合だけ、デノイズ後の間接光
         * （RTGIDiffuseIndirect）または書き込み済みの履歴age（RTGIHistoryAge）と、
         * その資源の現在の状態を返す。
         */
        bool TryGetRTGICaptureTexture(FrameCaptureSourceKind kind,
                                      uint64_t frameNumber,
                                      RHI::TexturePtr& outTexture,
                                      RHI::ResourceState& outState) const;

        /**
         * @brief デストラクタ
         */
        ~LightingPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "LightingPass"; }

        /** @brief 構築時の設定（ニューラルBRDFの重みの経路など） */
        const LightingPassSettings& GetSettings() const { return m_Settings; }

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
        friend struct DDGIProbeRayQueryVulkanTestAccess;
        friend struct RTGIDiffuseIndirectVulkanTestAccess;

        /**
         * @brief ライト情報をGPUバッファ向けに構築
         *
         * 更新に成功したフレームは、同じViewRenderContextへ物理ライトSSBO・方向影・
         * IBLリソースを公開します。ForwardPassはこの公開値だけを使用します。
         * outParamsが有効な場合はGPU定数を返し、GPUバッファの更新を呼出元へ委ねます。
         * @param context 描画コンテキスト
         * @param bShadowAvailable シャドウマップが利用可能か
         * @param bSSAOAvailable SSAOテクスチャが利用可能か
         * @param outParams GPU定数の出力先。nullptrの場合はGPUバッファへ直接反映します。
         */
        bool UpdateLightBuffer(ViewRenderContext& context,
                               bool bShadowAvailable,
                               bool bSSAOAvailable,
                               GPULightingParams* outParams = nullptr);
        bool EnsureLightArrayBufferCapacity(uint32_t requiredLightCount);
        uint32_t GetLightArrayBufferSizeBytes() const;

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
        bool CreateLightingDescriptorSet(RHI::DescriptorSetPtr& outDescriptorSet);
        bool EnsureLightingDescriptorSet();
        bool EnsureLightingPipeline();
        void ExecuteWithInputs(ViewRenderContext& context,
                               const RHI::TexturePtr& albedoTexture,
                               const RHI::TexturePtr& normalTexture,
                               const RHI::TexturePtr& materialTexture,
                               const RHI::TexturePtr& depthTexture,
                               const RHI::TexturePtr& velocityTexture,
                               const RHI::TexturePtr& emissiveTexture,
                               const RHI::TexturePtr& ssaoTexture,
                               const RHI::TexturePtr& shadowMapTexture,
                               const RHI::TexturePtr& rtgiDiffuseIndirectTexture,
                               bool bRegisterLegacyOutputs);
        bool EnsureRTGIComputePipeline(ViewRenderContext& context);
        bool EnsureRTGIDenoiserResources(ViewRenderContext& context,
                                          uint32_t width,
                                          uint32_t height);
        bool EnsureRTGIHistoryTextures(uint32_t width, uint32_t height);
        void InvalidateRTGIHistory();
        bool ExecuteRTGI(ViewRenderContext& context,
                         const RHI::TexturePtr& albedoTexture,
                         const RHI::TexturePtr& normalTexture,
                         const RHI::TexturePtr& materialTexture,
                         const RHI::TexturePtr& depthTexture,
                         const RHI::TexturePtr& velocityTexture,
                         const RHI::TexturePtr& rtgiDiffuseIndirectTexture,
                         const GPULightingParams& lightingParams);
        bool ExecuteRTGIDenoiser(ViewRenderContext& context,
                                 const RHI::TexturePtr& temporalRadiance,
                                 const RHI::TexturePtr& confidenceTexture,
                                 const RHI::TexturePtr& ageTexture,
                                 const RHI::TexturePtr& depthTexture,
                                 const RHI::TexturePtr& normalTexture,
                                 const RHI::TexturePtr& materialTexture);
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
        bool GenerateValidationSnapshots();
        bool EnsureSkyAtmosphereIbl(const SkyAtmosphereParameters& parameters,
                                    uint32_t radianceWidth,
                                    uint32_t radianceHeight);

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
        RGResourceHandle m_GBufferVelocityHandle;
        RGResourceHandle m_GBufferEmissiveHandle;
        RGResourceHandle m_SSAOBlurredHandle;
        RGResourceHandle m_ShadowMapHandle;
        RGResourceHandle m_RTGIDiffuseIndirectHandle;

        // ライティング用リソース
        RHI::RenderPassPtr m_LightingRenderPass;
        RHI::FramebufferPtr m_LightingFramebuffer;
        RHI::PipelinePtr m_LightingPipeline;
        RHI::ShaderPtr m_LightingVertexShader;
        RHI::ShaderPtr m_LightingFragmentShader;
        RHI::ShaderPtr m_RTGIComputeShader;
        RHI::PipelinePtr m_RTGIComputePipeline;
        RHI::DescriptorSetPtr m_RTGIComputeDescriptorSet;
        RHI::ShaderPtr m_RTGIDenoiserShader;
        RHI::PipelinePtr m_RTGIDenoiserPipeline;
        RHI::DescriptorSetPtr m_RTGIDenoiserDescriptorSet;
        RHI::TexturePtr m_RTGIDenoisedTexture;
        RHI::ResourceState m_RTGIDenoisedTextureState = RHI::ResourceState::Undefined;
        uint32_t m_RTGIDenoisedWidth = 0u;
        uint32_t m_RTGIDenoisedHeight = 0u;
        RHI::BufferPtr m_RTGIComputeParametersBuffer;
        RHI::BufferPtr m_RTGIComputeInstanceDataBuffer;
        /** @brief RTGIが光源標本する発光instanceの表 */
        RHI::BufferPtr m_RTGIComputeEmitterBuffer;
        Container::VariableArray<RHI::BufferPtr> m_RTGIGeometryBuffers;
        struct RTGIHistoryTextureSet
        {
            RHI::TexturePtr Radiance;
            RHI::TexturePtr Age;
            RHI::TexturePtr Confidence;
            RHI::TexturePtr Depth;
            RHI::TexturePtr Normal;
            RHI::TexturePtr Material;

            void Clear()
            {
                Radiance.reset();
                Age.reset();
                Confidence.reset();
                Depth.reset();
                Normal.reset();
                Material.reset();
            }
        };
        RTGIHistoryTextureSet m_RTGIHistoryTextures[2];
        RHI::ResourceState m_RTGIHistorySlotState[2] = {
            RHI::ResourceState::Undefined,
            RHI::ResourceState::Undefined};
        uint64_t m_RTGIComputeInstanceDataCapacity = 0;
        uint64_t m_RTGIComputeEmitterCapacity = 0;
        uint32_t m_RTGIHistoryWidth = 0;
        uint32_t m_RTGIHistoryHeight = 0;
        uint32_t m_RTGIHistoryWriteIndex = 0;
        uint32_t m_RTGIHistoryAgeFrames = 0;
        uint64_t m_RTGIHistoryFrameNumber = 0;
        uint64_t m_RTGIHistorySceneRevision = 0;
        uint64_t m_RTGIHistoryLightRevision = 0;
        RTGIRayQueryCapability m_RTGIHistoryCapability;
        uint32_t m_RTGIHistoryLightWeightLimitedFrames = 0;
        /** @brief 視点（逆ビュー射影・位置）とレイトレーシングのinstanceの前フレームの署名 */
        uint64_t m_RTGIStaticSignature = 0;
        /** @brief 視点・光源・シーンが変わらなかった連続フレーム数 */
        uint32_t m_RTGIStaticFrames = 0;
        /** @brief 直近のdispatchで使った画素ごとの履歴の年齢の上限 */
        uint32_t m_RTGIHistoryAgeCap = RTGIHistoryMaximumAge;
        bool m_bRTGIStaticSignatureValid = false;
        bool m_bRTGIHistoryValid = false;
        bool m_bRTGIHistoryFrameNumberValid = false;
        bool m_bRTGIHistoryCapabilityValid = false;
        bool m_bRTGIHistoryLightRevisionValid = false;
        bool m_bRTGIComputeUnavailable = false;
        bool m_bRTGIDenoiserUnavailable = false;
        DDGIProbePass m_DDGIProbePass;
        RayTracingShadowPass m_RayTracingShadowPass;
        RHI::BufferPtr m_LightDataBuffer;
        RHI::BufferPtr m_LightArrayBuffer;
        Container::VariableArray<RHI::BufferPtr> m_RetiredLightArrayBuffers;
        RHI::DescriptorSetPtr m_LightingDescriptorSet;
        RHI::SamplerPtr m_GBufferSampler;

        // IBL (Image-Based Lighting) リソース
        RHI::TexturePtr m_EnvironmentTexture; ///< HDR環境マップ（equirectangular）
        RHI::TexturePtr m_BrdfLutTexture;     ///< BRDF LUT（split-sum近似）
        RHI::TexturePtr m_DiffuseIrradianceTexture;
        RHI::TexturePtr m_PrefilteredSpecularTexture;
        RHI::TexturePtr m_SkyAtmosphereDiffuseIrradianceTexture;
        RHI::TexturePtr m_SkyAtmospherePrefilteredSpecularTexture;
        /** @brief 地表から見た空の環境（視線方向の透過率込み）。RTGI・DDGIの不交差へ公開する。 */
        RHI::TexturePtr m_SkyAtmosphereEnvironmentTexture;
        RHI::TexturePtr m_ValidationRaw250EnvironmentTexture;
        RHI::TexturePtr m_ValidationRaw250DiffuseIrradianceTexture;
        RHI::TexturePtr m_ValidationRaw250Texture;
        RHI::TexturePtr m_ValidationRaw252EnvironmentTexture;
        RHI::TexturePtr m_ValidationRaw252DiffuseIrradianceTexture;
        RHI::TexturePtr m_ValidationRaw252PrefilteredSpecularTexture;
        RHI::TexturePtr m_DefaultBlackTexture;
        RHI::TexturePtr m_DefaultShadowMapArrayTexture;
        RHI::TexturePtr m_DefaultDDGIIrradianceAtlas;
        RHI::TexturePtr m_DefaultDDGIDistanceAtlas;
        RHI::SamplerPtr m_IBLSampler;         ///< 環境放射輝度用サンプラー
        RHI::SamplerPtr m_DiffuseIrradianceSampler;
        RHI::SamplerPtr m_PrefilteredSpecularSampler;
        RHI::SamplerPtr m_DfgSampler;
        RHI::SamplerPtr m_DDGISampler;
        uint32_t m_EnvironmentMipLevels = 1;  ///< 環境マップのミップレベル数
        bool m_bIBLAvailable = false;         ///< IBLリソースが利用可能か
        SkyAtmosphereParameters m_SkyAtmosphereIblParameters;
        uint32_t m_SkyAtmosphereIblRadianceWidth = 0;
        uint32_t m_SkyAtmosphereIblRadianceHeight = 0;
        bool m_bSkyAtmosphereIblCacheValid = false;
        bool m_bSkyAtmosphereIblAvailable = false;

        // Neural BRDF リソース
        NeuralBRDFData m_NeuralBRDFData;         ///< 学習済みBRDFデータ
        RHI::BufferPtr m_NeuralBRDFWeightBuffer; ///< GPU側重みStorageBuffer
        RHI::BufferPtr m_DefaultNeuralBRDFWeightBuffer;
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
        uint32_t m_LightArrayCapacity = 0;
        bool m_bRegisterLegacyBridge = true;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bUsingRenderGraphResources = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
        uint32_t m_FramebufferWidth = 0;
        uint32_t m_FramebufferHeight = 0;
        RenderPassSignature m_RenderPassSignature;
    };

} // namespace NorvesLib::Core::Rendering
