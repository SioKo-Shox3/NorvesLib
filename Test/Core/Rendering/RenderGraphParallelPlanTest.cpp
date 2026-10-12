// RenderGraph の記録の前の計画（SCN-ECS-RT-PLAN）の契約を確かめる。
// 計画したバリアの layout が、今の逐次の実行（共有の layout を読み書きする従来のバリア）の layout と
// 全バリアで一致すること、宣言から計画した区間は記録の最中に共有の layout を更新しないこと、
// 記録の後に書き戻した最終の layout が逐次の実行と同じになることを、Serial の区間と計画した区間の両方で見る。
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "Container/PointerTypes.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    // 共有の layout を持つ偽のテクスチャ（VulkanTexture の追跡値の代わり）
    class LayoutTexture final : public RHI::ITexture
    {
    public:
        LayoutTexture(RHI::ResourceUsage usage, RHI::TextureLayout layout)
            : Layout(layout), m_Usage(usage)
        {
        }

        uint32_t GetWidth() const override { return 16; }
        uint32_t GetHeight() const override { return 16; }
        uint32_t GetDepth() const override { return 1; }
        uint32_t GetMipLevels() const override { return 1; }
        uint32_t GetArraySize() const override { return 1; }
        RHI::Format GetFormat() const override { return RHI::Format::R8G8B8A8_UNORM; }
        RHI::ResourceUsage GetUsage() const override { return m_Usage; }
        bool IsCubemap() const override { return false; }
        void Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

        RHI::TextureLayout Layout;

    private:
        RHI::ResourceUsage m_Usage;
    };

    struct RecordedBarrier
    {
        const RHI::ITexture* Texture = nullptr;
        RHI::TextureLayout LiveLayout = RHI::TextureLayout::Undefined; // バリアを記録した時点の共有の layout
        RHI::TextureLayout OldLayout = RHI::TextureLayout::Undefined;
        RHI::TextureLayout NewLayout = RHI::TextureLayout::Undefined;
    };

    // VulkanCommandList の layout の扱いを写した偽のリスト。
    // TextureBarrier は従来どおり共有の layout を読み書きし、TextureBarrierExplicit は読み書きしない。
    class LayoutCommandList final : public RHI::ICommandList
    {
    public:
        Container::VariableArray<RecordedBarrier> LegacyBarriers;
        Container::VariableArray<RecordedBarrier> ExplicitBarriers;
        uint32_t SuppressedLayoutWrites = 0;
        uint32_t CommitCount = 0;
        bool bSharedWrites = true;

        // EndRenderPass の写し: render pass の finalState を attachment の共有の layout へ書く。
        void EndRenderPassWithFinalState(RHI::ITexture* texture, RHI::ResourceState finalState)
        {
            WriteShared(texture, RHI::ResourceStateToTextureLayout(finalState));
        }

        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            // 計画の対象は全体を覆うバリアだけ（RenderGraph のバリアは常に全体）
            assert(mipLevel == 0 && arrayIndex == 0 && mipCount == 0 && arrayCount == 0);
            auto* layoutTexture = static_cast<LayoutTexture*>(texture.get());
            RecordedBarrier record;
            record.Texture = texture.get();
            record.LiveLayout = layoutTexture->Layout;
            record.OldLayout = RHI::ResourceStateToTextureLayout(beforeState);
            record.NewLayout = RHI::ResourceStateToTextureLayout(afterState);
            LegacyBarriers.push_back(record);
            WriteShared(texture.get(), record.NewLayout);
        }

        void TextureBarrierExplicit(RHI::TexturePtr texture, const RHI::ExplicitTextureBarrierDesc& desc) override
        {
            RecordedBarrier record;
            record.Texture = texture.get();
            record.LiveLayout = static_cast<LayoutTexture*>(texture.get())->Layout;
            record.OldLayout = desc.OldLayout;
            record.NewLayout = desc.NewLayout;
            ExplicitBarriers.push_back(record);
        }

        RHI::TextureLayout GetTextureLayout(const RHI::ITexture* texture) const override
        {
            return static_cast<const LayoutTexture*>(texture)->Layout;
        }

        void CommitTextureLayout(RHI::ITexture* texture, RHI::TextureLayout layout) override
        {
            ++CommitCount;
            static_cast<LayoutTexture*>(texture)->Layout = layout;
        }

        void SetSharedTextureLayoutWrites(bool bEnabled) override
        {
            bSharedWrites = bEnabled;
        }

        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            (void)renderPass;
            (void)framebuffer;
        }
        void EndRenderPass() override {}
        void SetViewport(const RHI::Viewport& viewport) override { (void)viewport; }
        void SetScissor(const RHI::ScissorRect& scissor) override { (void)scissor; }
        void SetPipeline(RHI::PipelinePtr pipeline) override { (void)pipeline; }
        void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
        {
            (void)buffer;
            (void)offset;
            (void)slot;
        }
        void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, RHI::IndexType = RHI::IndexType::Uint32) override
        {
            (void)buffer;
            (void)offset;
        }
        void SetConstantBuffer(RHI::BufferPtr buffer, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)buffer;
            (void)slot;
            (void)stage;
        }
        void SetTexture(RHI::TexturePtr texture, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)texture;
            (void)slot;
            (void)stage;
        }
        void SetSampler(RHI::SamplerPtr sampler, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)sampler;
            (void)slot;
            (void)stage;
        }
        void SetDescriptorSet(RHI::DescriptorSetPtr descriptorSet, uint32_t slot = 0) override
        {
            (void)descriptorSet;
            (void)slot;
        }
        void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0) override
        {
            (void)indexCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
        }
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
        {
            (void)vertexCount;
            (void)startVertexLocation;
        }
        void DrawIndexedInstanced(uint32_t indexCount,
                                  uint32_t instanceCount,
                                  uint32_t startIndexLocation = 0,
                                  int32_t baseVertexLocation = 0,
                                  uint32_t startInstanceLocation = 0) override
        {
            (void)indexCount;
            (void)instanceCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
            (void)startInstanceLocation;
        }
        void DrawInstanced(uint32_t vertexCount,
                           uint32_t instanceCount,
                           uint32_t startVertexLocation = 0,
                           uint32_t startInstanceLocation = 0) override
        {
            (void)vertexCount;
            (void)instanceCount;
            (void)startVertexLocation;
            (void)startInstanceLocation;
        }
        void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer, uint64_t offset, uint32_t drawCount, uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)offset;
            (void)drawCount;
            (void)stride;
        }
        void DrawIndexedIndirectCount(RHI::BufferPtr indirectBuffer,
                                      uint64_t indirectOffset,
                                      RHI::BufferPtr countBuffer,
                                      uint64_t countOffset,
                                      uint32_t maxDrawCount,
                                      uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)indirectOffset;
            (void)countBuffer;
            (void)countOffset;
            (void)maxDrawCount;
            (void)stride;
        }
        void FillBuffer(RHI::BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) override
        {
            (void)buffer;
            (void)offset;
            (void)size;
            (void)value;
        }
        void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) override
        {
            (void)threadGroupCountX;
            (void)threadGroupCountY;
            (void)threadGroupCountZ;
        }
        void CopyBuffer(RHI::BufferPtr src, RHI::BufferPtr dst, uint64_t size = 0, uint64_t srcOffset = 0, uint64_t dstOffset = 0) override
        {
            (void)src;
            (void)dst;
            (void)size;
            (void)srcOffset;
            (void)dstOffset;
        }
        void CopyBufferToTexture(RHI::BufferPtr src,
                                 RHI::TexturePtr dst,
                                 uint32_t width,
                                 uint32_t height,
                                 uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0,
                                 uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTextureToBuffer(RHI::TexturePtr src,
                                 RHI::BufferPtr dst,
                                 uint32_t width,
                                 uint32_t height,
                                 uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0,
                                 uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTexture(RHI::TexturePtr src,
                         RHI::TexturePtr dst,
                         uint32_t width,
                         uint32_t height,
                         uint32_t srcMipLevel = 0,
                         uint32_t srcArrayIndex = 0,
                         uint32_t dstMipLevel = 0,
                         uint32_t dstArrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)srcMipLevel;
            (void)srcArrayIndex;
            (void)dstMipLevel;
            (void)dstArrayIndex;
        }
        void GenerateMipmaps(RHI::TexturePtr texture) override { (void)texture; }
        void BufferBarrier(RHI::BufferPtr buffer,
                           RHI::ResourceState beforeState,
                           RHI::ResourceState afterState,
                           uint64_t offset = 0,
                           uint64_t size = 0) override
        {
            (void)buffer;
            (void)beforeState;
            (void)afterState;
            (void)offset;
            (void)size;
        }

    private:
        void WriteShared(RHI::ITexture* texture, RHI::TextureLayout layout)
        {
            if (!bSharedWrites)
            {
                ++SuppressedLayoutWrites;
                return;
            }
            static_cast<LayoutTexture*>(texture)->Layout = layout;
        }
    };

    struct ScriptedAccess
    {
        RGResourceHandle* Handle = nullptr;
        RHI::ResourceState State = RHI::ResourceState::ShaderResource;
        RHI::ResourceState FinalState = RHI::ResourceState::ShaderResource;
        bool bWrite = false;
        bool bAttachment = false;
        RGAttachmentKind AttachmentKind = RGAttachmentKind::Color;
    };

    struct ScriptedImport
    {
        RHI::TexturePtr Texture;
        RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
        RGResourceHandle* Handle = nullptr;
    };

    // 宣言のとおりに render pass の最終の layout を書き、指定があれば宣言に無いバリアも張るパス
    class ScriptedPass final : public IRenderGraphPass
    {
    public:
        ScriptedPass(const char* name, bool bDeclaresAllLayoutChanges)
            : m_Name(name), m_bDeclaresAllLayoutChanges(bDeclaresAllLayoutChanges)
        {
        }

        Container::VariableArray<ScriptedImport> Imports;
        Container::VariableArray<ScriptedAccess> Accesses;
        // 宣言に無いパスの中のバリア（Serial のパスが自分で張るもの）
        RHI::TexturePtr InternalBarrierTexture;
        RHI::ResourceState InternalBefore = RHI::ResourceState::Undefined;
        RHI::ResourceState InternalAfter = RHI::ResourceState::Undefined;

        const char* GetName() const override { return m_Name; }
        bool DeclaresAllLayoutChanges() const override { return m_bDeclaresAllLayoutChanges; }

        void Declare(RenderGraphBuilder& builder) override
        {
            for (const ScriptedImport& import : Imports)
            {
                *import.Handle = builder.ImportTexture(import.Texture, import.InitialState, m_Name);
            }
            for (const ScriptedAccess& access : Accesses)
            {
                if (access.bAttachment)
                {
                    const bool bOk = builder.UseAttachment(*access.Handle,
                                                           access.AttachmentKind,
                                                           RGAttachmentMutability::Write,
                                                           RHI::AttachmentLoadOp::Load,
                                                           RHI::AttachmentStoreOp::Store,
                                                           access.State,
                                                           access.FinalState);
                    assert(bOk);
                    (void)bOk;
                }
                else if (access.bWrite)
                {
                    builder.Write(*access.Handle, access.State, access.FinalState);
                }
                else
                {
                    builder.Read(*access.Handle, access.State);
                }
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            auto* commandList = static_cast<LayoutCommandList*>(context.CommandList);
            for (const ScriptedAccess& access : Accesses)
            {
                if (access.bAttachment || access.FinalState != access.State)
                {
                    commandList->EndRenderPassWithFinalState(resources.GetTextureRaw(*access.Handle), access.FinalState);
                }
            }
            if (InternalBarrierTexture)
            {
                commandList->TextureBarrier(InternalBarrierTexture, InternalBefore, InternalAfter);
            }
        }

    private:
        const char* m_Name = nullptr;
        bool m_bDeclaresAllLayoutChanges = false;
    };

    // 3 つの取り込んだテクスチャと 3 つのパスの組。パスごとに Serial か計画できるかを選ぶ。
    struct PlanFixture
    {
        Container::TSharedPtr<LayoutTexture> Color;
        Container::TSharedPtr<LayoutTexture> Storage;
        Container::TSharedPtr<LayoutTexture> Depth;
        // 実行の前に保留のコマンドで Undefined から読み取りへ移すテクスチャ（bPreQueuedBarrier のときだけ）
        Container::TSharedPtr<LayoutTexture> Sampled;
        RGResourceHandle ColorHandle;
        RGResourceHandle StorageHandle;
        RGResourceHandle DepthHandle;
        RGResourceHandle SampledHandle;
        Container::VariableArray<Container::TUniquePtr<ScriptedPass>> Passes;

        void ResetLayouts()
        {
            Color->Layout = RHI::TextureLayout::ShaderReadOnly;
            Storage->Layout = RHI::TextureLayout::ShaderReadOnly;
            Depth->Layout = RHI::TextureLayout::DepthStencilReadOnly;
            if (Sampled)
            {
                Sampled->Layout = RHI::TextureLayout::Undefined;
            }
        }

        // bInternalBarrierInPass1: パス 1 が宣言に無いバリアで Storage を ShaderResource へ移す
        // bPreQueuedBarrier: 共有の layout が Undefined の Sampled を初期状態 ShaderResource で取り込み、パス 0 が読む。
        //                    実行の前の保留のコマンドが Undefined → ShaderResource のバリアを張る。
        PlanFixture(bool bPass0Plannable,
                    bool bPass1Plannable,
                    bool bPass2Plannable,
                    bool bInternalBarrierInPass1,
                    bool bPreQueuedBarrier = false)
        {
            const RHI::ResourceUsage colorUsage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
            const RHI::ResourceUsage storageUsage = RHI::ResourceUsage::UnorderedAccess | RHI::ResourceUsage::ShaderRead;
            const RHI::ResourceUsage depthUsage = RHI::ResourceUsage::DepthStencil | RHI::ResourceUsage::ShaderRead;
            Color = Container::MakeShared<LayoutTexture>(colorUsage, RHI::TextureLayout::ShaderReadOnly);
            Storage = Container::MakeShared<LayoutTexture>(storageUsage, RHI::TextureLayout::ShaderReadOnly);
            Depth = Container::MakeShared<LayoutTexture>(depthUsage, RHI::TextureLayout::DepthStencilReadOnly);

            // パス 0: Color を描いて読み取りへ、Depth を書いて読み取り専用へ
            auto pass0 = Container::MakeUnique<ScriptedPass>("PlanPass0", bPass0Plannable);
            pass0->Imports.push_back({Color, RHI::ResourceState::ShaderResource, &ColorHandle});
            pass0->Imports.push_back({Storage, RHI::ResourceState::ShaderResource, &StorageHandle});
            pass0->Imports.push_back({Depth, RHI::ResourceState::DepthRead, &DepthHandle});
            pass0->Accesses.push_back({&ColorHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource, true, true, RGAttachmentKind::Color});
            pass0->Accesses.push_back({&DepthHandle, RHI::ResourceState::DepthWrite, RHI::ResourceState::DepthRead, true, true, RGAttachmentKind::DepthStencil});
            if (bPreQueuedBarrier)
            {
                Sampled = Container::MakeShared<LayoutTexture>(RHI::ResourceUsage::ShaderRead, RHI::TextureLayout::Undefined);
                pass0->Imports.push_back({Sampled, RHI::ResourceState::ShaderResource, &SampledHandle});
                pass0->Accesses.push_back({&SampledHandle, RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource, false, false, RGAttachmentKind::Color});
            }

            // パス 1: Color を読み、Storage を UAV で書く
            auto pass1 = Container::MakeUnique<ScriptedPass>("PlanPass1", bPass1Plannable);
            pass1->Accesses.push_back({&ColorHandle, RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource, false, false, RGAttachmentKind::Color});
            pass1->Accesses.push_back({&StorageHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess, true, false, RGAttachmentKind::Color});
            if (bInternalBarrierInPass1)
            {
                pass1->InternalBarrierTexture = Storage;
                pass1->InternalBefore = RHI::ResourceState::UnorderedAccess;
                pass1->InternalAfter = RHI::ResourceState::ShaderResource;
            }

            // パス 2: Storage と Depth を読み、Color をもう一度描く
            auto pass2 = Container::MakeUnique<ScriptedPass>("PlanPass2", bPass2Plannable);
            pass2->Accesses.push_back({&StorageHandle, RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource, false, false, RGAttachmentKind::Color});
            pass2->Accesses.push_back({&DepthHandle, RHI::ResourceState::DepthRead, RHI::ResourceState::DepthRead, false, false, RGAttachmentKind::DepthStencil});
            pass2->Accesses.push_back({&ColorHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource, true, true, RGAttachmentKind::Color});

            Passes.push_back(std::move(pass0));
            Passes.push_back(std::move(pass1));
            Passes.push_back(std::move(pass2));
        }
    };

    struct FinalLayouts
    {
        RHI::TextureLayout Color = RHI::TextureLayout::Undefined;
        RHI::TextureLayout Storage = RHI::TextureLayout::Undefined;
        RHI::TextureLayout Depth = RHI::TextureLayout::Undefined;
        RHI::TextureLayout Sampled = RHI::TextureLayout::Undefined;
    };

    FinalLayouts CaptureLayouts(const PlanFixture& fixture)
    {
        return FinalLayouts{fixture.Color->Layout,
                            fixture.Storage->Layout,
                            fixture.Depth->Layout,
                            fixture.Sampled ? fixture.Sampled->Layout : RHI::TextureLayout::Undefined};
    }

    // 今の逐次の実行: コンパイルした順に、従来のバリア（共有の layout を読み書きする）とパスの記録を交互に行う。
    Container::VariableArray<RecordedBarrier> RunSequentialReference(RenderGraph& graph,
                                                                     PlanFixture& fixture,
                                                                     ViewRenderContext& context,
                                                                     FinalLayouts& outFinal)
    {
        auto* commandList = static_cast<LayoutCommandList*>(context.CommandList);
        commandList->LegacyBarriers.clear();
        fixture.ResetLayouts();

        // 実行の前に積んだ保留のコマンドは、最初のパスより前に従来のバリアとして流れる
        if (fixture.Sampled)
        {
            commandList->TextureBarrier(fixture.Sampled, RHI::ResourceState::Undefined, RHI::ResourceState::ShaderResource);
        }

        // グラフのバリアの記録だけを、コンパイルしたバリアと同じ並びで取り出す（パスの中のバリアは混ぜない）
        Container::VariableArray<RecordedBarrier> graphBarriers;

        RenderGraphResources resources(&graph);
        const auto& order = graph.GetCompiledPassOrder();
        const auto& barriers = graph.GetCompiledBarriers();
        uint32_t barrierIndex = 0;
        for (uint32_t orderIndex = 0; orderIndex < order.size(); ++orderIndex)
        {
            for (; barrierIndex < barriers.size() && barriers[barrierIndex].CompiledOrderIndex == orderIndex; ++barrierIndex)
            {
                const RGCompiledBarrier& barrier = barriers[barrierIndex];
                assert(barrier.Kind == RGBarrierKind::Texture);
                commandList->TextureBarrier(resources.GetTexture(barrier.Resource), barrier.BeforeState, barrier.AfterState);
                graphBarriers.push_back(commandList->LegacyBarriers.back());
            }
            fixture.Passes[order[orderIndex]]->Execute(resources, context);
        }

        outFinal = CaptureLayouts(fixture);
        return graphBarriers;
    }

    struct PlanComparison
    {
        bool bAllBarriersMatch = true;
        bool bFinalLayoutsMatch = true;
    };

    PlanComparison RunPlannedAndCompare(PlanFixture& fixture, Container::VariableArray<RGRecordSegment>* outSegments = nullptr)
    {
        RenderGraph graph;
        const bool bInitialized = graph.Initialize(nullptr);
        assert(bInitialized);
        for (auto& pass : fixture.Passes)
        {
            graph.AddPass(pass.get());
        }
        const bool bDependenciesAdded = graph.AddDependency(0, 1) && graph.AddDependency(1, 2);
        assert(bDependenciesAdded);

        LayoutCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        const bool bCompiled = graph.Compile(context);
        assert(bCompiled);
        assert(!graph.GetCompiledBarriers().empty());

        FinalLayouts referenceFinal;
        const Container::VariableArray<RecordedBarrier> reference =
            RunSequentialReference(graph, fixture, context, referenceFinal);

        // 計画した実行（記録は 1 本のリストのまま）
        fixture.ResetLayouts();
        commandList.LegacyBarriers.clear();
        commandList.ExplicitBarriers.clear();
        SceneRenderer renderer;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        if (fixture.Sampled)
        {
            context.Renderer = &renderer;
            context.PendingFrameCommands = &pendingFrameCommands;
            pendingFrameCommands.push_back(FrameCommand::CreateTextureBarrier(fixture.Sampled,
                                                                              RHI::ResourceState::Undefined,
                                                                              RHI::ResourceState::ShaderResource));
        }
        const bool bExecuted = graph.Execute(context);
        assert(bExecuted);
        assert(pendingFrameCommands.empty());
        assert(commandList.bSharedWrites);
        assert(graph.GetLastExecutedPassCount() == 3);

        const auto& compiledBarriers = graph.GetCompiledBarriers();
        const auto& planned = graph.GetPlannedBarriers();
        assert(planned.size() == compiledBarriers.size());
        assert(commandList.ExplicitBarriers.size() == compiledBarriers.size());

        assert(reference.size() == compiledBarriers.size());
        PlanComparison comparison;
        for (uint32_t barrierIndex = 0; barrierIndex < compiledBarriers.size(); ++barrierIndex)
        {
            RenderGraphResources resources(&graph);
            const RHI::ITexture* texture = resources.GetTextureRaw(compiledBarriers[barrierIndex].Resource);
            const RecordedBarrier& expected = reference[barrierIndex];
            assert(expected.Texture == texture);

            const RGPlannedBarrier& plan = planned[barrierIndex];
            assert(plan.bPlanned);
            // 張ったバリアは計画どおりの old/new
            assert(commandList.ExplicitBarriers[barrierIndex].Texture == texture);
            assert(commandList.ExplicitBarriers[barrierIndex].OldLayout == plan.OldLayout);
            assert(commandList.ExplicitBarriers[barrierIndex].NewLayout == plan.NewLayout);

            const bool bMatch = plan.ExpectedCurrentLayout == expected.LiveLayout &&
                                plan.OldLayout == expected.OldLayout &&
                                plan.NewLayout == expected.NewLayout;
            if (!bMatch)
            {
                comparison.bAllBarriersMatch = false;
            }
        }

        const FinalLayouts plannedFinal = CaptureLayouts(fixture);
        comparison.bFinalLayoutsMatch = plannedFinal.Color == referenceFinal.Color &&
                                        plannedFinal.Storage == referenceFinal.Storage &&
                                        plannedFinal.Depth == referenceFinal.Depth &&
                                        plannedFinal.Sampled == referenceFinal.Sampled;

        // 計画した区間の記録の最中は、共有の layout を書かない
        for (const RGRecordSegment& segment : graph.GetRecordSegments())
        {
            if (!segment.bLayoutPlanned || segment.EndOrderIndex - segment.FirstOrderIndex < 2)
            {
                continue;
            }
            assert(commandList.SuppressedLayoutWrites > 0);
        }

        if (outSegments)
        {
            *outSegments = graph.GetRecordSegments();
        }
        return comparison;
    }

    void TestSerialSegmentsMatchSequentialLayouts()
    {
        // 全パスが Serial。パス 1 は宣言に無いバリアで layout を変えるので、記録の後に確定しないと次のバリアの見込みが外れる。
        PlanFixture fixture(false, false, false, true);
        Container::VariableArray<RGRecordSegment> segments;
        const PlanComparison comparison = RunPlannedAndCompare(fixture, &segments);
        assert(comparison.bAllBarriersMatch);
        assert(comparison.bFinalLayoutsMatch);
        assert(segments.size() == 3);
        for (const RGRecordSegment& segment : segments)
        {
            assert(!segment.bLayoutPlanned);
            assert(segment.EndOrderIndex == segment.FirstOrderIndex + 1);
        }
        std::cout << "TestSerialSegmentsMatchSequentialLayouts: 合格\n";
    }

    void TestPlannedSegmentMatchesSequentialLayouts()
    {
        // 全パスが宣言どおりにだけ layout を変える。1 つの区間で、最終の layout まで記録の前に決める。
        PlanFixture fixture(true, true, true, false);
        Container::VariableArray<RGRecordSegment> segments;
        const PlanComparison comparison = RunPlannedAndCompare(fixture, &segments);
        assert(comparison.bAllBarriersMatch);
        assert(comparison.bFinalLayoutsMatch);
        assert(segments.size() == 1);
        assert(segments[0].bLayoutPlanned);
        assert(segments[0].FirstOrderIndex == 0 && segments[0].EndOrderIndex == 3);
        std::cout << "TestPlannedSegmentMatchesSequentialLayouts: 合格\n";
    }

    void TestMixedSegmentsMatchSequentialLayouts()
    {
        // 計画した区間 → Serial の区間（宣言に無いバリア） → 計画した区間
        PlanFixture fixture(true, false, true, true);
        Container::VariableArray<RGRecordSegment> segments;
        const PlanComparison comparison = RunPlannedAndCompare(fixture, &segments);
        assert(comparison.bAllBarriersMatch);
        assert(comparison.bFinalLayoutsMatch);
        assert(segments.size() == 3);
        assert(segments[0].bLayoutPlanned && !segments[1].bLayoutPlanned && segments[2].bLayoutPlanned);
        std::cout << "TestMixedSegmentsMatchSequentialLayouts: 合格\n";
    }

    void TestUndeclaredBarrierInPlannedSegmentIsDetected()
    {
        // 宣言に無いバリアを張るパスを計画の対象にすると、次のバリアの見込みが逐次の実行と食い違う（比較が空振りしないこと）。
        PlanFixture fixture(true, true, true, true);
        const PlanComparison comparison = RunPlannedAndCompare(fixture);
        assert(!comparison.bAllBarriersMatch);
        std::cout << "TestUndeclaredBarrierInPlannedSegmentIsDetected: 合格\n";
    }

    void TestPreQueuedBarrierLayoutIsWrittenBack()
    {
        // 実行の前の保留のコマンドが張るバリアは、計画した区間の頭より前に流れ、その最終の layout が共有の layout に残る。
        PlanFixture fixture(true, true, true, false, true);
        Container::VariableArray<RGRecordSegment> segments;
        const PlanComparison comparison = RunPlannedAndCompare(fixture, &segments);
        assert(comparison.bAllBarriersMatch);
        assert(comparison.bFinalLayoutsMatch);
        assert(fixture.Sampled->Layout == RHI::TextureLayout::ShaderReadOnly);
        assert(segments.size() == 1 && segments[0].bLayoutPlanned);
        std::cout << "TestPreQueuedBarrierLayoutIsWrittenBack: 合格\n";
    }
} // namespace

int main()
{
    TestSerialSegmentsMatchSequentialLayouts();
    TestPlannedSegmentMatchesSequentialLayouts();
    TestMixedSegmentsMatchSequentialLayouts();
    TestUndeclaredBarrierInPlannedSegmentIsDetected();
    TestPreQueuedBarrierLayoutIsWrittenBack();
    std::cout << "RenderGraphParallelPlanTest: 合格\n";
    return 0;
}
