#pragma once

#include "IViewPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class SceneRenderer;

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
     * 入力:
     * - SceneViewのDrawCommand（Opaque/Transparent）
     *
     * 出力（SharedResourceRegistryに登録）:
     * - "SceneColor"  : フォワード描画結果
     * - "SceneDepth"  : 深度バッファ
     */
    class ForwardPass : public IViewPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param sceneView  描画対象のSceneView
         * @param sceneRenderer 描画実行器
         */
        ForwardPass(SceneView *sceneView, SceneRenderer *sceneRenderer);

        /**
         * @brief デストラクタ
         */
        ~ForwardPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "ForwardPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        // ========================================
        // モード設定
        // ========================================

        /**
         * @brief 不透明オブジェクトのみ描画するか
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

    private:
        // 参照（所有しない）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // 出力テクスチャ（TransientResourcePoolから取得、非所有）
        RHI::ITexture *m_ColorTexture = nullptr;
        RHI::ITexture *m_DepthTexture = nullptr;

        // フォワード用リソース
        RHI::RenderPassPtr m_ForwardRenderPass;
        RHI::FramebufferPtr m_ForwardFramebuffer;

        // 設定
        bool m_bTransparentOnly = false;
        bool m_bRegisterOutputs = true;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
