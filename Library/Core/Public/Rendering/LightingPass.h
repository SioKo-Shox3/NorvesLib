#pragma once

#include "IViewPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

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
    };

    /**
     * @brief ライティングパス（遅延ライティング）
     *
     * GBufferを入力としてフルスクリーン描画でPBRライティングを計算し、
     * HDRカラーバッファに出力するパス。
     *
     * 入力（SharedResourceRegistryから取得）:
     * - "GBuffer_Albedo"   : アルベド
     * - "GBuffer_Normal"   : ワールド法線
     * - "GBuffer_Material" : メタリック/ラフネス/AO
     * - "GBuffer_Depth"    : 深度
     *
     * 出力（SharedResourceRegistryに登録）:
     * - "SceneColor"       : HDRライティング結果 (R16G16B16A16_FLOAT)
     * - "SceneDepth"       : 深度のコピー（GBuffer_Depthのエイリアス）
     *
     * ライトデータはSceneViewのLightProxyから収集してUBOにパックします。
     */
    class LightingPass : public IViewPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings ライティングパス設定
         */
        explicit LightingPass(const LightingPassSettings &settings = LightingPassSettings{});

        /**
         * @brief デストラクタ
         */
        ~LightingPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "LightingPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        // ========================================
        // 出力アクセス
        // ========================================

        /**
         * @brief HDRシーンカラーテクスチャを取得
         */
        RHI::ITexture *GetSceneColorTexture() const { return m_SceneColorTexture.get(); }

    private:
        /**
         * @brief ライト情報をGPUバッファにパック
         * @param context 描画コンテキスト
         * @param bShadowAvailable シャドウマップが利用可能か
         */
        void UpdateLightBuffer(ViewRenderContext &context, bool bShadowAvailable);

        // 設定
        LightingPassSettings m_Settings;

        // 出力テクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_SceneColorTexture;

        // ライティング用リソース
        RHI::RenderPassPtr m_LightingRenderPass;
        RHI::FramebufferPtr m_LightingFramebuffer;
        RHI::PipelinePtr m_LightingPipeline;
        RHI::ShaderPtr m_LightingVertexShader;
        RHI::ShaderPtr m_LightingFragmentShader;
        RHI::BufferPtr m_LightDataBuffer;
        RHI::BufferPtr m_LightArrayBuffer;
        RHI::DescriptorSetPtr m_LightingDescriptorSet;
        RHI::SamplerPtr m_GBufferSampler;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
