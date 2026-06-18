#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include "Container/PointerTypes.h"
#include <cassert>
#include <iostream>
#include <utility>

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Identity;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    class FakeTexture final : public RHI::ITexture
    {
    public:
        FakeTexture()
        {
            m_Desc.Width = 32;
            m_Desc.Height = 16;
            m_Desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            m_Desc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
        }

        explicit FakeTexture(const RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override
        {
            return m_Desc.Width;
        }

        uint32_t GetHeight() const override
        {
            return m_Desc.Height;
        }

        uint32_t GetDepth() const override
        {
            return m_Desc.Depth;
        }

        uint32_t GetMipLevels() const override
        {
            return m_Desc.MipLevels;
        }

        uint32_t GetArraySize() const override
        {
            return m_Desc.ArraySize;
        }

        RHI::Format GetFormat() const override
        {
            return m_Desc.TextureFormat;
        }

        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }

        bool IsCubemap() const override
        {
            return m_Desc.IsCubemap;
        }

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

        uint64_t GetSize() const override
        {
            return m_Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)offset;
            (void)size;
            return nullptr;
        }

        void Unmap() override
        {
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            (void)data;
            (void)size;
            (void)offset;
        }

        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }

    private:
        RHI::BufferDesc m_Desc;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        void Begin() override
        {
        }

        void End() override
        {
        }

        void Submit(bool waitForCompletion = false) override
        {
            (void)waitForCompletion;
        }

        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            (void)renderPass;
            (void)framebuffer;
        }

        void EndRenderPass() override
        {
        }

        void SetViewport(const RHI::Viewport& viewport) override
        {
            (void)viewport;
        }

        void SetScissor(const RHI::ScissorRect& scissor) override
        {
            (void)scissor;
        }

        void SetPipeline(RHI::PipelinePtr pipeline) override
        {
            (void)pipeline;
        }

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

        void GenerateMipmaps(RHI::TexturePtr texture) override
        {
            (void)texture;
        }

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
    };

    class MockAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        bool bFailTextures = false;
        bool bFailBuffers = false;

        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc& desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            if (bFailBuffers)
            {
                return {};
            }

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
            if (bFailTextures)
            {
                return {};
            }

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

        size_t GetAllocatedMemory() const override
        {
            return 0;
        }

        size_t GetUsedMemory() const override
        {
            return 0;
        }

        void Trim() override
        {
        }

    private:
        Container::VariableArray<Container::TUniquePtr<FakeTexture>> m_Textures;
        Container::VariableArray<Container::TUniquePtr<FakeBuffer>> m_Buffers;
    };

    RGTextureDesc MakeTextureDesc(const char* debugName)
    {
        return RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, debugName);
    }

    class WriteTexturePass final : public IRenderGraphPass
    {
    public:
        explicit WriteTexturePass(RGTextureHandle* outHandle, const char* debugName = "WriteTexture")
            : m_OutHandle(outHandle), m_DebugName(debugName)
        {
        }

        const char* GetName() const override
        {
            return "WriteTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.CreateTextureHandle(MakeTextureDesc(m_DebugName));
            assert(handle.IsValid());
            builder.Write(handle.ToResourceHandle(),
                          RHI::ResourceState::RenderTarget,
                          RHI::ResourceState::ShaderResource);
            if (m_OutHandle)
            {
                *m_OutHandle = handle;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGTextureHandle* m_OutHandle = nullptr;
        const char* m_DebugName = nullptr;
    };

    class ReadTexturePass final : public IRenderGraphPass
    {
    public:
        explicit ReadTexturePass(const RGTextureHandle* inHandle)
            : m_InHandle(inHandle)
        {
        }

        const char* GetName() const override
        {
            return "ReadTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            assert(m_InHandle && m_InHandle->IsValid());
            builder.Read(m_InHandle->ToResourceHandle(), RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        const RGTextureHandle* m_InHandle = nullptr;
    };

    class ImportedReadPass final : public IRenderGraphPass
    {
    public:
        ImportedReadPass(RHI::TexturePtr texture, RGResourceHandle* outHandle)
            : m_Texture(texture), m_OutHandle(outHandle)
        {
        }

        const char* GetName() const override
        {
            return "ImportedReadPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle handle = builder.ImportTexture(m_Texture,
                                                            RHI::ResourceState::ShaderResource,
                                                            "ImportedRead");
            assert(handle.IsValid());
            builder.Read(handle, RHI::ResourceState::ShaderResource);
            if (m_OutHandle)
            {
                *m_OutHandle = handle;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RHI::TexturePtr m_Texture;
        RGResourceHandle* m_OutHandle = nullptr;
    };

    class LogicalWritePass final : public IRenderGraphPass
    {
    public:
        explicit LogicalWritePass(RGResourceHandle* outHandle)
            : m_OutHandle(outHandle)
        {
        }

        const char* GetName() const override
        {
            return "LogicalWritePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle handle = builder.CreateLogical("LogicalLifetime");
            assert(handle.IsValid());
            builder.Write(handle, RHI::ResourceState::Common, RHI::ResourceState::Common);
            if (m_OutHandle)
            {
                *m_OutHandle = handle;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_OutHandle = nullptr;
    };

    class LogicalReadPass final : public IRenderGraphPass
    {
    public:
        explicit LogicalReadPass(const RGResourceHandle* inHandle)
            : m_InHandle(inHandle)
        {
        }

        const char* GetName() const override
        {
            return "LogicalReadPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            assert(m_InHandle && m_InHandle->IsValid());
            builder.Read(*m_InHandle, RHI::ResourceState::Common);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        const RGResourceHandle* m_InHandle = nullptr;
    };

    class TransientReadBeforeWritePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override
        {
            return "TransientReadBeforeWritePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.CreateTextureHandle(MakeTextureDesc("ReadBeforeWrite"));
            assert(handle.IsValid());
            builder.Read(handle.ToResourceHandle(), RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }
    };

    class PublishOnlyTexturePass final : public IRenderGraphPass
    {
    public:
        explicit PublishOnlyTexturePass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "PublishOnlyTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.CreateTextureHandle(MakeTextureDesc("PublishedLoadStore"));
            assert(handle.IsValid());
            assert(builder.PublishTexture(m_Name, handle));
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
    };

    class NamedWriteTexturePass final : public IRenderGraphPass
    {
    public:
        explicit NamedWriteTexturePass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "NamedWriteTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.WriteTexture(m_Name,
                                                          MakeTextureDesc("NamedWrite"),
                                                          RHI::ResourceState::RenderTarget,
                                                          RHI::ResourceState::ShaderResource);
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

    class NamedLoadStorePass final : public IRenderGraphPass
    {
    public:
        explicit NamedLoadStorePass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "NamedLoadStorePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle;
            assert(builder.TryLoadStoreColorAttachment(m_Name,
                                                       handle,
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

    class ImportedLoadStorePass final : public IRenderGraphPass
    {
    public:
        explicit ImportedLoadStorePass(RHI::TexturePtr texture)
            : m_Texture(texture)
        {
        }

        const char* GetName() const override
        {
            return "ImportedLoadStorePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle handle = builder.ImportTexture(m_Texture,
                                                            RHI::ResourceState::RenderTarget,
                                                            "ImportedLoadStore");
            assert(handle.IsValid());
            assert(builder.LoadStoreColorAttachment(handle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource));
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RHI::TexturePtr m_Texture;
    };

    class UnusedResourcePass final : public IRenderGraphPass
    {
    public:
        explicit UnusedResourcePass(RGTextureHandle* outHandle)
            : m_OutHandle(outHandle)
        {
        }

        const char* GetName() const override
        {
            return "UnusedResourcePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.CreateTextureHandle(MakeTextureDesc("UnusedTransient"));
            assert(handle.IsValid());
            if (m_OutHandle)
            {
                *m_OutHandle = handle;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGTextureHandle* m_OutHandle = nullptr;
    };

    class ExportOnlyTexturePass final : public IRenderGraphPass
    {
    public:
        ExportOnlyTexturePass(Identity name, RGTextureHandle* outHandle, uint32_t* executeCount = nullptr)
            : m_Name(name), m_OutHandle(outHandle), m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override
        {
            return "ExportOnlyTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.CreateTextureHandle(MakeTextureDesc("ExportOnly"));
            assert(handle.IsValid());
            assert(builder.ExportTexture(m_Name, handle));
            if (m_OutHandle)
            {
                *m_OutHandle = handle;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_ExecuteCount)
            {
                ++(*m_ExecuteCount);
            }
        }

    private:
        Identity m_Name;
        RGTextureHandle* m_OutHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class NoBarrierFirstUsePass final : public IRenderGraphPass
    {
    public:
        explicit NoBarrierFirstUsePass(uint32_t* executeCount)
            : m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override
        {
            return "NoBarrierFirstUsePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle handle = builder.CreateTextureHandle(MakeTextureDesc("NoBarrierFirstUse"));
            assert(handle.IsValid());
            builder.Write(handle.ToResourceHandle(),
                          RHI::ResourceState::Undefined,
                          RHI::ResourceState::Undefined);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_ExecuteCount)
            {
                ++(*m_ExecuteCount);
            }
        }

    private:
        uint32_t* m_ExecuteCount = nullptr;
    };

    void AssertCompileProductsEmpty(const RenderGraph& graph)
    {
        assert(graph.GetCompiledPassOrder().empty());
        assert(graph.GetCompiledBarriers().empty());
        assert(graph.GetCompiledResourceLifetimes().empty());
        assert(graph.GetTransientAllocationPlan().empty());
    }

    void TestTransientWriteReadLifetimeAndAllocation()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle handle;
        WriteTexturePass writePass(&handle, "TransientWriteRead");
        ReadTexturePass readPass(&handle);
        const uint32_t writePassIndex = graph.AddPass(&writePass);
        const uint32_t readPassIndex = graph.AddPass(&readPass);

        assert(graph.Compile());

        RGCompiledResourceLifetime lifetime;
        assert(graph.TryGetCompiledResourceLifetime(handle.ToResourceHandle(), lifetime));
        assert(lifetime.bHasUse);
        assert(lifetime.FirstUsePassIndex == writePassIndex);
        assert(lifetime.LastUsePassIndex == readPassIndex);
        assert(lifetime.FirstUseOrderIndex == 0);
        assert(lifetime.LastUseOrderIndex == 1);
        assert(lifetime.LifetimeEndOrderIndex == 1);

        const auto& plan = graph.GetTransientAllocationPlan();
        assert(plan.size() == 1);
        assert(plan[0].Resource == handle.ToResourceHandle());
        assert(plan[0].AcquireBeforePassIndex == writePassIndex);
        assert(plan[0].AcquireBeforeOrderIndex == 0);

        std::cout << "TestTransientWriteReadLifetimeAndAllocation passed\n";
    }

    void TestImportedFirstReadHasNoAllocationPlan()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RHI::TexturePtr texture = RHI::MakeShared<FakeTexture>();
        RGResourceHandle handle;
        ImportedReadPass pass(texture, &handle);
        graph.AddPass(&pass);

        assert(graph.Compile());

        RGCompiledResourceLifetime lifetime;
        assert(graph.TryGetCompiledResourceLifetime(handle, lifetime));
        assert(lifetime.Lifetime == RGResourceLifetime::Imported);
        assert(lifetime.bHasUse);
        assert(graph.GetTransientAllocationPlan().empty());

        std::cout << "TestImportedFirstReadHasNoAllocationPlan passed\n";
    }

    void TestLogicalLifetimeHasNoAllocationOrBarrier()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle handle;
        LogicalWritePass writePass(&handle);
        LogicalReadPass readPass(&handle);
        graph.AddPass(&writePass);
        graph.AddPass(&readPass);

        assert(graph.Compile());

        RGCompiledResourceLifetime lifetime;
        assert(graph.TryGetCompiledResourceLifetime(handle, lifetime));
        assert(lifetime.Kind == RGResourceKind::Logical);
        assert(lifetime.Lifetime == RGResourceLifetime::Logical);
        assert(lifetime.bHasUse);
        assert(graph.GetTransientAllocationPlan().empty());
        assert(graph.GetCompiledBarriers().empty());

        std::cout << "TestLogicalLifetimeHasNoAllocationOrBarrier passed\n";
    }

    void TestTransientReadBeforeWriteFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        TransientReadBeforeWritePass pass;
        graph.AddPass(&pass);

        assert(!graph.Compile());
        AssertCompileProductsEmpty(graph);

        std::cout << "TestTransientReadBeforeWriteFailsCompile passed\n";
    }

    void TestLoadStoreFirstUseLoadFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        const Identity name("Lifetime.LoadStoreFirstUse");
        PublishOnlyTexturePass publishPass(name);
        NamedLoadStorePass loadStorePass(name);
        graph.AddPass(&publishPass);
        graph.AddPass(&loadStorePass);

        assert(!graph.Compile());
        AssertCompileProductsEmpty(graph);

        std::cout << "TestLoadStoreFirstUseLoadFailsCompile passed\n";
    }

    void TestPriorWriteThenLoadStoreLoadCompiles()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        const Identity name("Lifetime.LoadStoreAfterWrite");
        NamedWriteTexturePass writePass(name);
        NamedLoadStorePass loadStorePass(name);
        graph.AddPass(&writePass);
        graph.AddPass(&loadStorePass);

        assert(graph.Compile());

        std::cout << "TestPriorWriteThenLoadStoreLoadCompiles passed\n";
    }

    void TestImportedLoadStoreLoadCompiles()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RHI::TexturePtr texture = RHI::MakeShared<FakeTexture>();
        ImportedLoadStorePass pass(texture);
        graph.AddPass(&pass);

        assert(graph.Compile());

        std::cout << "TestImportedLoadStoreLoadCompiles passed\n";
    }

    void TestUnusedNonExportResourceCompilesWithSentinelLifetime()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle handle;
        UnusedResourcePass pass(&handle);
        graph.AddPass(&pass);

        assert(graph.Compile());

        RGCompiledResourceLifetime lifetime;
        assert(graph.TryGetCompiledResourceLifetime(handle.ToResourceHandle(), lifetime));
        assert(!lifetime.bHasUse);
        assert(!lifetime.bExported);
        assert(lifetime.FirstUsePassIndex == RGInvalidPassIndex);
        assert(lifetime.LastUsePassIndex == RGInvalidPassIndex);
        assert(lifetime.FirstUseOrderIndex == RGInvalidPassIndex);
        assert(lifetime.LastUseOrderIndex == RGInvalidPassIndex);
        assert(lifetime.LifetimeEndOrderIndex == RGInvalidPassIndex);

        std::cout << "TestUnusedNonExportResourceCompilesWithSentinelLifetime passed\n";
    }

    void TestExportOnlyTransientPinsWithoutAllocationPlan()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        const Identity name("Lifetime.ExportOnly");
        RGTextureHandle handle;
        ExportOnlyTexturePass pass(name, &handle);
        graph.AddPass(&pass);

        assert(graph.Compile());

        RGCompiledResourceLifetime lifetime;
        assert(graph.TryGetCompiledResourceLifetime(handle.ToResourceHandle(), lifetime));
        assert(!lifetime.bHasUse);
        assert(lifetime.bExported);
        assert(lifetime.bPinnedUntilGraphEnd);
        assert(lifetime.LifetimeEndOrderIndex == graph.GetCompiledPassOrder().size());
        assert(graph.GetTransientAllocationPlan().empty());

        std::cout << "TestExportOnlyTransientPinsWithoutAllocationPlan passed\n";
    }

    void TestCompileFailureClearsCompiledProducts()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        TransientReadBeforeWritePass pass;
        graph.AddPass(&pass);

        assert(!graph.Compile());
        AssertCompileProductsEmpty(graph);

        std::cout << "TestCompileFailureClearsCompiledProducts passed\n";
    }

    void TestNoBarrierFirstUseAcquireFailureStopsBeforeExecute()
    {
        MockAllocator allocator;
        allocator.bFailTextures = true;

        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));

        uint32_t executeCount = 0;
        NoBarrierFirstUsePass pass(&executeCount);
        graph.AddPass(&pass);
        assert(graph.Compile());
        assert(graph.GetCompiledBarriers().empty());

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;

        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(!result.bSuccess);
        assert(result.ExecutedPassCount == 0);
        assert(executeCount == 0);

        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();

        std::cout << "TestNoBarrierFirstUseAcquireFailureStopsBeforeExecute passed\n";
    }

    void TestExportCollectionAcquireFailureFailsResult()
    {
        MockAllocator allocator;
        allocator.bFailTextures = true;

        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));

        const Identity name("Lifetime.ExportAcquireFailure");
        RGTextureHandle handle;
        uint32_t executeCount = 0;
        ExportOnlyTexturePass pass(name, &handle, &executeCount);
        graph.AddPass(&pass);
        assert(graph.Compile());

        ViewRenderContext context;
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(!result.bSuccess);
        assert(result.ExecutedPassCount == 1);
        assert(executeCount == 1);
        assert(result.TextureOutputs.empty());

        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();

        std::cout << "TestExportCollectionAcquireFailureFailsResult passed\n";
    }
} // namespace

int main()
{
    std::cout << "RenderGraphLifetimeTest start\n";

    TestTransientWriteReadLifetimeAndAllocation();
    TestImportedFirstReadHasNoAllocationPlan();
    TestLogicalLifetimeHasNoAllocationOrBarrier();
    TestTransientReadBeforeWriteFailsCompile();
    TestLoadStoreFirstUseLoadFailsCompile();
    TestPriorWriteThenLoadStoreLoadCompiles();
    TestImportedLoadStoreLoadCompiles();
    TestUnusedNonExportResourceCompilesWithSentinelLifetime();
    TestExportOnlyTransientPinsWithoutAllocationPlan();
    TestCompileFailureClearsCompiledProducts();
    TestNoBarrierFirstUseAcquireFailureStopsBeforeExecute();
    TestExportCollectionAcquireFailureFailsResult();

    std::cout << "RenderGraphLifetimeTest passed\n";
    return 0;
}
