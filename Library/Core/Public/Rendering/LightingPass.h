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

        /**
         * @brief 接触影のレイの長さ（m）。0以下で無効
         *
         * 影を掛ける方向光（空の太陽）とキューブシャドウを持つ点光源について、GBufferの深度を
         * 光の方向へこの長さだけ画面空間で辿り、CSM・キューブシャドウの解像度では出ない接地部の
         * 細い影を影の結果へ掛けます。
         */
        float ContactShadowLength = 0.3f;

        /**
         * @brief 接触影で、深度バッファの面の奥にこの厚さ（m）までの点を遮られたとみなす
         *
         * 実際の厚さは、この値・レイの1段の長さ・受け手の位置の1画素の世界の幅の最大です。
         * 薄くすると、見えない側の面から物体へ入るレイ（逆光の接地部など）を見落とします。
         */
        float ContactShadowThickness = 0.3f;
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
     * - "LightingIndirectSpecular"    : SceneColorへ足した環境光の鏡面反射（露出後、R16G16B16A16_FLOAT）
     * - "LightingSpecularReflectance" : その反射率（鏡面の遮蔽込み、R8G8B8A8_UNORM）。SSRPassが
     *                                   環境光の鏡面反射を画面の反射へ置き換えるのに使う
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

        /**
         * @brief SceneColorへ足した環境光の鏡面反射（露出後）と、その反射率の出力
         */
        RGResourceHandle GetIndirectSpecularHandle() const { return m_IndirectSpecularHandle.ToResourceHandle(); }
        RGResourceHandle GetSpecularReflectanceHandle() const
        {
            return m_SpecularReflectanceHandle.ToResourceHandle();
        }

        /**
         * @brief 太陽の VSM の読み出しの統計として読み戻した実行の数と、その合計の逃げた標本の数
         *
         * 書いたフレームの提出の完了が確かめられた枠だけを足す（提出前・実行中の枠は読まない）。
         */
        uint64_t GetVsmStatsHarvestedExecuteCount() const { return m_VsmStatsHarvestedExecutes; }
        uint64_t GetVsmFallbackSampleCount() const { return m_VsmFallbackSamples; }
        /** @brief 点光源の VSM の読み出しで、粗い段へ逃げた PCF の標本の数の合計（統計の語 1） */
        uint64_t GetVsmPointFallbackSampleCount() const { return m_VsmPointFallbackSamples; }

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

        // ライティングの描画先（MRTの3枚。順にシェーダーの出力 location 0・1・2）
        struct LightingOutputTargets
        {
            RHI::TexturePtr SceneColor;
            RHI::TexturePtr IndirectSpecular;
            RHI::TexturePtr SpecularReflectance;
        };

        uint32_t ResolveLightingWidth(const ViewRenderContext& context) const;
        uint32_t ResolveLightingHeight(const ViewRenderContext& context) const;
        bool CreateLightingResources(uint32_t width, uint32_t height, ViewRenderContext& context);
        // 旧経路（RenderGraphを通らない）で使う、SceneColorと環境光の鏡面反射の出力を作る
        bool CreateLegacyLightingOutputs(uint32_t width, uint32_t height, LightingOutputTargets& outTargets) const;
        bool PrepareLightingOutput(uint32_t width,
                                   uint32_t height,
                                   const LightingOutputTargets& targets,
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
            AttachmentSignature IndirectSpecular;
            AttachmentSignature SpecularReflectance;
            bool bValid = false;
        };

        bool AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                       const AttachmentSignature& rhs) const;
        bool RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                       const RenderPassSignature& rhs) const;
        RenderPassSignature CreateLightingRenderPassSignature(uint32_t width,
                                                              uint32_t height,
                                                              const LightingOutputTargets& targets,
                                                              bool bUseRenderGraphInitialState) const;
        bool EnsureLightingRenderPass(const RenderPassSignature& signature);
        bool EnsureLightingFramebuffer(uint32_t width,
                                       uint32_t height,
                                       const LightingOutputTargets& targets);
        bool CreateLightingDescriptorSet(RHI::DescriptorSetPtr& outDescriptorSet);
        bool EnsureLightingDescriptorSet();
        /**
         * @brief この Execute が使う資源の組を選び、組の資源を m_LightDataBuffer などの現在の枠へ載せる
         *
         * 通し番号が前回と違えば先頭の組から使い直し、同じ間は Execute のたびに次の組へ進む。組が足りなければ作る。
         * 上限（MaxExecuteResourceSets）を超えたとき・組を作れないときは false（呼び出し側は描かない）。
         * @param frameSerial 今のフレームの通し番号（ViewRenderContext::ResolveRenderFrameSerial）
         */
        bool AcquireExecuteResourceSet(uint64_t frameSerial);
        /** @brief 現在の枠（m_LightDataBuffer などと m_LightingDescriptorSet）を組の配列の active の位置へ書き戻す */
        void StoreActiveExecuteResourceSet();
        /** @brief 組の配列の index の位置の組を現在の枠へ載せ、active を index にする */
        void LoadExecuteResourceSet(uint32_t index);
        /** @brief 現在の枠が空のとき、ライトの定数・配列・VSM の 3 つのバッファと descriptor set を作る */
        bool CreateExecuteResourceSet();
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
                             const LightingOutputTargets& targets,
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
        RHI::TexturePtr m_IndirectSpecularTexture;
        RHI::TexturePtr m_SpecularReflectanceTexture;
        RGTextureHandle m_SceneColorHandle;
        RGTextureHandle m_IndirectSpecularHandle;
        RGTextureHandle m_SpecularReflectanceHandle;
        RGResourceHandle m_GBufferAlbedoHandle;
        RGResourceHandle m_GBufferNormalHandle;
        RGResourceHandle m_GBufferMaterialHandle;
        RGResourceHandle m_GBufferDepthHandle;
        RGResourceHandle m_GBufferVelocityHandle;
        RGResourceHandle m_GBufferEmissiveHandle;
        RGResourceHandle m_SSAOBlurredHandle;
        RGResourceHandle m_ShadowMapHandle;
        RGResourceHandle m_PointShadowCubeHandle;
        RGResourceHandle m_RTGIDiffuseIndirectHandle;
        /** @brief 太陽の VSM（--shadow-method=vsm）のページの表・物理ページのプール。VirtualShadowMapPass が公開したときだけ有効 */
        RGResourceHandle m_VsmPageTableHandle;
        RGResourceHandle m_VsmPoolHandle;

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
        /** @brief 履歴の放射輝度に掛かっているプリエクスポージャ（露出が変わったら比で掛け直す） */
        float m_RTGIHistoryPreExposure = 1.0f;
        /** @brief 直近に記録した間接光の出どころ（RTGIIndirectLightingSource。0xFFは未記録） */
        uint8_t m_LoggedIndirectLightingSource = 0xFFu;
        /** @brief 視点（ジッタを除いた逆ビュー射影・位置）とレイトレーシングのinstanceの前フレームの署名 */
        uint64_t m_RTGIStaticSignature = 0;
        /** @brief 視点・光源・シーンが変わらなかった連続フレーム数 */
        uint32_t m_RTGIStaticFrames = 0;
        /** @brief 直近のdispatchで使った画素ごとの履歴の年齢の上限 */
        uint32_t m_RTGIHistoryAgeCap = RTGIHistoryMaximumAge;
        /**
         * @brief RTGIの低食い違い列の番号。描画フレーム番号が変わったdispatchごとに1ずつ進め、同じ描画フレーム番号の
         * dispatchは同じ列の番号を使う（描画フレーム番号は1回の描画の間に不規則に複数進むことがある）。
         */
        uint32_t m_RTGISampleIndex = 0;
        /** @brief m_RTGISampleIndex を最後に進めたときの描画フレーム番号 */
        uint64_t m_RTGISampleFrameNumber = 0;
        bool m_bRTGISampleFrameNumberValid = false;
        bool m_bRTGIStaticSignatureValid = false;
        bool m_bRTGIHistoryValid = false;
        bool m_bRTGIHistoryFrameNumberValid = false;
        bool m_bRTGIHistoryCapabilityValid = false;
        bool m_bRTGIHistoryLightRevisionValid = false;
        bool m_bRTGIComputeUnavailable = false;
        bool m_bRTGIDenoiserUnavailable = false;
        DDGIProbePass m_DDGIProbePass;
        RayTracingShadowPass m_RayTracingShadowPass;
        /**
         * @brief 1 回の Execute が自分で使う、書き換えるバッファと descriptor set の組
         *
         * 同じ SceneView が 1 フレームに複数のビューポートを描くと LightingPass が複数回 Execute される。
         * 照明の定数・ライトの配列・VSM のパラメータとスライスの表は Execute ごとに書き換えるうえ、descriptor set は
         * 記録済みのコマンドが参照するので、提出前に次の Execute が同じ資源を書くと先に記録した描画を壊す。
         * そのため Execute ごとに別の組を使う。
         */
        struct ExecuteResourceSet
        {
            RHI::BufferPtr LightData;
            RHI::BufferPtr LightArray;
            RHI::BufferPtr VsmSample;
            RHI::BufferPtr VsmPointSample;
            RHI::BufferPtr VsmSlice;
            RHI::DescriptorSetPtr DescriptorSet;
            uint32_t LightArrayCapacity = 0;
        };
        /** @brief 1 フレームに描ける Execute の数の上限（超えた Execute はエラーを 1 回出して描かない） */
        static constexpr uint32_t MaxExecuteResourceSets = 4;
        /**
         * @brief 組の配列。m_ActiveExecuteResourceSet の位置は古く、現在の枠（下の 7 つのメンバ）が最新
         *
         * 組は通し番号が変わったときだけ先頭から使い直す。前のフレームの提出は、スワップチェーンの BeginFrame が
         * 飛行中のフェンスを待つ（飛行中のフレームは 1 つ）ことで、次の記録の開始までに完了している。
         */
        ExecuteResourceSet m_ExecuteResourceSets[MaxExecuteResourceSets];
        uint32_t m_ExecuteResourceSetCount = 0;
        uint32_t m_ActiveExecuteResourceSet = 0;
        /** @brief 今のフレーム（m_ExecuteResourceSetFrameSerial）で渡した組の数 */
        uint32_t m_ExecuteResourceSetCursor = 0;
        uint64_t m_ExecuteResourceSetFrameSerial = 0;
        bool m_bExecuteResourceSetOverflowLogged = false;

        // 現在の枠（Execute が使っている組）。組の取り替えで差し替わる
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
        /** @brief 点光源のキューブシャドウが無いフレームにbinding 20へ置く1×1のキューブ配列（距離1） */
        RHI::TexturePtr m_DefaultPointShadowCubeTexture;
        /** @brief このフレームにShadowMapPassが描いたキューブ配列（無いフレームは空） */
        RHI::TexturePtr m_FramePointShadowCubeTexture;
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
        /** @brief 太陽の VSM を読むパラメータ（GPUVsmSampleParams の定数バッファ。VSM が使えないフレームは無効の値） */
        RHI::BufferPtr m_VsmSampleBuffer;
        /** @brief 太陽の VSM のスライスの表（GPUVsmSlice の配列。ページの一辺・texel・範囲の原点・ページの表の先頭。VSM が使えないフレームは何も無い表） */
        RHI::BufferPtr m_VsmSliceBuffer;
        /** @brief 点光源の VSM を読むパラメータ（GPUVsmPointSampleParams の定数バッファ。使えないフレームは無効の値 = キューブのまま） */
        RHI::BufferPtr m_VsmPointSampleBuffer;
        /** @brief 今フレームに RenderGraph から解決した太陽の VSM のページの表・物理ページのプール（無ければ null） */
        RHI::BufferPtr m_FrameVsmPageTable;
        RHI::BufferPtr m_FrameVsmPool;

        /**
         * @brief 太陽の VSM の読み出しの統計の読み戻し先（照明のシェーダーが storage buffer へ数え、ホストが数回後の実行で読む）
         *
         * [0] = 太陽の VSM で自分の段のページが無く、粗い段へ逃げた PCF の標本の数、[1] = 点光源の VSM の同じ数。--shadow-probe が無効でも数える。
         * 書いたフレームの通し番号を持ち、その提出の完了が確かめられた（ViewRenderContext::CompletedRenderFrameSerial 以下）
         * 枠だけを読んで空ける。
         */
        struct VsmStatsSlot
        {
            RHI::BufferPtr Buffer;
            const uint32_t* Mapped = nullptr;
            uint64_t FrameSerial = 0;
            bool bPending = false;
        };
        static constexpr uint32_t VsmStatsSlotCount = 16;
        static constexpr uint32_t VsmStatsBytes = 16;
        VsmStatsSlot m_VsmStatsSlots[VsmStatsSlotCount];
        /** @brief 空きの枠が無いとき・枠を作れないときに束ねる、読まない統計の置き場（シェーダーの数え上げが他の資源へ書かないようにする） */
        RHI::BufferPtr m_VsmStatsSink;
        /** @brief 読み戻した実行の数と、その合計の逃げた標本の数 */
        uint64_t m_VsmStatsHarvestedExecutes = 0;
        uint64_t m_VsmFallbackSamples = 0;
        uint64_t m_VsmPointFallbackSamples = 0;

        /**
         * @brief 次の統計の書き込み先を取り、0 に戻して返す
         *
         * 先に、提出の完了が確かめられた枠を読んで空ける。どの枠も GPU の完了が未確認のとき、作れない・写像できないときは null
         * （GPU が書いているかもしれない枠は上書きしない）。
         * @param frameSerial 今のフレームの通し番号（ViewRenderContext::ResolveRenderFrameSerial）
         * @param completedSerial 提出の完了が確かめられた最大の通し番号
         */
        RHI::BufferPtr AcquireVsmStatsSlot(uint64_t frameSerial, uint64_t completedSerial);
        /** @brief 書いたフレームの提出が完了した枠を合計へ足して空ける */
        void HarvestCompletedVsmStats(uint64_t completedSerial);
        /** @brief 残っている統計をすべて合計へ足す（GPU が書き終えた後の終了時） */
        void HarvestVsmStats();
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
        RHI::ITexture* m_FramebufferIndirectSpecularTexture = nullptr;
        RHI::ITexture* m_FramebufferSpecularReflectanceTexture = nullptr;
        uint32_t m_FramebufferWidth = 0;
        uint32_t m_FramebufferHeight = 0;
        RenderPassSignature m_RenderPassSignature;
    };

} // namespace NorvesLib::Core::Rendering
