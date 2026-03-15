#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/NeuralMaterialDecoder.h"
#include "Rendering/NeuralMaterialResource.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

namespace NorvesLib::Core::Rendering
{
    class SceneRenderer;
    class SceneView;

    /**
     * @brief ニューラルマテリアルデコードパス
     *
     * Cooperative Vector (VK_NV_cooperative_vector) を使用して、
     * ニューラルネットワークベースのマテリアルデコードをコンピュートシェーダーで実行します。
     *
     * 1つのMLPから複数のPBRプロパティテクスチャ（Albedo, Normal, ARM等）を
     * 一括デコードし、後続のGBufferPassから通常のテクスチャとしてサンプリングされます。
     *
     * パイプラインの位置: ShadowMap → **NeuralMaterialDecode** → GBuffer
     *
     * DrawCommand::CreateDispatch() を通じてDispatchコマンドを生成し、
     * SceneRenderer経由でGPUコマンドを記録します。
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

        const char* GetName() const override { return "NeuralMaterialDecodePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        // ========================================
        // リソース管理
        // ========================================

        /**
         * @brief SceneRendererを設定
         */
        void SetSceneRenderer(SceneRenderer* renderer) { m_SceneRenderer = renderer; }

        /**
         * @brief SceneViewを設定
         */
        void SetSceneView(SceneView* view) { m_SceneView = view; }

    private:
        RHI::IDevice* m_Device = nullptr;
        SceneRenderer* m_SceneRenderer = nullptr;
        SceneView* m_SceneView = nullptr;

        // ニューラルマテリアルデコーダー
        NeuralMaterialDecoder m_Decoder;

        // テスト用ダミーリソース（将来的にResourceRegistryから取得）
        Container::VariableArray<NeuralMaterialResource> m_OwnedResources;

        // Cooperative Vectorサポート状況
        bool m_bCooperativeVectorSupported = false;
    };

} // namespace NorvesLib::Core::Rendering
