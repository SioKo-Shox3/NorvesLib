#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/NeuralMaterialDecoder.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

namespace NorvesLib::Core::Rendering
{
    class SceneRenderer;
    class SceneView;
    class NeuralMaterialResource;

    /**
     * @brief ニューラルマテリアルデコードパス
     *
     * Cooperative Vector (VK_NV_cooperative_vector) を使用して、
     * ニューラルネットワークベースのマテリアルデコードをコンピュートシェーダーで実行します。
     *
     * デコード対象のNeuralMaterialResourceはRenderResourceManagerが管理し、
     * 本パスはSetup()時にResourceManagerからプルモデルで取得します。
     *
     * パイプラインの位置: ShadowMap → **NeuralMaterialDecode** → GBuffer
     *
     * Cooperative Vector非対応環境ではパスが自動的にスキップされます。
     */
    class NeuralMaterialDecodePass : public IViewPass
    {
    public:
        NeuralMaterialDecodePass() = default;
        ~NeuralMaterialDecodePass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "NeuralMaterialDecodePass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        // ========================================
        // リソース管理
        // ========================================

        /**
         * @brief SceneRendererを設定
         */
        void SetSceneRenderer(SceneRenderer *renderer) { m_SceneRenderer = renderer; }

        /**
         * @brief SceneViewを設定
         */
        void SetSceneView(SceneView *view) { m_SceneView = view; }

        /**
         * @brief Cooperative Vectorがサポートされているか
         */
        bool IsCooperativeVectorSupported() const { return m_bCooperativeVectorSupported; }

    private:
        RHI::IDevice *m_Device = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;
        SceneView *m_SceneView = nullptr;

        // ニューラルマテリアルデコーダー
        NeuralMaterialDecoder m_Decoder;

        // Setup()でResourceManagerから取得したデコード対象（フレーム単位の一時キャッシュ）
        Container::VariableArray<NeuralMaterialResource *> m_FrameDecodeTargets;

        // Cooperative Vectorサポート状況
        bool m_bCooperativeVectorSupported = false;
    };

} // namespace NorvesLib::Core::Rendering
