#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class SceneRenderer;
    class RenderResourceManager;

    /**
     * @brief MegaGeometryパス設定
     */
    struct MegaGeometryPassSettings
    {
        /** @brief IndirectDrawバッファの最大ドローコール数 */
        uint32_t MaxDrawCount = 65536;

        /** @brief LOD選択バイアス */
        float LODBias = 1.0f;
    };

    /**
     * @brief Mega Geometry 描画パス
     *
     * GPU駆動カリングとIndirect Drawによるクラスタベースジオメトリ描画。
     *
     * パイプラインの位置: ShadowMap → NeuralMaterialDecode → **MegaGeometry** → GBuffer
     *
     * 動作フロー:
     * 1. Setup(): 登録済みMegaMeshリソースを収集
     * 2. Execute():
     *    a. DrawCountバッファをゼロクリア
     *    b. クラスタカリングコンピュートシェーダーをディスパッチ
     *    c. バリア: Compute → IndirectDraw
     *    d. GBufferレンダーパス内でDrawIndexedIndirectを発行
     *
     * MegaMeshが未登録の場合はパスが自動的にスキップされます。
     */
    class MegaGeometryPass : public IViewPass
    {
    public:
        explicit MegaGeometryPass(const MegaGeometryPassSettings &settings = MegaGeometryPassSettings{});
        ~MegaGeometryPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "MegaGeometryPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        // ========================================
        // SceneView連携
        // ========================================

        void SetSceneView(SceneView *sceneView) { m_SceneView = sceneView; }
        void SetSceneRenderer(SceneRenderer *renderer) { m_SceneRenderer = renderer; }

        // ========================================
        // MegaMesh登録
        // ========================================

        /**
         * @brief 描画対象のMegaMeshを追加
         * @param handle MegaMeshHandle
         * @param worldMatrix ワールド変換行列（列優先）
         */
        void AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float *worldMatrix);

        /**
         * @brief 描画対象のMegaMeshをクリア
         */
        void ClearMegaMeshInstances();

    private:
        /**
         * @brief カリング用ユニフォームデータ（GPU送信用）
         */
        struct alignas(16) CullUniformData
        {
            float ViewMatrix[16];
            float ProjectionMatrix[16];
            float CameraPosition[4];   // xyz + pad
            float FrustumPlanes[6][4]; // 6 planes, each (nx, ny, nz, d)
            uint32_t TotalClusterCount;
            uint32_t MaxDrawCount;
            float LODBias;
            float ScreenHeight;     // スクリーン高さ（ピクセル）
            float ProjectionFactor; // screenHeight / (2 * tan(fov/2))
            uint32_t HiZWidth;      // Hi-Zテクスチャ幅（mip 0）
            uint32_t HiZHeight;     // Hi-Zテクスチャ高さ（mip 0）
            uint32_t HiZMipCount;   // ミップレベル数
            uint32_t bHiZEnabled;   // Hi-Z有効フラグ（1=有効, 0=無効）
        };

        /**
         * @brief MegaMeshインスタンス情報
         */
        struct MegaMeshInstance
        {
            MegaGeometry::MegaMeshHandle Handle;
            float WorldMatrix[16];
        };

        /**
         * @brief カリング用GPUリソースを作成
         */
        bool CreateCullResources(RHI::IDevice *device);

        /**
         * @brief GBuffer互換のグラフィックスパイプラインを作成
         */
        bool CreateDrawPipeline(ViewRenderContext &context);

        /**
         * @brief 視錐台平面を行列から抽出
         */
        static void ExtractFrustumPlanes(const float *viewProj, float planes[6][4]);

        /**
         * @brief Hi-Z深度ピラミッドのリソースを作成・再作成
         */
        bool CreateHiZResources(ViewRenderContext &context);

        /**
         * @brief Hi-Z深度ピラミッドを生成（GBuffer深度からダウンサンプル）
         */
        void GenerateHiZPyramid(RHI::ICommandList *cmdList);

        // 設定
        MegaGeometryPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // カリングコンピュートパイプライン
        RHI::PipelinePtr m_CullPipeline;
        RHI::ShaderPtr m_CullShader;
        RHI::DescriptorSetPtr m_CullDescriptorSet;

        // カリング用GPUバッファ
        RHI::BufferPtr m_CullUniformBuffer;  // CullUniformData UBO
        RHI::BufferPtr m_IndirectDrawBuffer; // DrawIndexedIndirectCommand[]
        RHI::BufferPtr m_DrawCountBuffer;    // uint32_t visibleClusterCount

        // GBuffer描画用グラフィックスパイプライン
        RHI::PipelinePtr m_DrawPipeline;
        RHI::ShaderPtr m_DrawVertexShader;
        RHI::ShaderPtr m_DrawFragmentShader;

        // GBuffer描画用レンダーパス・フレームバッファ（GBufferPassから共有）
        RHI::RenderPassPtr m_GBufferRenderPass;
        RHI::FramebufferPtr m_GBufferFramebuffer;

        // GBufferテクスチャ参照（GBufferPassが作成したものをSharedResourcesから取得）
        RHI::TexturePtr m_AlbedoTexture;
        RHI::TexturePtr m_NormalTexture;
        RHI::TexturePtr m_MaterialTexture;
        RHI::TexturePtr m_EmissiveTexture;
        RHI::TexturePtr m_DepthTexture;

        // PerObject UBO用ディスクリプタセット
        RHI::DescriptorSetPtr m_DrawDescriptorSet;
        RHI::BufferPtr m_DrawUniformBuffer;

        // デフォルトPBRテクスチャ（マテリアル未設定時のフォールバック）
        RHI::TexturePtr m_DefaultWhiteTexture;      // 1x1 白 — Albedo/AO/Roughnessデフォルト
        RHI::TexturePtr m_DefaultFlatNormalTexture; // 1x1 フラット法線 (128,128,255) — Normalデフォルト
        RHI::TexturePtr m_DefaultBlackTexture;      // 1x1 黒 — Metallic/Heightデフォルト
        RHI::SamplerPtr m_DefaultLinearSampler;     // Linear/Wrapサンプラー

        // Hi-Z 深度ピラミッド
        RHI::TexturePtr m_HiZTexture;               // R32_FLOAT ミップチェーン
        RHI::ShaderPtr m_HiZShader;                 // hiz_generate.comp
        RHI::PipelinePtr m_HiZPipeline;             // Hi-Zダウンサンプルパイプライン
        RHI::DescriptorSetPtr m_HiZDescriptorSet;   // Hi-Z生成用ディスクリプタ
        RHI::BufferPtr m_HiZParamsBuffer;            // HiZParams UBO
        RHI::SamplerPtr m_HiZNearestSampler;        // Nearest/Clampサンプラー
        uint32_t m_HiZMipCount = 0;                 // Hi-Zミップレベル数

        // フレーム単位のインスタンスリスト
        Container::VariableArray<MegaMeshInstance> m_Instances;

        // 現在のGBufferサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
