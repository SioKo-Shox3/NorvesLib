#pragma once

#include "IViewPass.h"
#include "DynamicUniformAllocator.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class SceneRenderer;
    class GBufferPass;
    class LightingPass;

    /**
     * @brief フォワードレンダリングパス
     *
     * 既存のSceneRenderer::ExecuteDrawCommandsをIViewPassとして
     * ラップするアダプタークラス。
     *
     * ディファードレンダリングへの段階的移行期間中、既存のフォワード描画
     * パイプラインをパスチェーンの中で使用できるようにします。
     *
     * 使用パターン:
     * - ディファードと併用: 半透明オブジェクトのみForwardPassで描画
     * - 単体使用: ディファード非対応シーンで従来通りの描画
     *
     * 半透明専用モードではLightingPassの後、SSRより前に実行され、
     * "SceneColor" へLoad合成した結果がSSR入力にも反映されます。
     *
     * 入力:
     * - SceneViewのDrawCommand（Opaque/Transparent）
     *
     * 出力（SharedResourceRegistryに登録）:
     * - "SceneColor"  : フォワード描画結果
     * - "SceneDepth"  : 深度バッファ
     */
    class ForwardPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param sceneView  描画対象のSceneView
         * @param sceneRenderer 描画実行器
         */
        ForwardPass(SceneView* sceneView, SceneRenderer* sceneRenderer);

        /**
         * @brief デストラクタ
         */
        ~ForwardPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char* GetName() const override { return "ForwardPass"; }

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
        // モード設定
        // ========================================

        /**
         * @brief 半透明オブジェクトのみ描画するか
         *
         * ディファードレンダリングと組み合わせる場合、
         * 半透明オブジェクトのみをForwardPassで処理するケースがあります。
         */
        void SetTransparentOnly(bool bTransparentOnly) { m_bTransparentOnly = bTransparentOnly; }
        bool IsTransparentOnly() const { return m_bTransparentOnly; }

        /**
         * @brief SharedResourceRegistryへの登録を行うか
         *
         * ディファードパイプライン後に半透明を描画する場合、
         * SceneColorはLightingPassが既に登録しているので再登録は不要。
         */
        void SetRegisterOutputs(bool bRegister) { m_bRegisterOutputs = bRegister; }

        /**
         * @brief Legacy bridge fallback 用のLighting参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetLightingPass(const LightingPass* lightingPass) { m_LightingPass = lightingPass; }

        /**
         * @brief Legacy bridge fallback 用のGBuffer参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetGBufferPass(const GBufferPass* gbufferPass) { m_GBufferPass = gbufferPass; }

    private:
        bool CreateTransparentResources(uint32_t width,
                                        uint32_t height,
                                        bool bUseRenderGraphInitialStates = false);
        bool PrepareTransparentResources(uint32_t width,
                                         uint32_t height,
                                         const RHI::TexturePtr& sceneColorTexture,
                                         const RHI::TexturePtr& gbufferDepthTexture,
                                         bool bUseRenderGraphInitialStates);
        void ExecuteTransparentCommands(ViewRenderContext& context, bool bUseRenderGraphManagedStates);
        void EnqueueEmptyTransparentPass(ViewRenderContext& context) const;
        void RegisterTransparentBridge(ViewRenderContext& context,
                                       const RHI::TexturePtr& sceneColorTexture,
                                       const RHI::TexturePtr& gbufferDepthTexture) const;

        // 参照（所有しない）
        SceneView* m_SceneView = nullptr;
        SceneRenderer* m_SceneRenderer = nullptr;
        const LightingPass* m_LightingPass = nullptr;
        const GBufferPass* m_GBufferPass = nullptr;
        RHI::IDevice* m_Device = nullptr;

        // 出力テクスチャ（フォワード単独パイプライン用、TransientResourcePoolから取得、非所有）
        RHI::ITexture* m_ColorTexture = nullptr;
        RHI::ITexture* m_DepthTexture = nullptr;

        // 半透明ディファード合成用出力（SharedResourceRegistryから取得、所有参照を保持）
        RHI::TexturePtr m_SceneColorTexture;
        RHI::TexturePtr m_GBufferDepthTexture;
        RGResourceHandle m_RenderGraphSceneColorHandle;
        RGResourceHandle m_RenderGraphDepthHandle;

        // フォワード用リソース
        RHI::RenderPassPtr m_ForwardRenderPass;
        RHI::FramebufferPtr m_ForwardFramebuffer;
        RHI::PipelinePtr m_TransparentPipeline;
        RHI::ShaderPtr m_TransparentVertexShader;
        RHI::ShaderPtr m_TransparentFragmentShader;

        // 透明フォワード用PerObject UBOアロケータ
        DynamicUniformAllocator m_UniformAllocator;

        // マテリアル未設定時のフォールバック
        RHI::TexturePtr m_DefaultWhiteTexture;
        RHI::TexturePtr m_DefaultFlatNormalTexture;
        RHI::TexturePtr m_DefaultBlackTexture;
        RHI::TexturePtr m_DefaultMidGrayTexture;
        RHI::SamplerPtr m_DefaultLinearSampler;

        // 設定
        bool m_bTransparentOnly = false;
        bool m_bRegisterOutputs = true;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bTransparentRenderPassUsesRenderGraphInitialStates = false;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
        RHI::ITexture* m_FramebufferGBufferDepthTexture = nullptr;

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
            AttachmentSignature Depth;
            bool bValid = false;
        };

        bool AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                       const AttachmentSignature& rhs) const;
        bool RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                       const RenderPassSignature& rhs) const;
        RenderPassSignature CreateTransparentRenderPassSignature(uint32_t width,
                                                                 uint32_t height,
                                                                 bool bUseRenderGraphInitialStates) const;
        RenderPassSignature m_TransparentRenderPassSignature;
    };

} // namespace NorvesLib::Core::Rendering
