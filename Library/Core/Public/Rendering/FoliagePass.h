#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Rendering/FoliageTypes.h"
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
     * 帯は両面（カリングなし）。影は照明が GBuffer の深度から引くので受ける。太陽の影を落とすのは、カメラに近い株を同じ置き方で
     * 仮想シャドウマップへ描く VirtualShadowMapPass（VirtualShadowMapFoliageCasters.h）。
     *
     * メッシュとインスタンスの配列は作った後に書き換えない値なので、初めて描くときに一度だけ GPU のバッファへ置き、束が
     * 一覧から外れてから十分な回数の描画を待って捨てる。定数・記述子セットは毎フレーム文脈の FrameResources（フレームの持ち主）
     * から借りる（パスを書く人向けの約束は Docs/Architecture/FrameResources.md）。
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

        /// 書き換えない頂点・インデックス・インスタンスの配列を置いた GPU のバッファ（元の値の参照を持ち、同じ番地の再利用を防ぐ）
        struct StaticBuffer
        {
            Container::TSharedPtr<const FoliageMeshData> MeshSource;
            Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> InstanceSource;
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            RHI::BufferPtr InstanceBuffer;
            uint64_t LastUsedExecute = 0;
        };

        /// メッシュの頂点・インデックスのバッファを outVertex・outIndex へ返す（無ければ作る）
        bool AcquireMeshBuffers(const Container::TSharedPtr<const FoliageMeshData> &mesh, RHI::BufferPtr &outVertex,
                                RHI::BufferPtr &outIndex);
        /// インスタンスの配列のバッファを返す（無ければ作る）
        RHI::BufferPtr AcquireInstanceBuffer(const Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> &instances);
        /// 長く使われていないバッファを捨てる
        void ReleaseUnusedStaticBuffers();

        RHI::IDevice *m_Device = nullptr;
        Container::VariableArray<StaticBuffer> m_StaticBuffers;
        // Execute の回数（バッファを捨てる時期の判定に使う）
        uint64_t m_ExecuteCount = 0;
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
