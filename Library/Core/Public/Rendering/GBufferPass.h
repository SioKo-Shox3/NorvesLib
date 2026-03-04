#pragma once

#include "IViewPass.h"
#include "SceneRenderer.h"
#include "DynamicUniformAllocator.h"
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
     * 出力はSharedResourceRegistryに登録され、後続のLightingPassが参照します。
     *
     * SharedResource登録名:
     * - "GBuffer_Albedo"
     * - "GBuffer_Normal"
     * - "GBuffer_Material"
     * - "GBuffer_Emissive"
     * - "GBuffer_Depth"
     */
    class GBufferPass : public IViewPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings GBufferパス設定
         */
        explicit GBufferPass(const GBufferPassSettings &settings = GBufferPassSettings{});

        /**
         * @brief デストラクタ
         */
        ~GBufferPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "GBufferPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

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

        // ========================================
        // GBufferアクセス
        // ========================================

        RHI::ITexture *GetAlbedoTexture() const { return m_AlbedoTexture.get(); }
        RHI::ITexture *GetNormalTexture() const { return m_NormalTexture.get(); }
        RHI::ITexture *GetMaterialTexture() const { return m_MaterialTexture.get(); }
        RHI::ITexture *GetEmissiveTexture() const { return m_EmissiveTexture.get(); }
        RHI::ITexture *GetDepthTexture() const { return m_DepthTexture.get(); }

    private:
        /**
         * @brief GBufferリソースを作成または更新します
         * @param width 幅
         * @param height 高さ
         * @param context 描画コンテキスト
         * @return 成功時true
         */
        bool CreateGBufferResources(uint32_t width, uint32_t height, ViewRenderContext &context);

        // 設定
        GBufferPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // GBufferテクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_AlbedoTexture;
        RHI::TexturePtr m_NormalTexture;
        RHI::TexturePtr m_MaterialTexture;
        RHI::TexturePtr m_EmissiveTexture;
        RHI::TexturePtr m_DepthTexture;

        // GBuffer用レンダーパス・フレームバッファ
        RHI::RenderPassPtr m_GBufferRenderPass;
        RHI::FramebufferPtr m_GBufferFramebuffer;

        // GBuffer用パイプライン
        RHI::PipelinePtr m_GBufferPipeline;
        RHI::ShaderPtr m_GBufferVertexShader;
        RHI::ShaderPtr m_GBufferFragmentShader;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // 現在のGBufferサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;

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
