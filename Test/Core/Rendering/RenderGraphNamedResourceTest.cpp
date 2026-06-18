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
        uint32_t TextureBarrierCount = 0;
        uint32_t BufferBarrierCount = 0;

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
            ++BufferBarrierCount;
        }

        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            assert(texture);
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
            ++TextureBarrierCount;
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
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
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
            auto texture = Container::MakeUnique<FakeTexture>(desc);

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(desc.Height) * 4u;
            allocation.Type = type;

            m_Textures.push_back(std::move(texture));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
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
            return m_AllocatedMemory;
        }

        void Trim() override
        {
        }

    private:
        Container::VariableArray<Container::TUniquePtr<FakeTexture>> m_Textures;
        Container::VariableArray<Container::TUniquePtr<FakeBuffer>> m_Buffers;
        size_t m_AllocatedMemory = 0;
    };

    RGTextureDesc MakeTextureDesc(const char* name)
    {
        return RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, name);
    }

    RGBufferDesc MakeBufferDesc(const char* name)
    {
        RGBufferDesc desc;
        desc.Size = 256;
        desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
        desc.DebugName = name;
        return desc;
    }

    class PublishReadTexturePass final : public IRenderGraphPass
    {
    public:
        PublishReadTexturePass(Identity name, RGTextureHandle* published, RGTextureHandle* read)
            : m_Name(name), m_Published(published), m_Read(read)
        {
        }

        const char* GetName() const override
        {
            return "PublishReadTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle texture = builder.CreateTextureHandle(MakeTextureDesc("PublishedTexture"));
            assert(texture.IsValid());
            assert(builder.PublishTexture(m_Name, texture));

            RGTextureHandle found;
            assert(builder.TryGetTexture(m_Name, found));
            assert(found == texture);

            RGTextureHandle read = builder.ReadTexture(m_Name);
            assert(read == texture);

            if (m_Published)
            {
                *m_Published = texture;
            }

            if (m_Read)
            {
                *m_Read = read;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        RGTextureHandle* m_Published = nullptr;
        RGTextureHandle* m_Read = nullptr;
    };

    class DuplicatePublishPass final : public IRenderGraphPass
    {
    public:
        explicit DuplicatePublishPass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "DuplicatePublishPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle first = builder.CreateTextureHandle(MakeTextureDesc("FirstPublish"));
            RGTextureHandle second = builder.CreateTextureHandle(MakeTextureDesc("SecondPublish"));
            assert(builder.PublishTexture(m_Name, first));
            assert(!builder.PublishTexture(m_Name, second));
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }

    private:
        Identity m_Name;
    };

    class WriteTexturePass final : public IRenderGraphPass
    {
    public:
        WriteTexturePass(Identity name, const char* debugName, RGTextureHandle* written)
            : m_Name(name), m_DebugName(debugName), m_Written(written)
        {
        }

        const char* GetName() const override
        {
            return "WriteTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle written = builder.WriteTexture(m_Name,
                                                           MakeTextureDesc(m_DebugName),
                                                           RHI::ResourceState::RenderTarget,
                                                           RHI::ResourceState::RenderTarget);
            assert(written.IsValid());
            if (m_Written)
            {
                *m_Written = written;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        const char* m_DebugName = nullptr;
        RGTextureHandle* m_Written = nullptr;
    };

    class SamePassReadWriteTexturePass final : public IRenderGraphPass
    {
    public:
        SamePassReadWriteTexturePass(Identity name, RGTextureHandle* written)
            : m_Name(name), m_Written(written)
        {
        }

        const char* GetName() const override
        {
            return "SamePassReadWriteTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle written = builder.WriteTexture(m_Name,
                                                           MakeTextureDesc("SamePassReadWrite"),
                                                           RHI::ResourceState::RenderTarget,
                                                           RHI::ResourceState::ShaderResource);
            assert(written.IsValid());
            builder.Read(written.ToResourceHandle(), RHI::ResourceState::ShaderResource);
            if (m_Written)
            {
                *m_Written = written;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }

    private:
        Identity m_Name;
        RGTextureHandle* m_Written = nullptr;
    };

    class LoadStoreColorAttachmentPass final : public IRenderGraphPass
    {
    public:
        LoadStoreColorAttachmentPass(Identity name,
                                     RGTextureHandle* loadStored,
                                     bool bExpectFound)
            : m_Name(name), m_LoadStored(loadStored), m_bExpectFound(bExpectFound)
        {
        }

        const char* GetName() const override
        {
            return "LoadStoreColorAttachmentPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle loadStored;
            const bool bFound = builder.TryLoadStoreColorAttachment(m_Name,
                                                                    loadStored,
                                                                    RHI::AttachmentLoadOp::Load,
                                                                    RHI::AttachmentStoreOp::Store,
                                                                    RHI::ResourceState::RenderTarget,
                                                                    RHI::ResourceState::ShaderResource);
            assert(bFound == m_bExpectFound);
            assert(loadStored.IsValid() == m_bExpectFound);
            if (m_LoadStored)
            {
                *m_LoadStored = loadStored;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        RGTextureHandle* m_LoadStored = nullptr;
        bool m_bExpectFound = false;
    };

    class LoadStoreThenReadColorAttachmentPass final : public IRenderGraphPass
    {
    public:
        explicit LoadStoreThenReadColorAttachmentPass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "LoadStoreThenReadColorAttachmentPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle loadStored;
            assert(builder.TryLoadStoreColorAttachment(m_Name,
                                                       loadStored,
                                                       RHI::AttachmentLoadOp::Load,
                                                       RHI::AttachmentStoreOp::Store,
                                                       RHI::ResourceState::RenderTarget,
                                                       RHI::ResourceState::ShaderResource));
            builder.Read(loadStored.ToResourceHandle(), RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }

    private:
        Identity m_Name;
    };

    class ReadTexturePass final : public IRenderGraphPass
    {
    public:
        ReadTexturePass(Identity name, RGTextureHandle* read, bool bExpectValid)
            : m_Name(name), m_Read(read), m_bExpectValid(bExpectValid)
        {
        }

        const char* GetName() const override
        {
            return "ReadTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle read = builder.ReadTexture(m_Name);
            assert(read.IsValid() == m_bExpectValid);
            if (m_Read)
            {
                *m_Read = read;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        RGTextureHandle* m_Read = nullptr;
        bool m_bExpectValid = false;
    };

    class TryReadTexturePass final : public IRenderGraphPass
    {
    public:
        TryReadTexturePass(Identity name, RGTextureHandle* read, bool bExpectFound)
            : m_Name(name), m_Read(read), m_bExpectFound(bExpectFound)
        {
        }

        const char* GetName() const override
        {
            return "TryReadTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle read;
            const bool bFound = builder.TryReadTexture(m_Name, read);
            assert(bFound == m_bExpectFound);
            assert(read.IsValid() == m_bExpectFound);
            if (m_Read)
            {
                *m_Read = read;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        RGTextureHandle* m_Read = nullptr;
        bool m_bExpectFound = false;
    };

    class WriteBufferPass final : public IRenderGraphPass
    {
    public:
        WriteBufferPass(Identity name, const char* debugName, RGBufferHandle* written)
            : m_Name(name), m_DebugName(debugName), m_Written(written)
        {
        }

        const char* GetName() const override
        {
            return "WriteBufferPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGBufferHandle written = builder.WriteBuffer(m_Name,
                                                         MakeBufferDesc(m_DebugName),
                                                         RHI::ResourceState::UnorderedAccess,
                                                         RHI::ResourceState::UnorderedAccess);
            assert(written.IsValid());
            if (m_Written)
            {
                *m_Written = written;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        const char* m_DebugName = nullptr;
        RGBufferHandle* m_Written = nullptr;
    };

    class ReadBufferPass final : public IRenderGraphPass
    {
    public:
        ReadBufferPass(Identity name, RGBufferHandle* read, bool bExpectValid)
            : m_Name(name), m_Read(read), m_bExpectValid(bExpectValid)
        {
        }

        const char* GetName() const override
        {
            return "ReadBufferPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGBufferHandle read = builder.ReadBuffer(m_Name);
            assert(read.IsValid() == m_bExpectValid);
            if (m_Read)
            {
                *m_Read = read;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_Name;
        RGBufferHandle* m_Read = nullptr;
        bool m_bExpectValid = false;
    };

    class TypeMismatchPass final : public IRenderGraphPass
    {
    public:
        explicit TypeMismatchPass(Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "TypeMismatchPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle texture = builder.CreateTextureHandle(MakeTextureDesc("MismatchTexture"));
            assert(builder.PublishTexture(m_Name, texture));

            RGBufferHandle buffer = builder.ReadBuffer(m_Name);
            assert(!buffer.IsValid());
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }

    private:
        Identity m_Name;
    };

    class TryGetWrongTypePass final : public IRenderGraphPass
    {
    public:
        TryGetWrongTypePass(Identity textureName, Identity bufferName)
            : m_TextureName(textureName), m_BufferName(bufferName)
        {
        }

        const char* GetName() const override
        {
            return "TryGetWrongTypePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle texture = builder.CreateTextureHandle(MakeTextureDesc("TryGetTexture"));
            assert(builder.PublishTexture(m_TextureName, texture));

            RGBufferHandle buffer = builder.CreateBufferHandle(MakeBufferDesc("TryGetBuffer"));
            assert(builder.PublishBuffer(m_BufferName, buffer));

            RGTextureHandle textureOut = texture;
            assert(textureOut.IsValid());
            assert(!builder.TryGetTexture(m_BufferName, textureOut));
            assert(!textureOut.IsValid());

            RGBufferHandle bufferOut = buffer;
            assert(bufferOut.IsValid());
            assert(!builder.TryGetBuffer(m_TextureName, bufferOut));
            assert(!bufferOut.IsValid());
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_TextureName;
        Identity m_BufferName;
    };

    class ExportCreateTexturePass final : public IRenderGraphPass
    {
    public:
        ExportCreateTexturePass(Identity name, RGTextureHandle* exported, uint32_t* executeCount)
            : m_Name(name), m_Exported(exported), m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override
        {
            return "ExportCreateTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle texture = builder.CreateTextureHandle(MakeTextureDesc("ExportCreateTexture"));
            assert(builder.ExportTexture(m_Name, texture));
            if (m_Exported)
            {
                *m_Exported = texture;
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
        RGTextureHandle* m_Exported = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class ExportWriteTexturePass final : public IRenderGraphPass
    {
    public:
        ExportWriteTexturePass(Identity name, RGTextureHandle* exported, uint32_t* executeCount)
            : m_Name(name), m_Exported(exported), m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override
        {
            return "ExportWriteTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle texture = builder.WriteTexture(m_Name,
                                                           MakeTextureDesc("ExportWriteTexture"),
                                                           RHI::ResourceState::RenderTarget,
                                                           RHI::ResourceState::RenderTarget);
            assert(texture.IsValid());
            assert(builder.ExportTexture(m_Name, texture));
            if (m_Exported)
            {
                *m_Exported = texture;
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
        RGTextureHandle* m_Exported = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class EmptyPass final : public IRenderGraphPass
    {
    public:
        explicit EmptyPass(uint32_t* executeCount)
            : m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override
        {
            return "EmptyPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            (void)builder;
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

    void TestPublishReadTexture()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle published;
        RGTextureHandle read;
        PublishReadTexturePass pass(Identity("Named.SceneColor"), &published, &read);
        graph.AddPass(&pass);

        assert(graph.Compile());
        assert(published.IsValid());
        assert(read == published);
        assert(graph.GetDeclaredPassAccessCount(0) == 1);

        std::cout << "TestPublishReadTexture passed\n";
    }

    void TestDuplicatePublishFails()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        DuplicatePublishPass pass(Identity("Named.Duplicate"));
        graph.AddPass(&pass);

        assert(!graph.Compile());

        std::cout << "TestDuplicatePublishFails passed\n";
    }

    void TestWriteAdvancesCurrentHead()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle first;
        RGTextureHandle second;
        RGTextureHandle read;
        WriteTexturePass writeA(Identity("Named.History"), "HistoryA", &first);
        WriteTexturePass writeB(Identity("Named.History"), "HistoryB", &second);
        ReadTexturePass readPass(Identity("Named.History"), &read, true);
        graph.AddPass(&writeA);
        graph.AddPass(&writeB);
        graph.AddPass(&readPass);

        assert(graph.Compile());
        assert(first.IsValid());
        assert(second.IsValid());
        assert(first != second);
        assert(read == second);

        std::cout << "TestWriteAdvancesCurrentHead passed\n";
    }

    void TestSamePassReadWriteFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle written;
        SamePassReadWriteTexturePass pass(Identity("Named.SamePassReadWrite"), &written);
        graph.AddPass(&pass);

        assert(!graph.Compile());
        assert(written.IsValid());

        std::cout << "TestSamePassReadWriteFailsCompile passed\n";
    }

    void TestLoadStoreColorAttachmentKeepsCurrentHead()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        const Identity name("Named.LoadStoreSceneColor");
        RGTextureHandle first;
        RGTextureHandle loadStored;
        RGTextureHandle read;
        WriteTexturePass writePass(name, "LoadStoreSceneColor", &first);
        LoadStoreColorAttachmentPass loadStorePass(name, &loadStored, true);
        TryReadTexturePass readPass(name, &read, true);
        graph.AddPass(&writePass);
        const uint32_t loadStorePassIndex = graph.AddPass(&loadStorePass);
        graph.AddPass(&readPass);

        assert(graph.Compile());
        assert(first.IsValid());
        assert(loadStored == first);
        assert(read == first);
        assert(graph.GetDeclaredPassAccessCount(loadStorePassIndex) == 1);

        uint32_t version = 0;
        assert(graph.TryGetNamedResourceVersion(name, version));
        assert(version == 1);

        std::cout << "TestLoadStoreColorAttachmentKeepsCurrentHead passed\n";
    }

    void TestLoadStoreWithReadFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        const Identity name("Named.LoadStoreMixedRead");
        RGTextureHandle first;
        WriteTexturePass writePass(name, "LoadStoreMixedRead", &first);
        LoadStoreThenReadColorAttachmentPass mixedPass(name);
        graph.AddPass(&writePass);
        graph.AddPass(&mixedPass);

        assert(!graph.Compile());
        assert(first.IsValid());

        std::cout << "TestLoadStoreWithReadFailsCompile passed\n";
    }

    void TestTryLoadStoreMissingDoesNotFailCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle loadStored;
        LoadStoreColorAttachmentPass pass(Identity("Named.LoadStoreMissing"), &loadStored, false);
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile());
        assert(!loadStored.IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 0);

        uint32_t version = 0;
        assert(!graph.TryGetNamedResourceVersion(Identity("Named.LoadStoreMissing"), version));

        std::cout << "TestTryLoadStoreMissingDoesNotFailCompile passed\n";
    }

    void TestWriteBufferAdvancesCurrentHead()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGBufferHandle first;
        RGBufferHandle second;
        RGBufferHandle read;
        WriteBufferPass writeA(Identity("Named.BufferHistory"), "BufferHistoryA", &first);
        WriteBufferPass writeB(Identity("Named.BufferHistory"), "BufferHistoryB", &second);
        ReadBufferPass readPass(Identity("Named.BufferHistory"), &read, true);
        graph.AddPass(&writeA);
        graph.AddPass(&writeB);
        graph.AddPass(&readPass);

        assert(graph.Compile());
        assert(first.IsValid());
        assert(second.IsValid());
        assert(first != second);
        assert(read == second);

        std::cout << "TestWriteBufferAdvancesCurrentHead passed\n";
    }

    void TestTryReadTextureAdvancesCurrentHead()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle first;
        RGTextureHandle second;
        RGTextureHandle read;
        WriteTexturePass writeA(Identity("Named.TryReadHistory"), "TryReadHistoryA", &first);
        WriteTexturePass writeB(Identity("Named.TryReadHistory"), "TryReadHistoryB", &second);
        TryReadTexturePass readPass(Identity("Named.TryReadHistory"), &read, true);
        graph.AddPass(&writeA);
        graph.AddPass(&writeB);
        const uint32_t readPassIndex = graph.AddPass(&readPass);

        assert(graph.Compile());
        assert(first.IsValid());
        assert(second.IsValid());
        assert(first != second);
        assert(read == second);
        assert(graph.GetDeclaredPassAccessCount(readPassIndex) == 1);

        std::cout << "TestTryReadTextureAdvancesCurrentHead passed\n";
    }

    void TestTryReadTextureMissingDoesNotFailCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle read;
        TryReadTexturePass pass(Identity("Named.TryReadMissing"), &read, false);
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile());
        assert(!read.IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 0);

        std::cout << "TestTryReadTextureMissingDoesNotFailCompile passed\n";
    }

    void TestUnregisteredReadFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle read;
        ReadTexturePass pass(Identity("Named.Missing"), &read, false);
        graph.AddPass(&pass);

        assert(!graph.Compile());
        assert(!read.IsValid());

        std::cout << "TestUnregisteredReadFailsCompile passed\n";
    }

    void TestTypeMismatchFails()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        TypeMismatchPass pass(Identity("Named.TypeMismatch"));
        graph.AddPass(&pass);

        assert(!graph.Compile());

        std::cout << "TestTypeMismatchFails passed\n";
    }

    void TestTypedHandleValidationRejectsWrongKind()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        TryGetWrongTypePass pass(Identity("Named.TypedMismatchTexture"),
                                 Identity("Named.TypedMismatchBuffer"));
        graph.AddPass(&pass);

        assert(!graph.Compile());

        std::cout << "TestTypedHandleValidationRejectsWrongKind passed\n";
    }

    void TestTryReadTextureTypeMismatchFails()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGBufferHandle buffer;
        RGTextureHandle read;
        WriteBufferPass writeBuffer(Identity("Named.TryReadWrongType"), "TryReadWrongType", &buffer);
        TryReadTexturePass readPass(Identity("Named.TryReadWrongType"), &read, false);
        graph.AddPass(&writeBuffer);
        graph.AddPass(&readPass);

        assert(!graph.Compile());
        assert(buffer.IsValid());
        assert(!read.IsValid());

        std::cout << "TestTryReadTextureTypeMismatchFails passed\n";
    }

    void TestInvalidIdentityFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle read;
        ReadTexturePass pass(Identity{}, &read, false);
        graph.AddPass(&pass);

        assert(!graph.Compile());
        assert(!read.IsValid());

        std::cout << "TestInvalidIdentityFailsCompile passed\n";
    }

    void TestTryReadTextureInvalidIdentityFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle read;
        TryReadTexturePass pass(Identity{}, &read, false);
        graph.AddPass(&pass);

        assert(!graph.Compile());
        assert(!read.IsValid());

        std::cout << "TestTryReadTextureInvalidIdentityFailsCompile passed\n";
    }

    void TestResetInvalidatesTypedHandles()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGTextureHandle published;
        RGTextureHandle read;
        PublishReadTexturePass pass(Identity("Named.Reset"), &published, &read);
        graph.AddPass(&pass);
        assert(graph.Compile());

        graph.Reset();

        RenderGraphResources resources(&graph);
        assert(resources.GetTexture(published) == nullptr);
        assert(resources.GetTextureRaw(read) == nullptr);

        std::cout << "TestResetInvalidatesTypedHandles passed\n";
    }

    void TestExportPopulatesExecutionResult()
    {
        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));
        pool.BeginFrame(1);

        RenderGraph graph;
        assert(graph.Initialize(&pool));

        const Identity outputName("Named.Output");
        RGTextureHandle exported;
        uint32_t executeCount = 0;
        ExportCreateTexturePass pass(outputName, &exported, &executeCount);
        graph.AddPass(&pass);

        assert(graph.Compile());

        ViewRenderContext context;
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);
        assert(result.ExecutedPassCount == 1);
        assert(executeCount == 1);
        assert(result.TextureOutputs.size() == 1);

        RHI::TexturePtr texture;
        assert(result.TryGetTexture(outputName, texture));
        assert(texture != nullptr);
        assert(graph.TryGetLastOutputTexture(outputName, texture));
        assert(texture != nullptr);
        assert(graph.GetLastExecutionResult().TextureOutputs.find(outputName)->second.Handle == exported);

        std::cout << "TestExportPopulatesExecutionResult passed\n";
    }

    void TestExecuteWithResultFailureClearsOutputs()
    {
        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));
        pool.BeginFrame(1);

        RenderGraph graph;
        assert(graph.Initialize(&pool));

        const Identity outputName("Named.FailureClears");
        RGTextureHandle exported;
        uint32_t executeCount = 0;
        ExportWriteTexturePass pass(outputName, &exported, &executeCount);
        graph.AddPass(&pass);
        assert(graph.Compile());

        FakeCommandList commandList;
        ViewRenderContext successContext;
        successContext.CommandList = &commandList;
        RenderGraphExecutionResult success = graph.ExecuteWithResult(successContext);
        assert(success.bSuccess);
        assert(success.TextureOutputs.size() == 1);
        assert(executeCount == 1);
        assert(commandList.TextureBarrierCount == 1);

        ViewRenderContext failureContext;
        RenderGraphExecutionResult failure = graph.ExecuteWithResult(failureContext);
        assert(!failure.bSuccess);
        assert(failure.ExecutedPassCount == 0);
        assert(failure.TextureOutputs.empty());
        assert(graph.GetLastExecutionResult().TextureOutputs.empty());

        RHI::TexturePtr stale;
        assert(!graph.TryGetLastOutputTexture(outputName, stale));
        assert(stale == nullptr);

        std::cout << "TestExecuteWithResultFailureClearsOutputs passed\n";
    }

    void TestBoolExecuteCompatibility()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass pass(&executeCount);
        graph.AddPass(&pass);
        assert(graph.Compile());

        ViewRenderContext context;
        assert(graph.Execute(context));
        assert(executeCount == 1);
        assert(graph.GetLastExecutionResult().bSuccess);
        assert(graph.GetLastExecutionResult().ExecutedPassCount == 1);

        std::cout << "TestBoolExecuteCompatibility passed\n";
    }
} // namespace

int main()
{
    TestPublishReadTexture();
    TestDuplicatePublishFails();
    TestWriteAdvancesCurrentHead();
    TestSamePassReadWriteFailsCompile();
    TestLoadStoreColorAttachmentKeepsCurrentHead();
    TestLoadStoreWithReadFailsCompile();
    TestTryLoadStoreMissingDoesNotFailCompile();
    TestWriteBufferAdvancesCurrentHead();
    TestTryReadTextureAdvancesCurrentHead();
    TestTryReadTextureMissingDoesNotFailCompile();
    TestUnregisteredReadFailsCompile();
    TestTypeMismatchFails();
    TestTypedHandleValidationRejectsWrongKind();
    TestTryReadTextureTypeMismatchFails();
    TestInvalidIdentityFailsCompile();
    TestTryReadTextureInvalidIdentityFailsCompile();
    TestResetInvalidatesTypedHandles();
    TestExportPopulatesExecutionResult();
    TestExecuteWithResultFailureClearsOutputs();
    TestBoolExecuteCompatibility();

    std::cout << "RenderGraphNamedResourceTest passed\n";
    return 0;
}
