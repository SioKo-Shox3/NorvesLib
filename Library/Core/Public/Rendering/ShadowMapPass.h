#pragma once

#include "IViewPass.h"
#include "SceneRenderer.h"
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

    /**
     * @brief シャドウマップパス設定
     */
    struct ShadowMapPassSettings
    {
        /** @brief シャドウマップ解像度（正方形） */
        uint32_t Resolution = 2048;

        /** @brief 深度フォーマット */
        RHI::Format DepthFormat = RHI::Format::D32_FLOAT;

        /** @brief 正射影の半サイズ */
        float OrthoSize = 20.0f;

        /** @brief ライトカメラのニアプレーン */
        float NearPlane = 0.1f;

        /** @brief ライトカメラのファープレーン */
        float FarPlane = 50.0f;
    };

    /**
     * @brief シャドウマップパス（深度描画 - ライト視点）
     *
     * ディレクショナルライトの視点からシーンの深度のみを描画し、
     * シャドウマップテクスチャとして出力します。
     *
     * GBufferPassの前に実行され、標準経路では RenderGraph named resource として公開した
     * シャドウマップをLightingPassが参照して影を計算します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * legacy bridge出力:
     * - "ShadowMap" : 深度テクスチャ (D32_FLOAT)
     */
    class ShadowMapPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings シャドウマップパス設定
         */
        explicit ShadowMapPass(const ShadowMapPassSettings &settings = ShadowMapPassSettings{});

        /**
         * @brief デストラクタ
         */
        ~ShadowMapPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "ShadowMapPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;
        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

        // ========================================
        // SceneView連携
        // ========================================

        /**
         * @brief 描画対象のSceneViewを設定します
         * @param sceneView SceneView
         */
        void SetSceneView(SceneView *sceneView) { m_SceneView = sceneView; }

        /**
         * @brief SceneRendererを設定します
         * @param renderer SceneRenderer
         */
        void SetSceneRenderer(SceneRenderer *renderer) { m_SceneRenderer = renderer; }

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
        // シャドウマップアクセス
        // ========================================

        RHI::ITexture* GetShadowMapTexture() const { return m_ShadowMapTexture.get(); }
        RGResourceHandle GetShadowMapHandle() const { return m_ShadowMapHandle; }

    private:
        // 設定
        ShadowMapPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // シャドウマップ深度テクスチャ
        RHI::TexturePtr m_ShadowMapTexture;
        RGResourceHandle m_ShadowMapHandle;

        // レンダーパス・フレームバッファ
        RHI::RenderPassPtr m_ShadowRenderPass;
        RHI::FramebufferPtr m_ShadowFramebuffer;

        // パイプライン・シェーダー
        RHI::PipelinePtr m_ShadowPipeline;
        RHI::ShaderPtr m_ShadowVertexShader;
        RHI::ShaderPtr m_ShadowFragmentShader;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        bool m_bRegisterLegacyBridge = true;

        // PerObject UBOアロケータ
        DynamicUniformAllocator m_UniformAllocator;
    };

} // namespace NorvesLib::Core::Rendering
