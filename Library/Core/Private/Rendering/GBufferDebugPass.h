#pragma once

#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_STATS

#include "Rendering/FrameCaptureTypes.h"
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

    /**
     * @brief 撮影の AOV: GBuffer の生の値と照明の結果を浮動小数の 3 枚へ書き、撮影の読み出しに渡す（--capture-aov）
     *
     * 表面（RGBA32F）: rgb = GBuffer.Albedo（照明が読むのと同じ線形のアルベド）、a = GBuffer.Material の粗さ。
     * 幾何（RGBA32F）: xyz = GBuffer.Normal（ワールドの法線。+Y が上。正規化し直さない）、
     *   w = 深度から戻したワールドの位置のカメラの前方向への距離（視線方向の深度 m）。
     * 照明（RGBA32F）: r = 太陽の可視率（LightingPass が Lighting.SpecularReflectance の a に書いた影の結果。n·l は掛けない）。
     *   照明の出力がグラフに無い構成では -1。
     * 空の画素（照明と同じ判定: アルベドの a < 0.01）は表面 0・法線 0・深度 +∞・可視率 0。
     * GBufferDebugPass の可視化と違い後処理を通さず、出力は "Capture.AovSurface"・"Capture.AovGeometry"・"Capture.AovLighting" として
     * グラフの外へ出す（View が拾い、撮影の元にする）。統計が有効な構成（Debug・RelWithDebInfo）だけに存在する。
     */
    class GBufferAovPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        GBufferAovPass() = default;
        ~GBufferAovPass() override;

        const char* GetName() const override { return "GBufferAovPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 最後に書いたフレームのカメラ（書いていなければ bValid が false） */
        const FrameCaptureAovCamera& GetLastCamera() const { return m_LastCamera; }

    private:
        struct Use
        {
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParamsUniform;
        };

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_Pipeline;
        RHI::RenderPassPtr m_RenderPass;
        RHI::SamplerPtr m_Sampler;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::ITexture* m_FramebufferTargets[3] = {nullptr, nullptr, nullptr};
        FrameUseRing<Use> m_Uses;
        FrameCaptureAovCamera m_LastCamera;

        RGResourceHandle m_SurfaceHandle;
        RGResourceHandle m_GeometryHandle;
        RGResourceHandle m_LightingHandle;
        /** @brief 照明の Lighting.SpecularReflectance（a = 太陽の可視率）。グラフに無ければ無効 */
        RGTextureHandle m_ReflectanceHandle;
        /** @brief RTGI の時間の蓄積の後の出力（a = 遮蔽の診断の符号と空の分）。グラフに無ければ無効 */
        RGTextureHandle m_RTGITemporalHandle;
        RGTextureHandle m_AlbedoHandle;
        RGTextureHandle m_NormalHandle;
        RGTextureHandle m_MaterialHandle;
        RGTextureHandle m_DepthHandle;
    };

} // namespace NorvesLib::Core::Rendering

#endif // NORVES_ENABLE_STATS
