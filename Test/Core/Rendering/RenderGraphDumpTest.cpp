#include "Debug/DebugConfig.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include "Container/PointerTypes.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Identity;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

    private:
        RHI::TextureDesc m_Desc;
    };

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const RHI::BufferDesc& desc)
            : m_Desc(desc)
        {
        }

        uint64_t GetSize() const override { return m_Desc.Size; }
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
        RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }

    private:
        RHI::BufferDesc m_Desc;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        uint32_t BeginDebugMarkerCount = 0;
        uint32_t EndDebugMarkerCount = 0;
        Container::VariableArray<Container::String> MarkerNames;

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
        void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0) override
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
        void DrawIndexed(uint32_t indexCount,
                         uint32_t startIndexLocation = 0,
                         int32_t baseVertexLocation = 0) override
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
        void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer,
                                 uint64_t offset,
                                 uint32_t drawCount,
                                 uint32_t stride) override
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
        void Dispatch(uint32_t threadGroupCountX,
                      uint32_t threadGroupCountY,
                      uint32_t threadGroupCountZ) override
        {
            (void)threadGroupCountX;
            (void)threadGroupCountY;
            (void)threadGroupCountZ;
        }
        void CopyBuffer(RHI::BufferPtr src,
                        RHI::BufferPtr dst,
                        uint64_t size = 0,
                        uint64_t srcOffset = 0,
                        uint64_t dstOffset = 0) override
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
        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            (void)texture;
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
        }
        void BeginDebugMarker(const char* name) override
        {
            ++BeginDebugMarkerCount;
            MarkerNames.push_back(name ? Container::String(name) : Container::String());
        }
        void EndDebugMarker() override
        {
            ++EndDebugMarkerCount;
        }
    };

    class MockAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc& desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto buffer = Container::MakeUnique<FakeBuffer>(desc);

            RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Offset = 0;
            allocation.Size = desc.Size;
            allocation.Type = type;

            m_Buffers.push_back(std::move(buffer));
            return allocation;
        }

        void FreeBuffer(RHI::BufferAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Buffers.begin(); it != m_Buffers.end(); ++it)
            {
                if (it->get() == allocation.Buffer)
                {
                    m_Buffers.erase(it);
                    allocation = {};
                    return;
                }
            }
        }

        RHI::TextureAllocation AllocateTexture(const RHI::TextureDesc& desc,
                                               RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto texture = Container::MakeUnique<FakeTexture>(desc);

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(desc.Height) * 4u;
            allocation.Type = type;

            m_Textures.push_back(std::move(texture));
            return allocation;
        }

        void FreeTexture(RHI::TextureAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Textures.begin(); it != m_Textures.end(); ++it)
            {
                if (it->get() == allocation.Texture)
                {
                    m_Textures.erase(it);
                    allocation = {};
                    return;
                }
            }
        }

        size_t GetAllocatedMemory() const override { return 0; }
        size_t GetUsedMemory() const override { return 0; }
        void Trim() override {}

    private:
        Container::VariableArray<Container::TUniquePtr<FakeTexture>> m_Textures;
        Container::VariableArray<Container::TUniquePtr<FakeBuffer>> m_Buffers;
    };

    RGTextureDesc MakeColorDesc(const char* name)
    {
        return RGTextureDesc::RenderTarget(64, 32, RHI::Format::R8G8B8A8_UNORM, name);
    }

    RGTextureDesc MakeDepthDesc(const char* name)
    {
        return RGTextureDesc::DepthStencil(64, 32, RHI::Format::D32_FLOAT, name);
    }

    class ColorWritePass final : public IRenderGraphPass
    {
    public:
        explicit ColorWritePass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override { return "ColorWritePass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            builder.WriteTexture(m_Name,
                                 MakeColorDesc("DumpSceneColor"),
                                 RHI::ResourceState::RenderTarget,
                                 RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
    };

    class ColorReadPass final : public IRenderGraphPass
    {
    public:
        explicit ColorReadPass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override { return "ColorReadPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.ReadTexture(m_Name, RHI::ResourceState::ShaderResource);
            assert(handle.IsValid());
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
    };

    class ColorMutateAttachmentPass final : public IRenderGraphPass
    {
    public:
        explicit ColorMutateAttachmentPass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override { return "ColorMutateAttachmentPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle;
            assert(builder.TryUseAttachment(m_Name,
                                            handle,
                                            RGAttachmentKind::Color,
                                            RGAttachmentMutability::Write,
                                            RHI::AttachmentLoadOp::Load,
                                            RHI::AttachmentStoreOp::Store,
                                            RHI::ResourceState::RenderTarget,
                                            RHI::ResourceState::ShaderResource));
            assert(handle.IsValid());
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
    };

    class DepthWritePass final : public IRenderGraphPass
    {
    public:
        explicit DepthWritePass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override { return "DepthWritePass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.WriteTextureAttachment(m_Name,
                                                                    MakeDepthDesc("DumpSceneDepth"),
                                                                    RGAttachmentKind::DepthStencil,
                                                                    RHI::AttachmentLoadOp::Clear,
                                                                    RHI::AttachmentStoreOp::Store,
                                                                    RHI::ResourceState::DepthWrite,
                                                                    RHI::ResourceState::DepthRead);
            assert(handle.IsValid());
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
    };

    class DepthReadOnlyPass final : public IRenderGraphPass
    {
    public:
        explicit DepthReadOnlyPass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override { return "DepthReadOnlyPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle;
            assert(builder.TryUseAttachment(m_Name,
                                            handle,
                                            RGAttachmentKind::DepthStencil,
                                            RGAttachmentMutability::ReadOnly,
                                            RHI::AttachmentLoadOp::Load,
                                            RHI::AttachmentStoreOp::Store,
                                            RHI::ResourceState::DepthRead,
                                            RHI::ResourceState::DepthRead));
            assert(handle.IsValid());
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
    };

    void ConfigureDump(RenderGraph& graph, bool bDebugMarkers)
    {
        RGDumpOptions options;
        options.bEnabled = true;
        options.bWriteFiles = false;
        options.bText = true;
        options.bDot = true;
        options.bJson = true;
        options.bDebugMarkers = bDebugMarkers;
        options.MaxFrameCount = 1;
        graph.SetDebugDumpOptions(options);
    }

    void TestDumpContainsDeterministicStructuredOutput()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        ConfigureDump(graph, false);

        const Identity sceneColor("Dump.SceneColor");
        const Identity sceneDepth("Dump.SceneDepth");
        ColorWritePass writeColor(sceneColor);
        ColorReadPass readColor(sceneColor);
        ColorMutateAttachmentPass mutateColor(sceneColor);
        DepthWritePass writeDepth(sceneDepth);
        DepthReadOnlyPass readOnlyDepth(sceneDepth);

        graph.AddPass(&writeColor);
        const uint32_t readColorPassIndex = graph.AddPass(&readColor);
        const uint32_t mutateColorPassIndex = graph.AddPass(&mutateColor);
        graph.AddPass(&writeDepth);
        const uint32_t readOnlyDepthPassIndex = graph.AddPass(&readOnlyDepth);

        assert(graph.Compile());

        RGNamedResourceVersionDiagnostic diagnostic;
        assert(graph.TryGetDeclaredPassVersionDiagnostic(0, 0, diagnostic));
        assert(diagnostic.NamedResourceIdentity == sceneColor);
        assert(!diagnostic.BeforeVersionValid);
        assert(diagnostic.BeforeVersion == 0);
        assert(diagnostic.AfterVersion == 0);
        assert(diagnostic.bCreatesNewHead);
        assert(!diagnostic.bMutatesCurrentHead);

        assert(graph.TryGetDeclaredPassVersionDiagnostic(readColorPassIndex, 0, diagnostic));
        assert(diagnostic.BeforeVersionValid);
        assert(diagnostic.BeforeVersion == 0);
        assert(diagnostic.AfterVersion == 0);
        assert(!diagnostic.bCreatesNewHead);
        assert(!diagnostic.bMutatesCurrentHead);

        assert(graph.TryGetDeclaredPassVersionDiagnostic(mutateColorPassIndex, 0, diagnostic));
        assert(diagnostic.BeforeVersionValid);
        assert(diagnostic.BeforeVersion == 0);
        assert(diagnostic.AfterVersion == 1);
        assert(!diagnostic.bCreatesNewHead);
        assert(diagnostic.bMutatesCurrentHead);

        assert(graph.TryGetDeclaredPassVersionDiagnostic(readOnlyDepthPassIndex, 0, diagnostic));
        assert(diagnostic.BeforeVersionValid);
        assert(diagnostic.BeforeVersion == 0);
        assert(diagnostic.AfterVersion == 0);
        assert(!diagnostic.bCreatesNewHead);
        assert(!diagnostic.bMutatesCurrentHead);

        const RGDumpStrings dumpA = graph.BuildDebugDump();
        const RGDumpStrings dumpB = graph.BuildDebugDump();
#if !NORVES_ENABLE_RENDERGRAPH_DUMP
        assert(dumpA.IsEmpty());
        assert(dumpB.IsEmpty());

        const RenderGraphDebugStats stats = graph.GetDebugStats();
        assert(stats.DeclaredPassCount == 5);
        assert(stats.CompiledPassCount == 5);
        assert(stats.BarrierCount > 0);

        std::cout << "TestDumpContainsDeterministicStructuredOutput passed (dump disabled)\n";
        return;
#else
        assert(!dumpA.IsEmpty());
        assert(dumpA.Text == dumpB.Text);
        assert(dumpA.Dot == dumpB.Dot);
        assert(dumpA.Json == dumpB.Json);

        assert(dumpA.Text.find("Pass Order:") != Container::String::npos);
        assert(dumpA.Text.find("Resources:") != Container::String::npos);
        assert(dumpA.Text.find("Producers:") != Container::String::npos);
        assert(dumpA.Text.find("Consumers:") != Container::String::npos);
        assert(dumpA.Text.find("Lifetimes:") != Container::String::npos);
        assert(dumpA.Text.find("Barriers:") != Container::String::npos);
        assert(dumpA.Text.find("BeforeVersionValid=true") != Container::String::npos);
        assert(dumpA.Text.find("bCreatesNewHead=true") != Container::String::npos);
        assert(dumpA.Text.find("bMutatesCurrentHead=true") != Container::String::npos);

        assert(dumpA.Dot.find("digraph RenderGraph") != Container::String::npos);
        assert(dumpA.Dot.find("Barrier 0") != Container::String::npos);
        assert(dumpA.Dot.find("life ") != Container::String::npos);

        assert(dumpA.Json.find("\"BeforeVersionValid\":true") != Container::String::npos);
        assert(dumpA.Json.find("\"AfterVersion\":1") != Container::String::npos);
        assert(dumpA.Json.find("\"bCreatesNewHead\":true") != Container::String::npos);
        assert(dumpA.Json.find("\"bMutatesCurrentHead\":true") != Container::String::npos);
        assert(dumpA.Json.find("\"producers\":[") != Container::String::npos);
        assert(dumpA.Json.find("\"consumers\":[") != Container::String::npos);
        assert(dumpA.Json.find("\"barriers\":[") != Container::String::npos);

        const RenderGraphDebugStats stats = graph.GetDebugStats();
        assert(stats.DeclaredPassCount == 5);
        assert(stats.CompiledPassCount == 5);
        assert(stats.BarrierCount > 0);

        std::cout << "TestDumpContainsDeterministicStructuredOutput passed\n";
#endif
    }

    void TestExecuteTracksAcquireCountsAndDebugMarkers()
    {
        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        ConfigureDump(graph, true);

        const Identity sceneColor("Dump.ExecuteSceneColor");
        const Identity sceneDepth("Dump.ExecuteSceneDepth");
        ColorWritePass writeColor(sceneColor);
        ColorReadPass readColor(sceneColor);
        ColorMutateAttachmentPass mutateColor(sceneColor);
        DepthWritePass writeDepth(sceneDepth);
        DepthReadOnlyPass readOnlyDepth(sceneDepth);

        graph.AddPass(&writeColor);
        graph.AddPass(&readColor);
        graph.AddPass(&mutateColor);
        graph.AddPass(&writeDepth);
        graph.AddPass(&readOnlyDepth);

        assert(graph.Compile());

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;

        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);
        assert(result.ExecutedPassCount == 5);
        assert(graph.GetLastExecutedPassCount() == 5);
        assert(graph.GetLastTransientAcquireCount() == 2);
        assert(graph.GetLastCompiledBarrierCount() > 0);
#if !NORVES_ENABLE_RENDERGRAPH_DUMP
        assert(commandList.BeginDebugMarkerCount == 0);
        assert(commandList.EndDebugMarkerCount == 0);
        assert(commandList.MarkerNames.empty());
        assert(graph.BuildDebugDump().IsEmpty());

        const RenderGraphDebugStats stats = graph.GetDebugStats();
        assert(stats.ExecutedPassCount == 5);
        assert(stats.TransientAcquireCount == 2);

        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();

        std::cout << "TestExecuteTracksAcquireCountsAndDebugMarkers passed (dump disabled)\n";
        return;
#else
        assert(commandList.BeginDebugMarkerCount == 5);
        assert(commandList.EndDebugMarkerCount == 5);
        assert(commandList.MarkerNames.size() == 5);
        assert(commandList.MarkerNames[0] == "ColorWritePass");
        assert(commandList.MarkerNames[4] == "DepthReadOnlyPass");

        const RenderGraphDebugStats stats = graph.GetDebugStats();
        assert(stats.ExecutedPassCount == 5);
        assert(stats.TransientAcquireCount == 2);

        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();

        std::cout << "TestExecuteTracksAcquireCountsAndDebugMarkers passed\n";
#endif
    }
} // namespace

int main()
{
    std::cout << "RenderGraphDumpTest start\n";

    TestDumpContainsDeterministicStructuredOutput();
    TestExecuteTracksAcquireCountsAndDebugMarkers();

    std::cout << "RenderGraphDumpTest passed\n";
    return 0;
}
