// RenderGraph の区間ごとの記録（SCN-ECS-RT-GRAPH）の契約を偽のリストで確かめる。
// 区間の分け方（グラフの頭と Parallel のパスで新しいリスト）、バリアがそのパスの区間のリストへ入ること、
// 区間ごとの保留のコマンドがその区間のリストのパスの後へ流れること、区間のリストを受け取った順に並べると
// 1 本へ記録したときと同じコマンドの並びになること、Serial だけのグラフは今と同じ 1 本の記録になること、
// 各パスの記録が記録したスレッドの番号で RG.Record.<パス> の区間として残ることを見る。
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "Container/PointerTypes.h"
#include "Debug/Stats.h"
#include "Thread/JobSystem.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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
        explicit LayoutTexture(RHI::TextureLayout layout)
            : Layout(layout)
        {
        }

        uint32_t GetWidth() const override { return 16; }
        uint32_t GetHeight() const override { return 16; }
        uint32_t GetDepth() const override { return 1; }
        uint32_t GetMipLevels() const override { return 1; }
        uint32_t GetArraySize() const override { return 1; }
        RHI::Format GetFormat() const override { return RHI::Format::R8G8B8A8_UNORM; }
        RHI::ResourceUsage GetUsage() const override
        {
            return RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
        }
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
    };

    // 保留のコマンド（バッファのバリア）の行き先を見るための偽のバッファ
    class DummyBuffer final : public RHI::IBuffer
    {
    public:
        uint64_t GetSize() const override { return 16; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)offset;
            (void)size;
            return nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            (void)data;
            (void)size;
            (void)offset;
        }
        RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::ShaderRead; }
    };

    enum class EventKind : uint8_t
    {
        TextureBarrier,
        BufferBarrier,
        Draw,
    };

    struct RecordEvent
    {
        EventKind Kind = EventKind::Draw;
        const void* Resource = nullptr;
        RHI::TextureLayout OldLayout = RHI::TextureLayout::Undefined;
        RHI::TextureLayout NewLayout = RHI::TextureLayout::Undefined;
        uint32_t PassId = 0;

        bool operator==(const RecordEvent& other) const
        {
            return Kind == other.Kind && Resource == other.Resource && OldLayout == other.OldLayout &&
                   NewLayout == other.NewLayout && PassId == other.PassId;
        }
    };

    // 記録したコマンドを並びのまま残す偽のリスト。layout の扱いは VulkanCommandList の写し
    // （明示のバリアは共有の layout を読み書きせず、書き込みを止めている間は render pass の最終の layout も書かない）。
    class RecordCommandList final : public RHI::ICommandList
    {
    public:
        Container::VariableArray<RecordEvent> Events;
        uint32_t SuppressedLayoutWrites = 0;
        bool bSharedWrites = true;

        void EndRenderPassWithFinalState(RHI::ITexture* texture, RHI::ResourceState finalState)
        {
            if (!bSharedWrites)
            {
                ++SuppressedLayoutWrites;
                return;
            }
            static_cast<LayoutTexture*>(texture)->Layout = RHI::ResourceStateToTextureLayout(finalState);
        }

        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            // グラフは明示の old/new のバリアだけを張る
            (void)texture;
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
            assert(false);
        }

        void TextureBarrierExplicit(RHI::TexturePtr texture, const RHI::ExplicitTextureBarrierDesc& desc) override
        {
            RecordEvent event;
            event.Kind = EventKind::TextureBarrier;
            event.Resource = texture.get();
            event.OldLayout = desc.OldLayout;
            event.NewLayout = desc.NewLayout;
            Events.push_back(event);
        }

        RHI::TextureLayout GetTextureLayout(const RHI::ITexture* texture) const override
        {
            return static_cast<const LayoutTexture*>(texture)->Layout;
        }

        void CommitTextureLayout(RHI::ITexture* texture, RHI::TextureLayout layout) override
        {
            static_cast<LayoutTexture*>(texture)->Layout = layout;
        }

        void SetSharedTextureLayoutWrites(bool bEnabled) override
        {
            bSharedWrites = bEnabled;
        }

        void BufferBarrier(RHI::BufferPtr buffer,
                           RHI::ResourceState beforeState,
                           RHI::ResourceState afterState,
                           uint64_t offset = 0,
                           uint64_t size = 0) override
        {
            (void)beforeState;
            (void)afterState;
            (void)offset;
            (void)size;
            RecordEvent event;
            event.Kind = EventKind::BufferBarrier;
            event.Resource = buffer.get();
            Events.push_back(event);
        }

        // パスの記録の印（頂点の数をパスの番号として使う）
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
        {
            (void)startVertexLocation;
            RecordEvent event;
            event.Kind = EventKind::Draw;
            event.PassId = vertexCount;
            Events.push_back(event);
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
    };

    // 受け取った順に偽のリストを渡す口（受け取った順が提出の順）
    class RecordListSource final : public IRenderGraphRecordListSource
    {
    public:
        Container::VariableArray<Container::TUniquePtr<RecordCommandList>> Lists;

        RHI::ICommandList* AcquireRecordList() override
        {
            Lists.push_back(Container::MakeUnique<RecordCommandList>());
            return Lists.back().get();
        }
    };

    // 自分の Texture を色の attachment として描き（RenderTarget → 最終 ShaderResource）、前のパスの Texture を読むパス
    class RecordPass final : public IRenderGraphPass
    {
    public:
        RecordPass(const char* name, uint32_t id, RGRecordPolicy policy, bool bDeclaresAllLayoutChanges)
            : m_Name(name), m_Id(id), m_Policy(policy), m_bDeclaresAllLayoutChanges(bDeclaresAllLayoutChanges)
        {
        }

        Container::TSharedPtr<LayoutTexture> Target;
        RGResourceHandle TargetHandle;
        RGResourceHandle* ReadHandle = nullptr;
        // 設定があれば、記録の中で保留のコマンド（このバッファのバリア）を積む
        RHI::BufferPtr PendingBuffer;

        // 直近の記録の記録先とスレッド
        RHI::ICommandList* ExecutedList = nullptr;
        uint32_t ExecutedThreadId = 0;

        const char* GetName() const override { return m_Name; }
        bool DeclaresAllLayoutChanges() const override { return m_bDeclaresAllLayoutChanges; }
        RGRecordPolicy GetRecordPolicy() const override { return m_Policy; }

        void Declare(RenderGraphBuilder& builder) override
        {
            TargetHandle = builder.ImportTexture(Target, RHI::ResourceState::ShaderResource, m_Name);
            const bool bOk = builder.UseAttachment(TargetHandle,
                                                   RGAttachmentKind::Color,
                                                   RGAttachmentMutability::Write,
                                                   RHI::AttachmentLoadOp::Load,
                                                   RHI::AttachmentStoreOp::Store,
                                                   RHI::ResourceState::RenderTarget,
                                                   RHI::ResourceState::ShaderResource);
            assert(bOk);
            (void)bOk;
            if (ReadHandle)
            {
                builder.Read(*ReadHandle, RHI::ResourceState::ShaderResource);
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            ExecutedList = context.CommandList;
            ExecutedThreadId = static_cast<uint32_t>(::GetCurrentThreadId());
            auto* commandList = static_cast<RecordCommandList*>(context.CommandList);
            commandList->Draw(m_Id);
            commandList->EndRenderPassWithFinalState(resources.GetTextureRaw(TargetHandle), RHI::ResourceState::ShaderResource);
            if (PendingBuffer)
            {
                context.EnqueueFrameCommand(FrameCommand::CreateBufferBarrier(PendingBuffer,
                                                                              RHI::ResourceState::UnorderedAccess,
                                                                              RHI::ResourceState::ShaderResource));
            }
        }

    private:
        const char* m_Name = nullptr;
        uint32_t m_Id = 0;
        RGRecordPolicy m_Policy = RGRecordPolicy::Serial;
        bool m_bDeclaresAllLayoutChanges = false;
    };

    struct PassSpec
    {
        const char* Name = nullptr;
        RGRecordPolicy Policy = RGRecordPolicy::Serial;
        bool bDeclaresAllLayoutChanges = false;
        bool bEnqueuesPending = false;
    };

    // パスを順に鎖でつないだグラフ（各パスが自分の Texture へバリアを 1 つ持つ）
    struct RecordFixture
    {
        Container::VariableArray<Container::TUniquePtr<RecordPass>> Passes;
        RHI::BufferPtr PendingBuffer;

        explicit RecordFixture(const Container::VariableArray<PassSpec>& specs)
        {
            PendingBuffer = Container::MakeShared<DummyBuffer>();
            for (uint32_t index = 0; index < specs.size(); ++index)
            {
                const PassSpec& spec = specs[index];
                auto pass = Container::MakeUnique<RecordPass>(spec.Name, index, spec.Policy, spec.bDeclaresAllLayoutChanges);
                pass->Target = Container::MakeShared<LayoutTexture>(RHI::TextureLayout::ShaderReadOnly);
                if (index > 0)
                {
                    pass->ReadHandle = &Passes.back()->TargetHandle;
                }
                if (spec.bEnqueuesPending)
                {
                    pass->PendingBuffer = PendingBuffer;
                }
                Passes.push_back(std::move(pass));
            }
        }

        void ResetLayouts()
        {
            for (auto& pass : Passes)
            {
                pass->Target->Layout = RHI::TextureLayout::ShaderReadOnly;
                pass->ExecutedList = nullptr;
                pass->ExecutedThreadId = 0;
            }
        }

        void Build(RenderGraph& graph, ViewRenderContext& context)
        {
            const bool bInitialized = graph.Initialize(nullptr);
            assert(bInitialized);
            (void)bInitialized;
            for (auto& pass : Passes)
            {
                graph.AddPass(pass.get());
            }
            for (uint32_t index = 1; index < Passes.size(); ++index)
            {
                const bool bAdded = graph.AddDependency(index - 1, index);
                assert(bAdded);
                (void)bAdded;
            }
            const bool bCompiled = graph.Compile(context);
            assert(bCompiled);
            (void)bCompiled;
        }
    };

    struct RunOutput
    {
        Container::VariableArray<RecordEvent> MainEvents;
        Container::VariableArray<RHI::TextureLayout> FinalLayouts;
    };

    // 区間ごとのリストを渡さずに（今と同じ 1 本へ）記録する
    RunOutput RunSingleList(RecordFixture& fixture)
    {
        RenderGraph graph;
        RecordCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        SceneRenderer renderer;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        fixture.Build(graph, context);
        fixture.ResetLayouts();

        const bool bExecuted = graph.Execute(context);
        assert(bExecuted);
        (void)bExecuted;
        assert(context.CommandList == &commandList);
        assert(graph.GetLastAcquiredRecordListCount() == 0);
        assert(commandList.bSharedWrites);
        assert(pendingFrameCommands.empty());

        RunOutput output;
        output.MainEvents = commandList.Events;
        for (auto& pass : fixture.Passes)
        {
            assert(pass->ExecutedList == &commandList);
            output.FinalLayouts.push_back(pass->Target->Layout);
        }
        return output;
    }

    uint32_t FindListSegment(const Container::VariableArray<RGRecordListSegment>& segments, uint32_t orderIndex)
    {
        for (uint32_t segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex)
        {
            if (orderIndex >= segments[segmentIndex].FirstOrderIndex && orderIndex < segments[segmentIndex].EndOrderIndex)
            {
                return segmentIndex;
            }
        }
        assert(false);
        return 0;
    }

    int32_t FindEvent(const Container::VariableArray<RecordEvent>& events, EventKind kind, const void* resource, uint32_t passId)
    {
        for (uint32_t index = 0; index < events.size(); ++index)
        {
            const RecordEvent& event = events[index];
            if (event.Kind == kind && (kind == EventKind::Draw ? event.PassId == passId : event.Resource == resource))
            {
                return static_cast<int32_t>(index);
            }
        }
        return -1;
    }

    void TestSerialOnlyGraphRecordsOneList()
    {
        // Serial だけ（計画できるパスと Serial の区間が混ざる）のグラフは、リストを受け取らず今の記録先 1 本へ記録する。
        Container::VariableArray<PassSpec> specs;
        specs.push_back({"SerialPass0", RGRecordPolicy::Serial, true, false});
        specs.push_back({"SerialPass1", RGRecordPolicy::Serial, false, true});
        specs.push_back({"SerialPass2", RGRecordPolicy::Serial, true, true});
        specs.push_back({"SerialPass3", RGRecordPolicy::Serial, false, false});
        RecordFixture fixture(specs);
        const RunOutput reference = RunSingleList(fixture);

        RenderGraph graph;
        RecordCommandList commandList;
        RecordListSource source;
        ViewRenderContext context;
        context.CommandList = &commandList;
        context.RecordListSource = &source;
        SceneRenderer renderer;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        fixture.Build(graph, context);
        fixture.ResetLayouts();

        const auto& listSegments = graph.GetRecordListSegments();
        assert(listSegments.size() == 1);
        assert(!listSegments[0].bParallel);
        assert(listSegments[0].FirstOrderIndex == 0 && listSegments[0].EndOrderIndex == specs.size());

        const bool bExecuted = graph.Execute(context);
        assert(bExecuted);
        (void)bExecuted;
        assert(source.Lists.empty());
        assert(graph.GetLastAcquiredRecordListCount() == 0);
        assert(context.CommandList == &commandList);
        assert(commandList.bSharedWrites);

        // 今と同じ 1 本の記録: バリア → パス → パスの後の保留のコマンド の並びが、リストの渡し口が無いときと一致する。
        assert(commandList.Events.size() == reference.MainEvents.size());
        for (uint32_t index = 0; index < commandList.Events.size(); ++index)
        {
            assert(commandList.Events[index] == reference.MainEvents[index]);
        }
        // 各パスにバリアが 1 つ、保留のコマンドを積む 2 つのパスの後にバッファのバリアが 1 つ
        assert(commandList.Events.size() == specs.size() * 2 + 2);
        for (uint32_t index = 0; index < fixture.Passes.size(); ++index)
        {
            assert(fixture.Passes[index]->ExecutedList == &commandList);
            assert(fixture.Passes[index]->Target->Layout == reference.FinalLayouts[index]);
        }
        std::cout << "TestSerialOnlyGraphRecordsOneList: 合格\n";
    }

    void TestParallelDeclarationWithoutLayoutPlanStaysSerial()
    {
        // layout を宣言から決められないパスは、Parallel を返しても Serial として同じリストへ続ける。
        Container::VariableArray<PassSpec> specs;
        specs.push_back({"UnplannedPass0", RGRecordPolicy::Serial, true, false});
        specs.push_back({"UnplannedPass1", RGRecordPolicy::Parallel, false, false});
        RecordFixture fixture(specs);
        RenderGraph graph;
        RecordCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        fixture.Build(graph, context);
        const auto& listSegments = graph.GetRecordListSegments();
        assert(listSegments.size() == 1);
        assert(listSegments[0].EndOrderIndex == 2 && !listSegments[0].bParallel);
        std::cout << "TestParallelDeclarationWithoutLayoutPlanStaysSerial: 合格\n";
    }

    void TestParallelSegmentsSplitBarriersAndOrder()
    {
        // 順: 0 Serial（計画） / 1 Parallel / 2 Parallel / 3 Serial（計画） / 4 Parallel / 5 Serial（計画できない）
        // リストの区間: [0] [1] [2 3] [4 5]。区間 1〜3 は渡し口から受け取ったリストで、受け取った順が提出の順。
        Container::VariableArray<PassSpec> specs;
        specs.push_back({"RecordPass0", RGRecordPolicy::Serial, true, false});
        specs.push_back({"RecordPass1", RGRecordPolicy::Parallel, true, true});
        specs.push_back({"RecordPass2", RGRecordPolicy::Parallel, true, false});
        specs.push_back({"RecordPass3", RGRecordPolicy::Serial, true, true});
        specs.push_back({"RecordPass4", RGRecordPolicy::Parallel, true, false});
        specs.push_back({"RecordPass5", RGRecordPolicy::Serial, false, false});
        RecordFixture fixture(specs);
        const RunOutput reference = RunSingleList(fixture);

        RenderGraph graph;
        RecordCommandList mainList;
        RecordListSource source;
        ViewRenderContext context;
        context.CommandList = &mainList;
        context.RecordListSource = &source;
        SceneRenderer renderer;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        fixture.Build(graph, context);
        fixture.ResetLayouts();

        // 区間の分け方
        const auto& listSegments = graph.GetRecordListSegments();
        assert(listSegments.size() == 4);
        const uint32_t expectedFirst[] = {0, 1, 2, 4};
        const uint32_t expectedEnd[] = {1, 2, 4, 6};
        const bool expectedParallel[] = {false, true, true, true};
        for (uint32_t segmentIndex = 0; segmentIndex < 4; ++segmentIndex)
        {
            assert(listSegments[segmentIndex].FirstOrderIndex == expectedFirst[segmentIndex]);
            assert(listSegments[segmentIndex].EndOrderIndex == expectedEnd[segmentIndex]);
            assert(listSegments[segmentIndex].bParallel == expectedParallel[segmentIndex]);
        }

#if NORVES_ENABLE_STATS
        auto& stats = NorvesLib::Debug::StatsManager::Get();
        const char* tracePath = "RenderGraphParallelRecordTest.trace.csv";
        const bool bTraceStarted = stats.StartTrace(Container::String(tracePath));
        assert(bTraceStarted);
        (void)bTraceStarted;
        stats.BeginFrame(1, 1.0f / 60.0f);
#endif
        const bool bExecuted = graph.Execute(context);
        assert(bExecuted);
        (void)bExecuted;
#if NORVES_ENABLE_STATS
        const NorvesLib::Debug::FrameProfile profile = stats.GetFrameProfileSnapshot();
        stats.StopTrace();
        std::remove(tracePath);
#endif
        assert(graph.GetLastExecutedPassCount() == specs.size());
        assert(pendingFrameCommands.empty());

        // 区間ごとのリスト: 0 番は今の記録先、1〜3 番は受け取った順のリスト。実行の後の記録先は最後のリスト。
        assert(source.Lists.size() == 3);
        assert(graph.GetLastAcquiredRecordListCount() == 3);
        RecordCommandList* segmentLists[] = {&mainList, source.Lists[0].get(), source.Lists[1].get(), source.Lists[2].get()};
        assert(context.CommandList == segmentLists[3]);
        for (RecordCommandList* list : segmentLists)
        {
            // 計画した区間で止めた共有の layout の書き込みは、区間の後に戻っている
            assert(list->bSharedWrites);
        }

        const auto& order = graph.GetCompiledPassOrder();
        for (uint32_t orderIndex = 0; orderIndex < order.size(); ++orderIndex)
        {
            RecordPass& pass = *fixture.Passes[order[orderIndex]];
            RecordCommandList* expectedList = segmentLists[FindListSegment(listSegments, orderIndex)];
            assert(pass.ExecutedList == expectedList);
            assert(FindEvent(expectedList->Events, EventKind::Draw, nullptr, order[orderIndex]) >= 0);
        }

        // バリアは、そのパスの区間のリストへ、パスの記録の前に入る
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == specs.size());
        for (const RGCompiledBarrier& barrier : barriers)
        {
            const uint32_t passIndex = order[barrier.CompiledOrderIndex];
            RecordCommandList* expectedList = segmentLists[FindListSegment(listSegments, barrier.CompiledOrderIndex)];
            const void* texture = fixture.Passes[passIndex]->Target.get();
            const int32_t barrierEvent = FindEvent(expectedList->Events, EventKind::TextureBarrier, texture, 0);
            const int32_t drawEvent = FindEvent(expectedList->Events, EventKind::Draw, nullptr, passIndex);
            assert(barrierEvent >= 0 && barrierEvent < drawEvent);
            for (RecordCommandList* list : segmentLists)
            {
                if (list != expectedList)
                {
                    assert(FindEvent(list->Events, EventKind::TextureBarrier, texture, 0) < 0);
                }
            }
        }

        // 区間ごとの保留のコマンドは、その区間のリストのパスの後へ流れる（パス 1 は区間 1、パス 3 は区間 2）
        const void* pendingBuffer = fixture.PendingBuffer.get();
        assert(FindEvent(segmentLists[1]->Events, EventKind::BufferBarrier, pendingBuffer, 0) >
               FindEvent(segmentLists[1]->Events, EventKind::Draw, nullptr, 1));
        assert(FindEvent(segmentLists[2]->Events, EventKind::BufferBarrier, pendingBuffer, 0) >
               FindEvent(segmentLists[2]->Events, EventKind::Draw, nullptr, 3));
        assert(FindEvent(segmentLists[0]->Events, EventKind::BufferBarrier, pendingBuffer, 0) < 0);
        assert(FindEvent(segmentLists[3]->Events, EventKind::BufferBarrier, pendingBuffer, 0) < 0);

        // 区間のリストを提出の順に並べると、1 本へ記録したときと同じコマンドの並び（計画した old/new も同じ）
        Container::VariableArray<RecordEvent> submitted;
        for (RecordCommandList* list : segmentLists)
        {
            for (const RecordEvent& event : list->Events)
            {
                submitted.push_back(event);
            }
        }
        assert(submitted.size() == reference.MainEvents.size());
        for (uint32_t index = 0; index < submitted.size(); ++index)
        {
            assert(submitted[index] == reference.MainEvents[index]);
        }
        for (uint32_t index = 0; index < fixture.Passes.size(); ++index)
        {
            assert(fixture.Passes[index]->Target->Layout == reference.FinalLayouts[index]);
        }

#if NORVES_ENABLE_STATS
        // 各パスの記録が、記録したスレッドの番号で RG.Record.<パス> の区間として 1 つずつ残る
        for (auto& pass : fixture.Passes)
        {
            Container::String scopeName("RG.Record.");
            scopeName += pass->GetName();
            uint32_t matched = 0;
            for (const NorvesLib::Debug::ProfileEvent& event : profile.Events)
            {
                if (std::strcmp(event.Name.c_str(), scopeName.c_str()) == 0)
                {
                    assert(event.ThreadId == pass->ExecutedThreadId);
                    ++matched;
                }
            }
            assert(matched == 1);
        }
#endif
        std::cout << "TestParallelSegmentsSplitBarriersAndOrder: 合格\n";
    }
} // namespace

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(2);
    TestSerialOnlyGraphRecordsOneList();
    TestParallelDeclarationWithoutLayoutPlanStaysSerial();
    TestParallelSegmentsSplitBarriersAndOrder();
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "RenderGraphParallelRecordTest: 合格\n";
    return 0;
}
