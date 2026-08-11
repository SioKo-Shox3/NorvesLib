#include "Rendering/FrameCommand.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace Container = NorvesLib::Core::Container;
    namespace Rendering = NorvesLib::Core::Rendering;
    namespace RHI = NorvesLib::RHI;

    class FakeTexture final : public RHI::ITexture
    {
    public:
        uint32_t GetWidth() const override { return 1u; }
        uint32_t GetHeight() const override { return 1u; }
        uint32_t GetDepth() const override { return 1u; }
        uint32_t GetMipLevels() const override { return 1u; }
        uint32_t GetArraySize() const override { return 1u; }
        RHI::Format GetFormat() const override { return RHI::Format::R8G8B8A8_UNORM; }
        RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::ShaderRead; }
        bool IsCubemap() const override { return false; }
        void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        explicit FakeCommandList(Container::VariableArray<Container::String>& events)
            : Events(events)
        {
        }

        uint32_t GetMaximumGPUTimestampScopesPerFrame() const override { return RHI::MaximumGPUTimestampScopesPerFrame; }
        RHI::GPUTimestampScopeHandle BeginGPUTimestampScope(const char* scopeName) override
        {
            if (NextScopeIndex >= RHI::MaximumGPUTimestampScopesPerFrame)
            {
                return {};
            }
            Events.push_back(Container::String("begin:") + scopeName);
            RHI::GPUTimestampScopeHandle handle;
            handle.FrameSlotIndex = 0u;
            handle.ScopeIndex = NextScopeIndex++;
            handle.FrameNumber = 7u;
            return handle;
        }
        void EndGPUTimestampScope(RHI::GPUTimestampScopeHandle handle) override
        {
            if (handle.IsValid())
            {
                Events.push_back(Container::String("end:") + ScopeNames[handle.ScopeIndex]);
            }
        }
        void AbortGPUTimestampFrame(uint32_t) noexcept override { Events.push_back("abort-frame"); }

        void Begin() override {}
        void End() override {}
        void Submit(bool = false) override {}
        void BeginRenderPass(RHI::RenderPassPtr, RHI::FramebufferPtr) override {}
        void EndRenderPass() override {}
        void SetViewport(const RHI::Viewport&) override {}
        void SetScissor(const RHI::ScissorRect&) override {}
        void SetPipeline(RHI::PipelinePtr) override {}
        void SetVertexBuffer(RHI::BufferPtr, uint64_t = 0, uint32_t = 0) override {}
        void SetIndexBuffer(RHI::BufferPtr, uint64_t = 0, RHI::IndexType = RHI::IndexType::Uint32) override {}
        void SetConstantBuffer(RHI::BufferPtr, uint32_t, RHI::ShaderStage) override {}
        void SetTexture(RHI::TexturePtr, uint32_t, RHI::ShaderStage) override {}
        void SetSampler(RHI::SamplerPtr, uint32_t, RHI::ShaderStage) override {}
        void SetDescriptorSet(RHI::DescriptorSetPtr, uint32_t = 0) override {}
        void DrawIndexed(uint32_t, uint32_t = 0, int32_t = 0) override {}
        void Draw(uint32_t, uint32_t = 0) override {}
        void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
        void DrawInstanced(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
        void DrawIndexedIndirect(RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferPtr, uint64_t, RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void FillBuffer(RHI::BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(RHI::BufferPtr, RHI::BufferPtr, uint64_t = 0, uint64_t = 0, uint64_t = 0) override {}
        void CopyBufferToTexture(RHI::BufferPtr, RHI::TexturePtr, uint32_t, uint32_t, uint64_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void CopyTextureToBuffer(RHI::TexturePtr, RHI::BufferPtr, uint32_t, uint32_t, uint64_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void CopyTexture(RHI::TexturePtr, RHI::TexturePtr, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void GenerateMipmaps(RHI::TexturePtr) override {}
        void BufferBarrier(RHI::BufferPtr, RHI::ResourceState, RHI::ResourceState, uint64_t = 0, uint64_t = 0) override {}
        void TextureBarrier(RHI::TexturePtr, RHI::ResourceState, RHI::ResourceState, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) override
        {
            Events.push_back(Container::String("flush:") + ScopeNames[LastExecutingPass]);
            if (bThrowOnFlush)
            {
                throw std::runtime_error("flush failure");
            }
        }

        Container::VariableArray<Container::String>& Events;
        Container::VariableArray<Container::String> ScopeNames;
        uint32_t NextScopeIndex = 0u;
        uint32_t LastExecutingPass = 0u;
        bool bThrowOnFlush = false;
    };

    class EventPass final : public Rendering::IRenderGraphPass
    {
    public:
        EventPass(const char* name,
                  uint32_t passIndex,
                  Container::VariableArray<Container::String>& events,
                  FakeCommandList& commandList,
                  RHI::TexturePtr flushTexture)
            : Name(name), PassIndex(passIndex), Events(events), CommandList(commandList), FlushTexture(flushTexture)
        {
            CommandList.ScopeNames.push_back(name);
        }

        const char* GetName() const override { return Name; }
        void Declare(Rendering::RenderGraphBuilder& builder) override { builder.PreserveInsertionOrder(); }
        void Execute(Rendering::RenderGraphResources&, Rendering::ViewRenderContext& context) override
        {
            CommandList.LastExecutingPass = PassIndex;
            Events.push_back(Container::String("execute:") + Name);
            if (bThrowOnExecute)
            {
                throw std::runtime_error("execute failure");
            }
            context.PendingFrameCommands->push_back(Rendering::FrameCommand::CreateTextureBarrier(
                FlushTexture,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource));
        }

        const char* Name = nullptr;
        uint32_t PassIndex = 0u;
        Container::VariableArray<Container::String>& Events;
        FakeCommandList& CommandList;
        RHI::TexturePtr FlushTexture;
        bool bThrowOnExecute = false;
    };

    bool Equals(const Container::VariableArray<Container::String>& actual,
                std::initializer_list<const char*> expected)
    {
        if (actual.size() != expected.size())
        {
            return false;
        }
        uint32_t index = 0u;
        for (const char* value : expected)
        {
            if (actual[index++] != value)
            {
                return false;
            }
        }
        return true;
    }

    void ExecuteGraphWithAbortOnFailure(Rendering::RenderGraph& graph,
                                        Rendering::ViewRenderContext& context,
                                        FakeCommandList& commandList)
    {
        try
        {
            graph.ExecuteWithResult(context);
        }
        catch (...)
        {
            commandList.AbortGPUTimestampFrame(0u);
        }
    }

    void AssertPassScopesIncludeExecuteAndFlush()
    {
        Container::VariableArray<Container::String> events;
        FakeCommandList commandList(events);
        Rendering::SceneRenderer renderer;
        Container::VariableArray<Rendering::FrameCommand> pendingCommands;
        RHI::TexturePtr texture = Container::MakeShared<FakeTexture>();
        EventPass first("FirstPass", 0u, events, commandList, texture);
        EventPass second("SecondPass", 1u, events, commandList, texture);
        Rendering::RenderGraph graph;
        graph.Initialize(nullptr);
        graph.BeginFrame(7u);
        graph.AddPass(&first);
        graph.AddPass(&second);
        assert(graph.Compile());
        Rendering::ViewRenderContext context;
        context.CommandList = &commandList;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingCommands;
        assert(graph.ExecuteWithResult(context).bSuccess);
        assert(Equals(events, {
            "begin:FirstPass", "execute:FirstPass", "flush:FirstPass", "end:FirstPass",
            "begin:SecondPass", "execute:SecondPass", "flush:SecondPass", "end:SecondPass"}));
    }

    void AssertExecuteThrowClosesScopeBeforeAbort()
    {
        Container::VariableArray<Container::String> events;
        FakeCommandList commandList(events);
        Rendering::SceneRenderer renderer;
        Container::VariableArray<Rendering::FrameCommand> pendingCommands;
        RHI::TexturePtr texture = Container::MakeShared<FakeTexture>();
        EventPass pass("ThrowExecute", 0u, events, commandList, texture);
        pass.bThrowOnExecute = true;
        Rendering::RenderGraph graph;
        graph.Initialize(nullptr);
        graph.BeginFrame(7u);
        graph.AddPass(&pass);
        assert(graph.Compile());
        Rendering::ViewRenderContext context;
        context.CommandList = &commandList;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingCommands;
        ExecuteGraphWithAbortOnFailure(graph, context, commandList);
        assert(Equals(events, {"begin:ThrowExecute", "execute:ThrowExecute", "end:ThrowExecute", "abort-frame"}));
    }

    void AssertFlushThrowClosesScopeBeforeAbort()
    {
        Container::VariableArray<Container::String> events;
        FakeCommandList commandList(events);
        commandList.bThrowOnFlush = true;
        Rendering::SceneRenderer renderer;
        Container::VariableArray<Rendering::FrameCommand> pendingCommands;
        RHI::TexturePtr texture = Container::MakeShared<FakeTexture>();
        EventPass pass("ThrowFlush", 0u, events, commandList, texture);
        Rendering::RenderGraph graph;
        graph.Initialize(nullptr);
        graph.BeginFrame(7u);
        graph.AddPass(&pass);
        assert(graph.Compile());
        Rendering::ViewRenderContext context;
        context.CommandList = &commandList;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingCommands;
        ExecuteGraphWithAbortOnFailure(graph, context, commandList);
        assert(Equals(events, {"begin:ThrowFlush", "execute:ThrowFlush", "flush:ThrowFlush", "end:ThrowFlush", "abort-frame"}));
    }

    void AssertScopeOverflowIsANoOp()
    {
        Container::VariableArray<Container::String> events;
        FakeCommandList commandList(events);
        for (uint32_t index = 0u; index < RHI::MaximumGPUTimestampScopesPerFrame; ++index)
        {
            assert(commandList.BeginGPUTimestampScope("Pass").IsValid());
        }
        assert(!commandList.BeginGPUTimestampScope("Overflow").IsValid());
        assert(events.size() == RHI::MaximumGPUTimestampScopesPerFrame);
    }
}

int main()
{
    AssertPassScopesIncludeExecuteAndFlush();
    AssertExecuteThrowClosesScopeBeforeAbort();
    AssertFlushThrowClosesScopeBeforeAbort();
    AssertScopeOverflowIsANoOp();
    std::cout << "RenderGraphGPUTimestampScopeTest passed\n";
    return 0;
}
