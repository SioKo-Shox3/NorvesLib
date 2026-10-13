#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Container/Containers.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    class GBufferPass;

    /**
     * @brief 画面空間AO（GTAO）の設定
     */
    struct SSAOSettings
    {
        /** @brief 遮蔽を探す半径（m）。これより遠い面は遮蔽にならない */
        float Radius = 1.0f;

        /** @brief 半径のうち、外側で遮蔽の重みを0へ落としていく幅の割合（0〜1） */
        float FalloffFraction = 0.615f;

        /** @brief 可視率に掛ける指数（1で物理的な余弦重みの可視率のまま） */
        float Intensity = 1.0f;

        /** @brief 画面上の半径の上限（画素）。近くの面で探索が画面全体へ広がらないようにする */
        float MaxRadiusPixels = 128.0f;

        /** @brief 雑音除去の後の出力フォーマット（単チャンネル） */
        RHI::Format OutputFormat = RHI::Format::R8_UNORM;
    };

    /**
     * @brief 画面空間AOパス（GTAO: Ground Truth Ambient Occlusion）
     *
     * GBufferの深度と法線から、画素ごとに画面上の2方向のスライスで地平線の角度を求め、余弦重みの
     * 可視率を解析的に積分します（Jimenez et al. 2016）。半径は世界の長さ（m）で、半径の外側の
     * 面は遮蔽にならないので、部屋の大きさの壁全体が暗くならず、隅・接地部・軒下だけが暗くなります。
     * 雑音は4×4の画素で一巡し、深度を考慮した4×4の平均で除きます。TAAのジッタが掛かったフレームでは
     * 雑音をフレームごとにずらし、TAAの履歴で時間方向にも蓄積します。
     * 多重反射の近似（明るい面の遮蔽を弱める）は、アルベドを持つLightingPassが可視率へ掛けます。
     *
     * パイプラインの位置: GBuffer → **SSAO(GTAO)** → Lighting
     *
     * 入力（RenderGraph named resource）:
     * - "GBuffer.Depth" : 深度テクスチャ
     * - "GBuffer.Normal" : ワールド法線テクスチャ
     *
     * 出力:
     * - "SSAO.Raw" : 雑音除去の前の可視率 (R16_FLOAT)
     * - "SSAO.Blurred" : 雑音除去の後の可視率 (OutputFormat。既定 R8_UNORM)
     */
    class SSAOPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings SSAO設定
         */
        explicit SSAOPass(const SSAOSettings &settings = SSAOSettings{});

        /**
         * @brief デストラクタ
         */
        ~SSAOPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "SSAOPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        // ========================================
        // IRenderGraphPass実装
        // ========================================

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;
        // 監査済み: Execute は全画面のパスを積むだけで自分でバリアを張らず、出力は render pass の finalState
        // （宣言の最終の状態 ShaderResource）へ移る。準備が整ったフレームだけ、計画した区間で Parallel に記録する。
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        // 区間ごとの記録の前に、render pass・framebuffer・パイプラインの作り直しを直列で済ませる。
        void PrepareParallelRecord(RenderGraphResources &resources, ViewRenderContext &context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        void SetRadius(float radius) { m_Settings.Radius = radius; }
        void SetIntensity(float intensity) { m_Settings.Intensity = intensity; }
        const SSAOSettings &GetSettings() const { return m_Settings; }

        /**
         * @brief Legacy bridge fallback 用のGBuffer参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetGBufferPass(const GBufferPass *gbufferPass) { m_GBufferPass = gbufferPass; }
        RGResourceHandle GetSSAORawHandle() const { return m_SSAORawHandle.ToResourceHandle(); }
        RGResourceHandle GetSSAOBlurredHandle() const { return m_SSAOBlurredHandle.ToResourceHandle(); }

        /** @brief 雑音除去の前の可視率のフォーマット（0〜1の外の値も平均へ残す） */
        static constexpr RHI::Format RawFormat = RHI::Format::R16_FLOAT;

    private:
        bool CreateSSAOResources(uint32_t width, uint32_t height, ViewRenderContext &context);
        uint32_t ResolveSSAOWidth(const ViewRenderContext &context) const;
        uint32_t ResolveSSAOHeight(const ViewRenderContext &context) const;
        bool PrepareSSAOAttachments(uint32_t width,
                                    uint32_t height,
                                    const RHI::TexturePtr &rawTexture,
                                    const RHI::TexturePtr &blurredTexture,
                                    bool bUseRenderGraphInitialStates);
        bool EnsureSSAORenderPass(bool bUseRenderGraphInitialStates);
        bool EnsureSSAOFramebuffer(uint32_t width,
                                   uint32_t height,
                                   const RHI::TexturePtr &rawTexture);
        bool EnsureSSAOPipeline();
        bool EnsureBlurRenderPass(bool bUseRenderGraphInitialStates);
        bool EnsureBlurFramebuffer(uint32_t width,
                                   uint32_t height,
                                   const RHI::TexturePtr &blurredTexture);
        bool EnsureBlurPipeline();
        void ExecuteWithGBufferTextures(ViewRenderContext &context,
                                        const RHI::TexturePtr &depthTexture,
                                        const RHI::TexturePtr &normalTexture,
                                        bool bRegisterLegacyBridge);
        bool TryEnqueueNativeTransitionPasses(ViewRenderContext &context) const;
        /** @brief 記述子セットを借りる（持ち主の無い文脈では使い回さないセットを作り、パスを手放すまで持つ） */
        RHI::DescriptorSetPtr BorrowDescriptorSet(ViewRenderContext &context,
                                                  const RHI::DescriptorSetLayoutPtr &layout,
                                                  const RHI::DescriptorSetDesc &desc);
        /** @brief 定数を借りて data を書き、set の binding へ結ぶ（持ち主の無い文脈は BorrowDescriptorSet と同じ扱い） */
        bool BindFrameConstants(ViewRenderContext &context,
                                RHI::IDescriptorSet &set,
                                uint32_t binding,
                                const void *data,
                                uint32_t size,
                                const char *unownedDebugName);

        // 設定
        SSAOSettings m_Settings;
        const GBufferPass *m_GBufferPass = nullptr;

        // SSAOパス用リソース
        RHI::TexturePtr m_SSAORawTexture;     // 雑音除去の前の可視率
        RHI::TexturePtr m_SSAOBlurredTexture; // 雑音除去の後の可視率

        RGTextureHandle m_SSAORawHandle;
        RGTextureHandle m_SSAOBlurredHandle;
        RGResourceHandle m_GBufferDepthHandle;
        RGResourceHandle m_GBufferNormalHandle;

        // SSAOパス
        RHI::RenderPassPtr m_SSAORenderPass;
        RHI::FramebufferPtr m_SSAOFramebuffer;
        RHI::PipelinePtr m_SSAOPipeline;
        RHI::ShaderPtr m_SSAOVertexShader;
        RHI::ShaderPtr m_SSAOFragmentShader;
        // 共有の記述子セットのレイアウト（定数と記述子セットは Execute ごとにフレームの持ち主から借りる）
        RHI::DescriptorSetLayoutPtr m_SSAODescriptorSetLayout;

        // ブラーパス
        RHI::RenderPassPtr m_BlurRenderPass;
        RHI::FramebufferPtr m_BlurFramebuffer;
        RHI::PipelinePtr m_BlurPipeline;
        RHI::ShaderPtr m_BlurFragmentShader;
        RHI::DescriptorSetLayoutPtr m_BlurDescriptorSetLayout;

        // 持ち主の無い文脈（RenderingCoordinator を通さない検査）で作った使い回さない定数と記述子セット。
        // 提出の完了を知る手段が無いので、パスを手放すまで持つ
        Container::VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;

        // サンプラー（シェーダーは texelFetch で読むが、結合サンプラーの記述子に要る）
        RHI::SamplerPtr m_LinearClampSampler;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bUsingRenderGraphResources = false;
        bool m_bSSAOInitialStateFromRenderGraph = false;
        bool m_bBlurInitialStateFromRenderGraph = false;
        RHI::ITexture *m_SSAOFramebufferTexture = nullptr;
        RHI::ITexture *m_BlurFramebufferTexture = nullptr;
        uint32_t m_SSAOFramebufferWidth = 0;
        uint32_t m_SSAOFramebufferHeight = 0;
        uint32_t m_BlurFramebufferWidth = 0;
        uint32_t m_BlurFramebufferHeight = 0;
    };

} // namespace NorvesLib::Core::Rendering
