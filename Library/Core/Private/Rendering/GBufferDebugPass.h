#pragma once

#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_STATS

#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "RHI/IDevice.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    /** @brief GBufferDebugPass が表示する GBuffer の内容 */
    enum class GBufferDebugView : uint32_t
    {
        /** @brief 世界の法線（xyz × 0.5 + 0.5 を、後段の処理を通る前の色として書く） */
        Normal = 0,
        /** @brief 速度（現在の UV − 前の UV に倍率を掛け、0 が灰色 0.5。R = x、G = y） */
        Velocity = 1,
        /** @brief 深度（R = 深度、G = fract(深度 × 256)、B = fract(深度 × 65536)。細かい差が縞で見える） */
        Depth = 2,
        /** @brief アルベド（GBuffer.Albedo の rgb をそのまま。発光や照明を通る前の材質の色） */
        Albedo = 3,
        /** @brief 材質（GBuffer.Material の rgb = 金属度・粗さ・AO をそのまま） */
        Material = 4
    };

    /**
     * @brief GBuffer.Normal・Velocity・Depth の検証表示（最後のシーンの色へ書き込む）
     *
     * ビジビリティバッファの on（幾何の解決が書く）と off（GBufferPass が書く）で、同じ画面の GBuffer の値を撮り比べるための表示。
     * 環境変数 NORVES_GBUFFER_DEBUG=normal|velocity|depth のときだけ SceneView が追加する。
     * 統計が有効な構成（NORVES_ENABLE_STATS。Debug・RelWithDebInfo）だけに存在し、Release の Core には入らない。
     *
     * 書いた色は最後のシーンの色なので、後段の後処理（TAA・露出・ブルーム・トーンマップ等）を通って保存される。
     * 保存された PNG の値は GBuffer の値そのものではなく、on・off が同じ後処理を通った色どうしの比較に使う。
     */
    class GBufferDebugPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit GBufferDebugPass(GBufferDebugView view);
        ~GBufferDebugPass() override;

        const char* GetName() const override { return "GBufferDebugPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 環境変数 NORVES_GBUFFER_DEBUG を読む。normal・velocity・depth のどれかなら true */
        static bool TryGetViewFromEnvironment(GBufferDebugView& outView);

    private:
        // 1 回の Execute が使う資源（フレームの枠の中で、Execute のたびに次の 1 組を使う）
        struct Use
        {
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParamsUniform;
        };

        GBufferDebugView m_View = GBufferDebugView::Normal;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_Pipeline;
        RHI::RenderPassPtr m_RenderPass;
        RHI::SamplerPtr m_Sampler;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::ITexture* m_FramebufferColor = nullptr;
        RHI::Format m_ColorFormat = RHI::Format::UNKNOWN;
        FrameUseRing<Use> m_Uses;

        RGResourceHandle m_ColorHandle;
        RGTextureHandle m_NormalHandle;
        RGTextureHandle m_VelocityHandle;
        RGTextureHandle m_DepthHandle;
    };

} // namespace NorvesLib::Core::Rendering

#endif // NORVES_ENABLE_STATS
