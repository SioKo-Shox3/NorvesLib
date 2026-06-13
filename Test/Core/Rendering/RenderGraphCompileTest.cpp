#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/ViewRenderContext.h"
#include "Container/PointerTypes.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include <cassert>
#include <iostream>
#include <utility>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    struct BarrierEvent
    {
        RGBarrierKind Kind = RGBarrierKind::Texture;
        RHI::ITexture* Texture = nullptr;
        RHI::IBuffer* Buffer = nullptr;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
        uint64_t BufferSize = 0;
    };

    class FakeTexture final : public RHI::ITexture
    {
    public:
        uint32_t GetWidth() const override { return 64; }
        uint32_t GetHeight() const override { return 32; }
        uint32_t GetDepth() const override { return 1; }
        uint32_t GetMipLevels() const override { return 1; }
        uint32_t GetArraySize() const override { return 1; }
        RHI::Format GetFormat() const override { return RHI::Format::R8G8B8A8_UNORM; }
        RHI::ResourceUsage GetUsage() const override
        {
            return RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
        }
        bool IsCubemap() const override { return false; }
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
    };

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        uint64_t GetSize() const override { return 256; }
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
        RHI::ResourceUsage GetUsage() const override
        {
            return RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::ShaderRead;
        }
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        Container::VariableArray<BarrierEvent> Barriers;

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
            (void)offset;
            BarrierEvent event;
            event.Kind = RGBarrierKind::Buffer;
            event.Buffer = buffer.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            event.BufferSize = size;
            Barriers.push_back(event);
        }
        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
            BarrierEvent event;
            event.Kind = RGBarrierKind::Texture;
            event.Texture = texture.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            Barriers.push_back(event);
        }
    };

    class MockAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc& desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto buffer = Container::MakeUnique<FakeBuffer>();

            RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Offset = 0;
            allocation.Size = desc.Size;
            allocation.Type = type;

            m_Buffers.push_back(std::move(buffer));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            ++m_BufferAllocationCount;
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
                    m_AllocatedMemory -= static_cast<size_t>(allocation.Size);
                    m_UsedMemory -= static_cast<size_t>(allocation.Size);
                    m_Buffers.erase(it);
                    allocation = {};
                    return;
                }
            }

            assert(false);
        }

        RHI::TextureAllocation AllocateTexture(const RHI::TextureDesc& desc,
                                               RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto texture = Container::MakeUnique<FakeTexture>();

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(desc.Height) * 4u;
            allocation.Type = type;

            m_Textures.push_back(std::move(texture));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            ++m_TextureAllocationCount;
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
                    m_AllocatedMemory -= static_cast<size_t>(allocation.Size);
                    m_UsedMemory -= static_cast<size_t>(allocation.Size);
                    m_Textures.erase(it);
                    allocation = {};
                    return;
                }
            }

            assert(false);
        }

        size_t GetAllocatedMemory() const override
        {
            return m_AllocatedMemory;
        }

        size_t GetUsedMemory() const override
        {
            return m_UsedMemory;
        }

        void Trim() override
        {
        }

        size_t GetLiveAllocationCount() const
        {
            return m_Textures.size() + m_Buffers.size();
        }

        uint32_t GetTextureAllocationCount() const
        {
            return m_TextureAllocationCount;
        }

        uint32_t GetBufferAllocationCount() const
        {
            return m_BufferAllocationCount;
        }

    private:
        Container::VariableArray<Container::TUniquePtr<FakeTexture>> m_Textures;
        Container::VariableArray<Container::TUniquePtr<FakeBuffer>> m_Buffers;
        size_t m_AllocatedMemory = 0;
        size_t m_UsedMemory = 0;
        uint32_t m_TextureAllocationCount = 0;
        uint32_t m_BufferAllocationCount = 0;
    };

    class EmptyPass final : public IRenderGraphPass
    {
    public:
        EmptyPass(const char* name, uint32_t* executeCount)
            : m_Name(name), m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return m_Name; }
        void Declare(RenderGraphBuilder& builder) override { (void)builder; }
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
        const char* m_Name = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class LogicalPass final : public IRenderGraphPass
    {
    public:
        explicit LogicalPass(uint32_t id, Container::VariableArray<uint32_t>* executed)
            : m_Id(id), m_Executed(executed)
        {
        }

        const char* GetName() const override { return "LogicalPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            if (m_CreateTarget)
            {
                *m_CreateTarget = builder.CreateLogical("LogicalPass.Resource");
            }

            if (m_ReadA)
            {
                builder.Read(*m_ReadA);
            }

            if (m_ReadB)
            {
                builder.Read(*m_ReadB);
            }

            if (m_CreateTarget)
            {
                builder.Write(*m_CreateTarget);
            }

            if (m_WriteExisting)
            {
                builder.Write(*m_WriteExisting);
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_Executed)
            {
                m_Executed->push_back(m_Id);
            }
        }

        RGResourceHandle* m_CreateTarget = nullptr;
        RGResourceHandle* m_ReadA = nullptr;
        RGResourceHandle* m_ReadB = nullptr;
        RGResourceHandle* m_WriteExisting = nullptr;

    private:
        uint32_t m_Id = 0;
        Container::VariableArray<uint32_t>* m_Executed = nullptr;
    };

    class ImportedProducerPass final : public IRenderGraphPass
    {
    public:
        ImportedProducerPass(RHI::TexturePtr texture,
                             RHI::BufferPtr buffer,
                             RGResourceHandle* textureHandle,
                             RGResourceHandle* bufferHandle,
                             uint32_t* executeCount)
            : m_Texture(texture),
              m_Buffer(buffer),
              m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "ImportedProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.ImportTexture(m_Texture, RHI::ResourceState::Undefined, "ImportedTexture");
            builder.Write(*m_TextureHandle, RHI::ResourceState::RenderTarget);

            *m_BufferHandle = builder.ImportBuffer(m_Buffer, RHI::ResourceState::Common, "ImportedBuffer");
            builder.Write(*m_BufferHandle, RHI::ResourceState::CopyDest);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            assert(resources.GetTextureRaw(*m_TextureHandle) == m_Texture.get());
            assert(resources.GetBufferRaw(*m_BufferHandle) == m_Buffer.get());
            ++(*m_ExecuteCount);
        }

    private:
        RHI::TexturePtr m_Texture;
        RHI::BufferPtr m_Buffer;
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class ImportedConsumerPass final : public IRenderGraphPass
    {
    public:
        ImportedConsumerPass(RHI::TexturePtr texture,
                             RHI::BufferPtr buffer,
                             RGResourceHandle* textureHandle,
                             RGResourceHandle* bufferHandle,
                             uint32_t* executeCount)
            : m_Texture(texture),
              m_Buffer(buffer),
              m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "ImportedConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(*m_TextureHandle, RHI::ResourceState::ShaderResource);
            builder.Read(*m_BufferHandle, RHI::ResourceState::GenericRead);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            assert(resources.GetTextureRaw(*m_TextureHandle) == m_Texture.get());
            assert(resources.GetBufferRaw(*m_BufferHandle) == m_Buffer.get());
            ++(*m_ExecuteCount);
        }

    private:
        RHI::TexturePtr m_Texture;
        RHI::BufferPtr m_Buffer;
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class TransientProducerPass final : public IRenderGraphPass
    {
    public:
        TransientProducerPass(RGResourceHandle* textureHandle,
                              RGResourceHandle* bufferHandle,
                              RHI::ITexture** resolvedTexture,
                              RHI::IBuffer** resolvedBuffer,
                              uint32_t* executeCount)
            : m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ResolvedTexture(resolvedTexture),
              m_ResolvedBuffer(resolvedBuffer),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "TransientProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.CreateTexture(
                RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, "TransientTexture"));
            builder.Write(*m_TextureHandle, RHI::ResourceState::RenderTarget);

            RGBufferDesc bufferDesc;
            bufferDesc.Size = 512;
            bufferDesc.Usage = RHI::ResourceUsage::StorageBuffer;
            bufferDesc.DebugName = "TransientBuffer";
            *m_BufferHandle = builder.CreateBuffer(bufferDesc);
            builder.Write(*m_BufferHandle, RHI::ResourceState::UnorderedAccess);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            *m_ResolvedTexture = resources.GetTextureRaw(*m_TextureHandle);
            *m_ResolvedBuffer = resources.GetBufferRaw(*m_BufferHandle);
            assert(*m_ResolvedTexture);
            assert(*m_ResolvedBuffer);
            ++(*m_ExecuteCount);
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        RHI::ITexture** m_ResolvedTexture = nullptr;
        RHI::IBuffer** m_ResolvedBuffer = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class InvalidHandlePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "InvalidHandlePass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(RGResourceHandle{});
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }
    };

    void AssertOrder(const Container::VariableArray<uint32_t>& order,
                     uint32_t a,
                     uint32_t b,
                     uint32_t c)
    {
        assert(order.size() == 3);
        assert(order[0] == a);
        assert(order[1] == b);
        assert(order[2] == c);
    }

    void TestLinearDependencyOrder()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass passA("A", &executeCount);
        EmptyPass passB("B", &executeCount);
        EmptyPass passC("C", &executeCount);

        const uint32_t indexA = graph.AddPass(&passA);
        const uint32_t indexB = graph.AddPass(&passB);
        const uint32_t indexC = graph.AddPass(&passC);
        assert(graph.AddDependency(indexA, indexB));
        assert(graph.AddDependency(indexB, indexC));

        assert(graph.Compile());
        AssertOrder(graph.GetCompiledPassOrder(), indexA, indexB, indexC);
    }

    void TestDiamondStableOrder()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle root;
        RGResourceHandle left;
        RGResourceHandle right;
        Container::VariableArray<uint32_t> executed;

        LogicalPass rootPass(0, &executed);
        rootPass.m_CreateTarget = &root;
        LogicalPass leftPass(1, &executed);
        leftPass.m_ReadA = &root;
        leftPass.m_CreateTarget = &left;
        LogicalPass rightPass(2, &executed);
        rightPass.m_ReadA = &root;
        rightPass.m_CreateTarget = &right;
        LogicalPass sinkPass(3, &executed);
        sinkPass.m_ReadA = &left;
        sinkPass.m_ReadB = &right;

        graph.AddPass(&rootPass);
        graph.AddPass(&leftPass);
        graph.AddPass(&rightPass);
        graph.AddPass(&sinkPass);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == 0);
        assert(order[1] == 1);
        assert(order[2] == 2);
        assert(order[3] == 3);
    }

    void TestWriteAfterWriteDependency()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle resource;
        Container::VariableArray<uint32_t> executed;

        LogicalPass firstWrite(0, &executed);
        firstWrite.m_CreateTarget = &resource;
        LogicalPass secondWrite(1, &executed);
        secondWrite.m_WriteExisting = &resource;

        graph.AddPass(&firstWrite);
        graph.AddPass(&secondWrite);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    void TestWriteAfterReadDependency()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle resource;
        Container::VariableArray<uint32_t> executed;

        LogicalPass firstRead(0, &executed);
        firstRead.m_CreateTarget = &resource;
        firstRead.m_ReadA = &resource;
        LogicalPass laterWrite(1, &executed);
        laterWrite.m_WriteExisting = &resource;

        graph.AddPass(&firstRead);
        graph.AddPass(&laterWrite);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    void TestImportedResourceBarriers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RHI::TexturePtr texture = RHI::MakeShared<FakeTexture>();
        RHI::BufferPtr buffer = RHI::MakeShared<FakeBuffer>();
        RGResourceHandle textureHandle;
        RGResourceHandle bufferHandle;
        uint32_t executeCount = 0;

        ImportedProducerPass producer(texture, buffer, &textureHandle, &bufferHandle, &executeCount);
        ImportedConsumerPass consumer(texture, buffer, &textureHandle, &bufferHandle, &executeCount);
        graph.AddPass(&producer);
        graph.AddPass(&consumer);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 4);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[0].CompiledOrderIndex == 0);
        assert(barriers[1].Kind == RGBarrierKind::Buffer);
        assert(barriers[1].BeforeState == RHI::ResourceState::Common);
        assert(barriers[1].AfterState == RHI::ResourceState::CopyDest);
        assert(barriers[1].CompiledOrderIndex == 0);
        assert(barriers[1].BufferSize == buffer->GetSize());
        assert(barriers[2].Kind == RGBarrierKind::Texture);
        assert(barriers[2].BeforeState == RHI::ResourceState::RenderTarget);
        assert(barriers[2].AfterState == RHI::ResourceState::ShaderResource);
        assert(barriers[2].CompiledOrderIndex == 1);
        assert(barriers[3].Kind == RGBarrierKind::Buffer);
        assert(barriers[3].BeforeState == RHI::ResourceState::CopyDest);
        assert(barriers[3].AfterState == RHI::ResourceState::GenericRead);
        assert(barriers[3].CompiledOrderIndex == 1);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        assert(graph.Execute(context));
        assert(executeCount == 2);
        assert(graph.GetLastExecutedPassCount() == 2);
        assert(commandList.Barriers.size() == 4);
        assert(commandList.Barriers[0].Texture == texture.get());
        assert(commandList.Barriers[1].Buffer == buffer.get());
        assert(commandList.Barriers[1].BufferSize == buffer->GetSize());
        assert(commandList.Barriers[2].Texture == texture.get());
        assert(commandList.Barriers[3].Buffer == buffer.get());
    }

    void TestTransientResourcesResolveThroughPool()
    {
        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        RGResourceHandle textureHandle;
        RGResourceHandle bufferHandle;
        RHI::ITexture* resolvedTexture = nullptr;
        RHI::IBuffer* resolvedBuffer = nullptr;
        uint32_t executeCount = 0;
        TransientProducerPass pass(&textureHandle,
                                   &bufferHandle,
                                   &resolvedTexture,
                                   &resolvedBuffer,
                                   &executeCount);
        graph.AddPass(&pass);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 2);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[1].Kind == RGBarrierKind::Buffer);
        assert(barriers[1].AfterState == RHI::ResourceState::UnorderedAccess);
        assert(barriers[1].BufferSize == 512);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        assert(graph.Execute(context));

        assert(executeCount == 1);
        assert(resolvedTexture);
        assert(resolvedBuffer);
        assert(allocator.GetTextureAllocationCount() == 1);
        assert(allocator.GetBufferAllocationCount() == 1);
        assert(commandList.Barriers.size() == 2);
        assert(commandList.Barriers[0].Texture == resolvedTexture);
        assert(commandList.Barriers[1].Buffer == resolvedBuffer);
        assert(commandList.Barriers[1].BufferSize == 512);

        pool.EndFrame();
        graph.Shutdown();
        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
    }

    void TestCycleDetectionDoesNotExecute()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass passA("A", &executeCount);
        EmptyPass passB("B", &executeCount);
        const uint32_t indexA = graph.AddPass(&passA);
        const uint32_t indexB = graph.AddPass(&passB);
        assert(graph.AddDependency(indexA, indexB));
        assert(graph.AddDependency(indexB, indexA));

        assert(!graph.Compile());
        ViewRenderContext context;
        assert(!graph.Execute(context));
        assert(executeCount == 0);
        assert(graph.GetLastExecutedPassCount() == 0);
    }

    void TestInvalidHandleRejected()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        InvalidHandlePass pass;
        graph.AddPass(&pass);
        assert(!graph.Compile());
    }
} // namespace

int main()
{
    std::cout << "RenderGraphCompileTest start\n";

    TestLinearDependencyOrder();
    TestDiamondStableOrder();
    TestWriteAfterWriteDependency();
    TestWriteAfterReadDependency();
    TestImportedResourceBarriers();
    TestTransientResourcesResolveThroughPool();
    TestCycleDetectionDoesNotExecute();
    TestInvalidHandleRejected();

    std::cout << "RenderGraphCompileTest passed\n";
    return 0;
}

