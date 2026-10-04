// TAA のパス。ジッタを掛けて描いた SceneColor へ、velocity で再投影した前のフレームの履歴を
// 近傍の色の分散（YCoCg）でクリップして混ぜる。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/TemporalAA.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief TAA のパス（ポストプロセスの SSR の後・自動露出とブルームの前）
     *
     * SceneView::Render がフレームごとに BeginFrame でジッタを決め、ジッタを掛けたカメラで
     * ライティング・半透明までを描く。前のカメラにも同じジッタを掛けるので、velocity と空の再投影には
     * ジッタが入らない。このパスは:
     * 1. SceneColor（SSR の後の色、無ければ Scene.Color）の3×3の近傍から YCoCg の平均と分散を求める。
     * 2. 近傍で最も手前の画素の velocity（空は前のカメラからのカメラの動き）で履歴を再投影し、
     *    平均 ± 標準偏差の箱へクリップし、露出が変わったら履歴を今の露出へ合わせる。velocity の基準は
     *    履歴を書いたフレームに揃える（描画がゲームのフレームを飛ばしたときは、物体の前の変換を
     *    RenderingCoordinator が、前のカメラを SceneView が履歴のフレームのものへ付け替える）。揃わない
     *    フレームでは履歴を使わない。
     * 3. 現在の色と輝度で重み付けして混ぜ、結果を次のフレームの履歴に残して SceneColor へ書き戻す。
     * 履歴が無い（最初のフレーム・カメラの切り替え・寸法の変更・その Viewport を TAA 無しで描いた後）ときや、
     * 再投影が画面の外に出た画素は現在の色だけを使う。無効（既定）のときは何もしない。
     */
    class TemporalAAPass : public IViewPass, public IRenderGraphPass
    {
    public:
        TemporalAAPass();
        ~TemporalAAPass() override;

        const char* GetName() const override { return "TemporalAAPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /**
         * @brief この View のフレームのジッタを決める
         *
         * SceneView::Render が描画の前に呼ぶ。同じフレームの2つ目以降の Viewport では履歴が混ざるため
         * false を返し、その Viewport では TAA を掛けない。
         */
        bool BeginFrame(uint64_t frameNumber,
                        uint32_t viewportId,
                        uint32_t width,
                        uint32_t height,
                        TemporalAAJitter& outJitter);

        /**
         * @brief Viewport を描く前に、その Viewport へ TAA を掛けるかを知らせる
         *
         * 掛けないなら無効にし、履歴を書いた Viewport なら履歴を捨てる（別の Viewport の履歴には触れない）。
         */
        void NotifyViewportRendered(uint32_t viewportId, bool bApplied)
        {
            SetEnabled(bApplied);
            if (!bApplied)
            {
                m_History.NotifyViewportWithoutTemporalAA(viewportId);
            }
        }

        /** @brief 描画がフレームを飛ばしたときに前のカメラにする、履歴を書いたフレームのカメラ（無ければ null）。 */
        const CameraProxy* FindReprojectionCamera(uint32_t viewportId, uint64_t cameraId, uint64_t sourceCameraId,
                                                  uint64_t frameNumber) const
        {
            return m_History.FindReprojectionCamera(viewportId, cameraId, sourceCameraId, frameNumber);
        }

        /** @brief 履歴を書いたフレームの番号（FindReprojectionCamera が返すカメラのフレーム）。 */
        uint64_t GetHistoryFrameNumber() const { return m_History.GetFrameNumber(); }

        /** @brief 履歴を捨てる（次に働くフレームは現在の色だけを使う）。 */
        void InvalidateHistory() { m_History.Invalidate(); }

        /** @brief 次のフレームに使える履歴があるか。 */
        bool HasValidHistory() const { return m_History.IsValid(); }

        /**
         * @brief 決定的な撮影のエポックで、ジッタの列を先頭へ戻して履歴を捨てる。
         *
         * 次の BeginFrame が 1 番目のジッタを返し、そのフレームは現在の色だけを使う。
         */
        void ResetForDeterministicEpoch()
        {
            m_JitterIndex = 0u;
            m_History.Invalidate();
        }

    private:
        bool PrepareResources(const RHI::TexturePtr& sceneColor);
        void ReleaseSizedResources();
        bool IsActiveFor(const ViewRenderContext& context) const;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_ResolveShader;
        RHI::ShaderPtr m_CopyShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::SamplerPtr m_PointSampler;
        RHI::SamplerPtr m_LinearSampler;
        RHI::DescriptorSetPtr m_ResolveDescriptorSet;
        RHI::DescriptorSetPtr m_CopyDescriptorSet;

        // 履歴の2枚を交互に書く（書いた方が次のフレームの読む方になる）。
        RHI::TexturePtr m_HistoryTextures[2];
        RHI::FramebufferPtr m_HistoryFramebuffers[2];
        RHI::RenderPassPtr m_HistoryRenderPass;
        RHI::PipelinePtr m_ResolvePipeline;
        RHI::RenderPassPtr m_CopyRenderPass;
        RHI::FramebufferPtr m_CopyFramebuffer;
        RHI::PipelinePtr m_CopyPipeline;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_VelocityHandle;

        uint32_t m_CurrentWidth = 0u;
        uint32_t m_CurrentHeight = 0u;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;

        uint32_t m_HistoryWriteIndex = 0u;
        // 履歴を書いたフレームの番号・Viewport・カメラ・露出。
        TemporalAAHistoryTracker m_History;
        // 履歴を使ったフレーム数、そのうち描画がゲームのフレームを飛ばした（velocity の基準を履歴のフレームへ
        // 付け替えた）フレーム数、履歴があったのに使わなかったフレーム数と、そのうち velocity の物体の基準が
        // 揃わなかったフレーム数（終了時にログへ出す）。
        uint64_t m_HistoryReusedFrameCount = 0u;
        uint64_t m_HistoryRebasedFrameCount = 0u;
        uint64_t m_HistoryRejectedFrameCount = 0u;
        uint64_t m_HistoryObjectStateMismatchCount = 0u;

        // BeginFrame が決めたこのフレームのジッタと、それを受け取った Viewport。
        uint64_t m_JitterIndex = 0u;
        bool m_bFrameBegun = false;
        uint64_t m_FrameNumber = 0u;
        uint32_t m_FrameViewportId = 0u;
        TemporalAAJitter m_FrameJitter;
    };
} // namespace NorvesLib::Core::Rendering
