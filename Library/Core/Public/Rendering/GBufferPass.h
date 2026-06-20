#pragma once

#include "IViewPass.h"
#include "SceneRenderer.h"
#include "DynamicUniformAllocator.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class SceneView;

    /**
     * @brief GBufferパス設定
     */
    struct GBufferPassSettings
    {
        /** @brief GBufferの幅（0=スクリーン解像度と一致） */
        uint32_t Width = 0;

        /** @brief GBufferの高さ（0=スクリーン解像度と一致） */
        uint32_t Height = 0;

        /** @brief Albedoバッファのフォーマット */
        RHI::Format AlbedoFormat = RHI::Format::R8G8B8A8_UNORM;

        /** @brief Normalバッファのフォーマット */
        RHI::Format NormalFormat = RHI::Format::R16G16B16A16_FLOAT;

        /** @brief Metallic/Roughness/AOバッファのフォーマット */
        RHI::Format MaterialFormat = RHI::Format::R8G8B8A8_UNORM;

        /** @brief エミッシブバッファのフォーマット（HDR発光色） */
        RHI::Format EmissiveFormat = RHI::Format::R16G16B16A16_FLOAT;

        /** @brief Depthバッファのフォーマット */
        RHI::Format DepthFormat = RHI::Format::D32_FLOAT;
    };

    /**
     * @brief GBufferパス（ジオメトリ→GBuffer書き出し）
     *
     * 遅延レンダリングの最初のパス。
     * シーン内のメッシュをMRT（Multiple Render Targets）でGBufferに描画します。
     *
     * 出力GBuffer:
     * - RT0: Albedo (R8G8B8A8_UNORM)
     * - RT1: WorldNormal (R16G16B16A16_FLOAT)
     * - RT2: Metallic/Roughness/AO (R8G8B8A8_UNORM)
     * - RT3: Emissive (R16G16B16A16_FLOAT)
     * - DS:  Depth (D32_FLOAT)
     *
     * 標準経路では RenderGraph named resource として公開され、後続のLightingPassが参照します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * SharedResource登録名:
     * - "GBuffer_Albedo"
     * - "GBuffer_Normal"
     * - "GBuffer_Material"
     * - "GBuffer_Emissive"
     * - "GBuffer_Depth"
     */
    class GBufferPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings GBufferパス設定
         */
        explicit GBufferPass(const GBufferPassSettings& settings = GBufferPassSettings{});

        /**
         * @brief デストラクタ
         */
        ~GBufferPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "GBufferPass"; }

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
        // SceneView連携
        // ========================================

        /**
         * @brief 描画対象のSceneViewを設定します
         * @param sceneView SceneView
         */
        void SetSceneView(SceneView* sceneView) { m_SceneView = sceneView; }

        /**
         * @brief SceneRendererを設定します
         * @param renderer SceneRenderer
         */
        void SetSceneRenderer(SceneRenderer* renderer) { m_SceneRenderer = renderer; }

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

        // ========================================
        // GBufferアクセス
        // ========================================

        RHI::ITexture* GetAlbedoTexture() const { return m_AlbedoTexture.get(); }
        RHI::ITexture* GetNormalTexture() const { return m_NormalTexture.get(); }
        RHI::ITexture* GetMaterialTexture() const { return m_MaterialTexture.get(); }
        RHI::ITexture* GetEmissiveTexture() const { return m_EmissiveTexture.get(); }
        RHI::ITexture* GetDepthTexture() const { return m_DepthTexture.get(); }

        RGResourceHandle GetAlbedoHandle() const { return m_AlbedoHandle.ToResourceHandle(); }
        RGResourceHandle GetNormalHandle() const { return m_NormalHandle.ToResourceHandle(); }
        RGResourceHandle GetMaterialHandle() const { return m_MaterialHandle.ToResourceHandle(); }
        RGResourceHandle GetEmissiveHandle() const { return m_EmissiveHandle.ToResourceHandle(); }
        RGResourceHandle GetDepthHandle() const { return m_DepthHandle.ToResourceHandle(); }

    private:
        /**
         * @brief GBufferリソースを作成または更新します
         * @param width 幅
         * @param height 高さ
         * @param context 描画コンテキスト
         * @return 成功時true
         */
        bool CreateGBufferResources(uint32_t width, uint32_t height, ViewRenderContext& context);
        uint32_t ResolveGBufferWidth(const ViewRenderContext& context) const;
        uint32_t ResolveGBufferHeight(const ViewRenderContext& context) const;
        bool PrepareGBufferAttachments(uint32_t width,
                                       uint32_t height,
                                       const RHI::TexturePtr& albedo,
                                       const RHI::TexturePtr& normal,
                                       const RHI::TexturePtr& material,
                                       const RHI::TexturePtr& emissive,
                                       const RHI::TexturePtr& depth,
                                       bool bUseRenderGraphInitialStates);
        bool EnsureGBufferFramebuffer(uint32_t width,
                                      uint32_t height,
                                      const RHI::TexturePtr& albedo,
                                      const RHI::TexturePtr& normal,
                                      const RHI::TexturePtr& material,
                                      const RHI::TexturePtr& emissive,
                                      const RHI::TexturePtr& depth);
        bool EnsureGBufferPipeline();
        bool CreateGBufferPipelineVariant(RHI::PolygonMode polygonMode, RHI::PipelinePtr& outPipeline);
        RHI::PipelinePtr SelectGBufferPipeline(DebugViewMode mode) const;
        void EnqueueGBufferGeometryPass(ViewRenderContext& context,
                                        Container::TSharedPtr<Container::VariableArray<DrawCommand>> drawCommands,
                                        const RHI::Viewport &viewport,
                                        const RHI::ScissorRect &scissor,
                                        MeshResources *meshes) const;
        bool TryEnqueueNativeClearPass(ViewRenderContext& context,
                                       const RHI::Viewport &viewport,
                                       const RHI::ScissorRect &scissor,
                                       MeshResources *meshes) const;

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
            AttachmentSignature Albedo;
            AttachmentSignature Normal;
            AttachmentSignature Material;
            AttachmentSignature Emissive;
            AttachmentSignature Depth;
            bool bValid = false;
        };

        bool AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                       const AttachmentSignature& rhs) const;
        bool RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                       const RenderPassSignature& rhs) const;
        bool EnsureGBufferRenderPass(const RenderPassSignature& signature);
        RenderPassSignature CreateGBufferRenderPassSignature(uint32_t width,
                                                             uint32_t height,
                                                             const RHI::TexturePtr& albedo,
                                                             const RHI::TexturePtr& normal,
                                                             const RHI::TexturePtr& material,
                                                             const RHI::TexturePtr& emissive,
                                                             const RHI::TexturePtr& depth,
                                                             bool bUseRenderGraphInitialStates) const;

        // 設定
        GBufferPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView* m_SceneView = nullptr;
        SceneRenderer* m_SceneRenderer = nullptr;

        // GBufferテクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_AlbedoTexture;
        RHI::TexturePtr m_NormalTexture;
        RHI::TexturePtr m_MaterialTexture;
        RHI::TexturePtr m_EmissiveTexture;
        RHI::TexturePtr m_DepthTexture;

        RGTextureHandle m_AlbedoHandle;
        RGTextureHandle m_NormalHandle;
        RGTextureHandle m_MaterialHandle;
        RGTextureHandle m_EmissiveHandle;
        RGTextureHandle m_DepthHandle;

        // GBuffer用レンダーパス・フレームバッファ
        RHI::RenderPassPtr m_GBufferRenderPass;
        RHI::FramebufferPtr m_GBufferFramebuffer;

        // GBuffer用パイプライン
        RHI::PipelinePtr m_GBufferPipeline;
        RHI::PipelinePtr m_GBufferWireframePipeline;
        RHI::ShaderPtr m_GBufferVertexShader;
        RHI::ShaderPtr m_GBufferFragmentShader;

        // デバイス参照
        RHI::IDevice* m_Device = nullptr;

        // 現在のGBufferサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bRegisterLegacyBridge = true;
        bool m_bUsingRenderGraphResources = false;
        bool m_bRenderPassUsesRenderGraphInitialStates = false;
        RHI::ITexture* m_FramebufferAlbedoTexture = nullptr;
        RHI::ITexture* m_FramebufferNormalTexture = nullptr;
        RHI::ITexture* m_FramebufferMaterialTexture = nullptr;
        RHI::ITexture* m_FramebufferEmissiveTexture = nullptr;
        RHI::ITexture* m_FramebufferDepthTexture = nullptr;
        uint32_t m_FramebufferWidth = 0;
        uint32_t m_FramebufferHeight = 0;
        RenderPassSignature m_RenderPassSignature;

        // PerObject UBOアロケータ
        DynamicUniformAllocator m_UniformAllocator;

        // デフォルトテクスチャ・サンプラー（テクスチャ未設定オブジェクト用）
        RHI::TexturePtr m_DefaultWhiteTexture;      // 1x1 白 (255,255,255,255) — アルベド/AOデフォルト
        RHI::TexturePtr m_DefaultFlatNormalTexture; // 1x1 フラット法線 (128,128,255,255) — ノーマルマップデフォルト
        RHI::TexturePtr m_DefaultBlackTexture;      // 1x1 黒 (0,0,0,255) — メタリックデフォルト
        RHI::TexturePtr m_DefaultMidGrayTexture;    // 1x1 中間灰 (128,128,128,255) — ラフネスデフォルト
        RHI::SamplerPtr m_DefaultLinearSampler;
    };

} // namespace NorvesLib::Core::Rendering
