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
     * @brief SSRパス設定
     */
    struct SSRSettings
    {
        /** @brief 最大レイ距離 */
        float MaxDistance = 15.0f;

        /** @brief レイの厚み判定 */
        float Thickness = 0.3f;

        /** @brief 最大ステップ数 */
        float MaxSteps = 64.0f;

        /** @brief フェード開始距離 */
        float FadeStart = 8.0f;

        /** @brief フェード終了距離 */
        float FadeEnd = 15.0f;

        /** @brief ラフネスカットオフ */
        float RoughnessCutoff = 0.5f;

        /** @brief SSR強度 */
        float Intensity = 0.8f;

        /** @brief SSR有効/無効 */
        bool bEnabled = true;

        /** @brief 出力フォーマット */
        RHI::Format OutputFormat = RHI::Format::R16G16B16A16_FLOAT;
    };

    /**
     * @brief SSR（Screen-Space Reflections）パス
     *
     * LightingPassの後、Bloomの前に配置。
     * スクリーンスペースでレイマーチングを行い、反射を追加する。
     * 入力: "SceneColor", "GBuffer_Normal", "GBuffer_Material", "GBuffer_Depth"
     * 出力: "SceneColor"を上書き
     */
    class SSRPass : public IViewPass
    {
    public:
        explicit SSRPass(const SSRSettings &settings = SSRSettings{});
        ~SSRPass() override;

        const char *GetName() const override { return "SSRPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        void SetEnabled(bool bEnabled) { m_Settings.bEnabled = bEnabled; }
        const SSRSettings &GetSettings() const { return m_Settings; }

    private:
        SSRSettings m_Settings;

        // 出力テクスチャ
        RHI::TexturePtr m_OutputTexture;

        // パイプラインリソース
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::DescriptorSetPtr m_DescriptorSet;
        RHI::SamplerPtr m_LinearSampler;
        RHI::SamplerPtr m_PointSampler;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
