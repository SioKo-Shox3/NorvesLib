#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ITexture;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 草・低木の株（FoliageTypes.h の束）を、インスタンス描画で GBuffer へ直接ラスタするパス
     *
     * ビジビリティバッファの解決（または GBuffer の描画）の後・SSAO と影の印付けの前に置き、GBuffer の 5 枚と深度へ重ねて描く。
     * 頂点シェーダーで風に揺らすので、頂点を読み直す解決の経路には載せない。束ごとに 1 回の DrawIndexedInstanced で描き、
     * 帯は両面（カリングなし）。影は照明が GBuffer の深度から引くので受ける（影は落とさない）。
     *
     * メッシュ・インスタンスの配列・定数・記述子セットは、毎フレーム文脈の FrameResources（フレームの持ち主）から借りて上げる
     * （寿命を気にしない。パスを書く人向けの約束は Docs/Architecture/FrameResources.md）。
     */
    class FoliagePass : public IViewPass, public IRenderGraphPass
    {
    public:
        FoliagePass() = default;
        ~FoliagePass() override;

        const char *GetName() const override { return "FoliagePass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

    private:
        /// GBuffer の 5 枚（gbuffer.frag の出力の順）と深度
        static constexpr uint32_t kColorTargetCount = 5;

        /// 描き先が変わったら render pass・framebuffer・パイプラインを作り直す
        bool EnsureTargets(const RHI::TexturePtr (&colorTargets)[kColorTargetCount], const RHI::TexturePtr &depthTarget);
        /// 描き先の形式から render pass とパイプラインを作る
        bool CreatePipeline(const RHI::TexturePtr (&colorTargets)[kColorTargetCount], const RHI::TexturePtr &depthTarget);

        RHI::IDevice *m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        // 共有の記述子セットのレイアウト（セットは毎フレーム借りる）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RGResourceHandle m_ColorHandles[kColorTargetCount];
        RGResourceHandle m_DepthHandle;
        // 今の framebuffer の描き先（作り直しの判定用。所有しない）
        const RHI::ITexture *m_FramebufferTargets[kColorTargetCount + 1] = {};
        // 今の render pass・パイプラインの描き先の形式
        RHI::Format m_TargetFormats[kColorTargetCount + 1] = {};
        // 最後にログへ出した束の数（変わったときだけ 1 行出す）
        uint32_t m_LoggedBatchCount = 0;
        uint64_t m_LoggedInstanceCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
