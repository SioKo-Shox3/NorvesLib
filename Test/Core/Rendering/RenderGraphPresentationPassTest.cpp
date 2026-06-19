#include "Rendering/PresentationPass.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    struct BarrierEvent
    {
        RHI::ITexture* Texture = nullptr;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
    };

    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
        }

        explicit FakeTexture(const char* name)
            : m_Desc(RHI::TextureDesc::RenderTarget(64, 32, RHI::Format::R8G8B8A8_UNORM, name))
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

    class FakeAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc& desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            RHI::BufferPtr buffer = RHI::MakeShared<FakeBuffer>(desc);
            Buffers.push_back(buffer);

            RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Size = desc.Size;
            allocation.Type = type;
            return allocation;
        }

        void FreeBuffer(RHI::BufferAllocation& allocation) override
        {
            allocation.Buffer = nullptr;
            allocation.Size = 0;
        }

        RHI::TextureAllocation AllocateTexture(const RHI::TextureDesc& desc,
                                               RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            RHI::TexturePtr texture = RHI::MakeShared<FakeTexture>(desc);
            Textures.push_back(texture);

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<size_t>(desc.Width) * static_cast<size_t>(desc.Height) * 4;
            allocation.Type = type;
            return allocation;
        }

        void FreeTexture(RHI::TextureAllocation& allocation) override
        {
            allocation.Texture = nullptr;
            allocation.Size = 0;
        }

        size_t GetAllocatedMemory() const override { return 0; }
        size_t GetUsedMemory() const override { return 0; }
        void Trim() override {}

        Container::VariableArray<RHI::TexturePtr> Textures;
        Container::VariableArray<RHI::BufferPtr> Buffers;
    };

    class FakeRenderPass final : public RHI::IRenderPass
    {
    public:
        uint32_t GetColorAttachmentCount() const override { return 1; }
        bool HasDepthStencilAttachment() const override { return false; }
        RHI::Format GetColorAttachmentFormat(uint32_t index) const override
        {
            return index == 0 ? RHI::Format::R8G8B8A8_UNORM : RHI::Format::UNKNOWN;
        }
        RHI::Format GetDepthStencilFormat() const override { return RHI::Format::UNKNOWN; }
    };

    class FakeFramebuffer final : public RHI::IFramebuffer
    {
    public:
        explicit FakeFramebuffer(RHI::RenderPassPtr renderPass)
            : m_RenderPass(renderPass)
        {
        }

        uint32_t GetWidth() const override { return 64; }
        uint32_t GetHeight() const override { return 32; }
        RHI::RenderPassPtr GetRenderPass() const override { return m_RenderPass; }
        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            (void)index;
            return nullptr;
        }
        RHI::TexturePtr GetDepthStencilAttachment() const override { return nullptr; }
        uint32_t GetColorAttachmentCount() const override { return 1; }
        bool HasDepthStencilAttachment() const override { return false; }

    private:
        RHI::RenderPassPtr m_RenderPass;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        RHI::PipelineType GetPipelineType() const override { return RHI::PipelineType::Graphics; }
        uint32_t GetBindPointCount() const override { return 1; }
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        RHI::FilterMode GetFilterMin() const override { return RHI::FilterMode::Linear; }
        RHI::FilterMode GetFilterMag() const override { return RHI::FilterMode::Linear; }
        RHI::FilterMode GetFilterMip() const override { return RHI::FilterMode::Linear; }
        RHI::TextureAddressMode GetAddressModeU() const override { return RHI::TextureAddressMode::Clamp; }
        RHI::TextureAddressMode GetAddressModeV() const override { return RHI::TextureAddressMode::Clamp; }
        RHI::TextureAddressMode GetAddressModeW() const override { return RHI::TextureAddressMode::Clamp; }
        uint32_t GetMaxAnisotropy() const override { return 1; }
        RHI::CompareFunc GetCompareFunc() const override { return RHI::CompareFunc::Never; }
    };

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        void BindConstantBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t offset, uint32_t size) override
        {
            (void)binding;
            (void)buffer;
            (void)offset;
            (void)size;
        }

        void BindTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            if (binding == 0)
            {
                BoundTexture = texture;
            }
            ++BindTextureCount;
        }

        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            if (binding == 0)
            {
                BoundSampler = sampler;
            }
            ++BindSamplerCount;
        }

        void BindStorageBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t offset, uint32_t size) override
        {
            (void)binding;
            (void)buffer;
            (void)offset;
            (void)size;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            (void)binding;
            (void)texture;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture, uint32_t mipLevel) override
        {
            (void)binding;
            (void)texture;
            (void)mipLevel;
        }

        void Update() override { ++UpdateCount; }

        RHI::TexturePtr BoundTexture;
        RHI::SamplerPtr BoundSampler;
        uint32_t BindTextureCount = 0;
        uint32_t BindSamplerCount = 0;
        uint32_t UpdateCount = 0;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
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
        void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) override
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
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;

            BarrierEvent event;
            event.Texture = texture.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            Barriers.push_back(event);
        }

        Container::VariableArray<BarrierEvent> Barriers;
    };

    class PublishedTexturePass final : public IRenderGraphPass
    {
    public:
        explicit PublishedTexturePass(NorvesLib::Core::Identity name)
            : m_Name(name)
        {
        }

        const char* GetName() const override
        {
            return "PublishedTexturePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            m_Handle = builder.WriteTexture(m_Name,
                                            RGTextureDesc::RenderTarget(64,
                                                                        32,
                                                                        RHI::Format::R8G8B8A8_UNORM,
                                                                        "PublishedPresentationInput"),
                                            RHI::ResourceState::RenderTarget,
                                            RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        RGTextureHandle GetHandle() const
        {
            return m_Handle;
        }

    private:
        NorvesLib::Core::Identity m_Name;
        RGTextureHandle m_Handle;
    };

    struct PresentationFixture
    {
        RHI::TexturePtr BackBuffer = RHI::MakeShared<FakeTexture>("BackBuffer");
        RHI::RenderPassPtr ClearRenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::RenderPassPtr LoadRenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::FramebufferPtr ClearFramebuffer = RHI::MakeShared<FakeFramebuffer>(ClearRenderPass);
        RHI::FramebufferPtr LoadFramebuffer = RHI::MakeShared<FakeFramebuffer>(LoadRenderPass);
        RHI::PipelinePtr BlitPipeline = RHI::MakeShared<FakePipeline>();
        RHI::DescriptorSetPtr BlitDescriptorSet = RHI::MakeShared<FakeDescriptorSet>();
        RHI::SamplerPtr BlitSampler = RHI::MakeShared<FakeSampler>();
        FakeAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderGraph Graph;
        FakeCommandList CommandList;
        Container::VariableArray<FrameCommand> PendingCommands;
        ViewRenderContext Context;

        PresentationFixture()
        {
            assert(Pool.Initialize(&Allocator, 1));
            Pool.BeginFrame(0);
            assert(Graph.Initialize(&Pool));
            Graph.BeginFrame(0);

            Context.CommandList = &CommandList;
            Context.TransientPool = &Pool;
            Context.PendingFrameCommands = &PendingCommands;
            Context.RenderWidth = 64;
            Context.RenderHeight = 32;
            Context.ScreenWidth = 64;
            Context.ScreenHeight = 32;
        }

        ~PresentationFixture()
        {
            Graph.Shutdown();
            Pool.EndFrame();
            Pool.Shutdown();
        }

        PresentationPassRequest MakeRequest(bool bClearPresentation = true) const
        {
            PresentationPassRequest request;
            request.BackBufferTexture = BackBuffer;
            request.ClearRenderPass = ClearRenderPass;
            request.LoadRenderPass = LoadRenderPass;
            request.ClearFramebuffer = ClearFramebuffer;
            request.LoadFramebuffer = LoadFramebuffer;
            request.BlitPipeline = BlitPipeline;
            request.BlitDescriptorSet = BlitDescriptorSet;
            request.BlitSampler = BlitSampler;
            request.bClearPresentation = bClearPresentation;
            return request;
        }

        FakeDescriptorSet* GetDescriptorSet() const
        {
            return static_cast<FakeDescriptorSet*>(BlitDescriptorSet.get());
        }
    };

    bool FindBackBufferBarrier(const PresentationFixture& fixture,
                               RHI::ResourceState beforeState,
                               RHI::ResourceState afterState)
    {
        for (const BarrierEvent& barrier : fixture.CommandList.Barriers)
        {
            if (barrier.Texture == fixture.BackBuffer.get() &&
                barrier.BeforeState == beforeState &&
                barrier.AfterState == afterState)
            {
                return true;
            }
        }

        return false;
    }

    bool HasBackBufferRuntimeBarrier(const PresentationFixture& fixture)
    {
        for (const BarrierEvent& barrier : fixture.CommandList.Barriers)
        {
            if (barrier.Texture == fixture.BackBuffer.get())
            {
                return true;
            }
        }

        return false;
    }

    bool HasBackBufferCompiledBarrier(const RenderGraph& graph)
    {
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.ResourceDebugName &&
                std::strcmp(barrier.ResourceDebugName, "SwapChainBackBuffer") == 0)
            {
                return true;
            }
        }

        return false;
    }

    void AssertOnlyInputReadDeclaration(RenderGraph& graph, uint32_t passIndex)
    {
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Write;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        bool bAttachment = true;
        RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::Clear;
        RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::Store;
        RGAttachmentKind attachmentKind = RGAttachmentKind::Color;
        RGAttachmentMutability mutability = RGAttachmentMutability::Write;

        assert(graph.TryGetDeclaredPassAccess(passIndex,
                                              0,
                                              resource,
                                              mode,
                                              state,
                                              finalState,
                                              &bAttachment,
                                              &loadOp,
                                              &storeOp,
                                              &attachmentKind,
                                              &mutability));
        assert(resource.IsValid());
        assert(mode == RGAccessMode::Read);
        assert(state == RHI::ResourceState::ShaderResource);
        assert(finalState == RHI::ResourceState::ShaderResource);
        assert(!bAttachment);
    }

    void AssertPresentationAttachmentDeclaration(RenderGraph& graph,
                                                 uint32_t passIndex,
                                                 RHI::AttachmentLoadOp expectedLoadOp)
    {
        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        bool bAttachment = false;
        RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
        RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
        RGAttachmentKind attachmentKind = RGAttachmentKind::DepthStencil;
        RGAttachmentMutability mutability = RGAttachmentMutability::ReadOnly;

        assert(graph.TryGetDeclaredPassAccess(passIndex,
                                              1,
                                              resource,
                                              mode,
                                              state,
                                              finalState,
                                              &bAttachment,
                                              &loadOp,
                                              &storeOp,
                                              &attachmentKind,
                                              &mutability));
        assert(resource.IsValid());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::RenderTarget);
        assert(finalState == RHI::ResourceState::Present);
        assert(bAttachment);
        assert(loadOp == expectedLoadOp);
        assert(storeOp == RHI::AttachmentStoreOp::Store);
        assert(attachmentKind == RGAttachmentKind::Color);
        assert(mutability == RGAttachmentMutability::Write);
    }

    void TestPresentationColorPreferredOverToneMappedColor()
    {
        PresentationFixture fixture;
        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PublishedTexturePass presentationPassProducer(RenderGraphResourceNames::PresentationColor);
        PresentationPass presentationPass;
        presentationPass.SetRequest(fixture.MakeRequest());

        fixture.Graph.AddPass(&toneMappedPass);
        fixture.Graph.AddPass(&presentationPassProducer);
        fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.Execute(fixture.Context));

        RenderGraphResources resources(&fixture.Graph);
        RHI::TexturePtr expectedTexture = resources.GetTexture(presentationPassProducer.GetHandle());
        assert(expectedTexture);
        assert(presentationPass.WasPresented());
        assert(presentationPass.GetLastResult().InputName == RenderGraphResourceNames::PresentationColor);
        assert(presentationPass.GetLastResult().InputTexture.get() == expectedTexture.get());
        assert(fixture.GetDescriptorSet()->BoundTexture.get() == expectedTexture.get());
        assert(fixture.PendingCommands.size() == 1);
        std::cout << "TestPresentationColorPreferredOverToneMappedColor passed\n";
    }

    void TestToneMappedColorUsedWhenPresentationColorMissing()
    {
        PresentationFixture fixture;
        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PresentationPass presentationPass;
        presentationPass.SetRequest(fixture.MakeRequest());

        fixture.Graph.AddPass(&toneMappedPass);
        fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.Execute(fixture.Context));

        RenderGraphResources resources(&fixture.Graph);
        RHI::TexturePtr expectedTexture = resources.GetTexture(toneMappedPass.GetHandle());
        assert(expectedTexture);
        assert(presentationPass.WasPresented());
        assert(presentationPass.GetLastResult().InputName == RenderGraphResourceNames::ToneMappedColor);
        assert(presentationPass.GetLastResult().InputTexture.get() == expectedTexture.get());
        assert(fixture.GetDescriptorSet()->BoundTexture.get() == expectedTexture.get());
        std::cout << "TestToneMappedColorUsedWhenPresentationColorMissing passed\n";
    }

    void TestImportedBackBufferUsesUndefinedToRenderTargetAndFinalPresent()
    {
        PresentationFixture fixture;
        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PresentationPass presentationPass;
        presentationPass.SetRequest(fixture.MakeRequest(true));

        fixture.Graph.AddPass(&toneMappedPass);
        const uint32_t presentationPassIndex = fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        AssertPresentationAttachmentDeclaration(fixture.Graph,
                                                presentationPassIndex,
                                                RHI::AttachmentLoadOp::Clear);

        assert(fixture.Graph.Execute(fixture.Context));
        assert(FindBackBufferBarrier(fixture,
                                     RHI::ResourceState::Undefined,
                                     RHI::ResourceState::RenderTarget));
        assert(presentationPass.WasPresented());
        assert(presentationPass.GetLastResult().BackBufferTexture.get() == fixture.BackBuffer.get());
        assert(presentationPass.GetLastResult().LoadOp == RHI::AttachmentLoadOp::Clear);
        std::cout << "TestImportedBackBufferUsesUndefinedToRenderTargetAndFinalPresent passed\n";
    }

    void TestLoadPresentationUsesPresentInitialStateAndLoadResources()
    {
        PresentationFixture fixture;
        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PresentationPass presentationPass;
        presentationPass.SetRequest(fixture.MakeRequest(false));

        fixture.Graph.AddPass(&toneMappedPass);
        const uint32_t presentationPassIndex = fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        AssertPresentationAttachmentDeclaration(fixture.Graph,
                                                presentationPassIndex,
                                                RHI::AttachmentLoadOp::Load);

        assert(fixture.Graph.Execute(fixture.Context));
        assert(FindBackBufferBarrier(fixture,
                                     RHI::ResourceState::Present,
                                     RHI::ResourceState::RenderTarget));
        assert(presentationPass.WasPresented());
        assert(presentationPass.GetLastResult().RenderPass.get() == fixture.LoadRenderPass.get());
        assert(presentationPass.GetLastResult().Framebuffer.get() == fixture.LoadFramebuffer.get());
        assert(presentationPass.GetLastResult().LoadOp == RHI::AttachmentLoadOp::Load);
        std::cout << "TestLoadPresentationUsesPresentInitialStateAndLoadResources passed\n";
    }

    void TestActiveOutputViewportAndScissorUsed()
    {
        PresentationFixture fixture;
        ViewportRenderPlan activeViewport;
        activeViewport.RenderWidth = 48;
        activeViewport.RenderHeight = 24;
        activeViewport.PixelRect.X = 11.0f;
        activeViewport.PixelRect.Y = 22.0f;
        activeViewport.PixelRect.Width = 101.0f;
        activeViewport.PixelRect.Height = 51.0f;
        activeViewport.PixelRect.MinDepth = 0.25f;
        activeViewport.PixelRect.MaxDepth = 0.75f;
        activeViewport.Scissor.Left = 12;
        activeViewport.Scissor.Top = 23;
        activeViewport.Scissor.Right = 113;
        activeViewport.Scissor.Bottom = 74;
        fixture.Context.CurrentViewport = &activeViewport;

        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PresentationPass presentationPass;
        presentationPass.SetRequest(fixture.MakeRequest());

        fixture.Graph.AddPass(&toneMappedPass);
        fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.Execute(fixture.Context));

        assert(presentationPass.WasPresented());
        assert(fixture.PendingCommands.size() == 1);
        const FullscreenPassCommand& command = fixture.PendingCommands[0].FullscreenPass;
        assert(command.Viewport.x == 11.0f);
        assert(command.Viewport.y == 22.0f);
        assert(command.Viewport.width == 101.0f);
        assert(command.Viewport.height == 51.0f);
        assert(command.Viewport.minDepth == 0.25f);
        assert(command.Viewport.maxDepth == 0.75f);
        assert(command.Scissor.left == 12);
        assert(command.Scissor.top == 23);
        assert(command.Scissor.right == 113);
        assert(command.Scissor.bottom == 74);
        assert(presentationPass.GetLastResult().Viewport.width == 101.0f);
        assert(presentationPass.GetLastResult().Scissor.right == 113);
        std::cout << "TestActiveOutputViewportAndScissorUsed passed\n";
    }

    void TestMissingInputDoesNotPresentOrEnqueueFullscreen()
    {
        PresentationFixture fixture;
        PresentationPass presentationPass;
        presentationPass.SetRequest(fixture.MakeRequest());

        const uint32_t presentationPassIndex = fixture.Graph.AddPass(&presentationPass);
        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.GetDeclaredPassAccessCount(presentationPassIndex) == 0);
        assert(fixture.Graph.GetCompiledBarriers().empty());
        assert(fixture.Graph.Execute(fixture.Context));

        assert(!presentationPass.WasPresented());
        assert(!presentationPass.GetLastResult().bPresented);
        assert(fixture.PendingCommands.empty());
        assert(!HasBackBufferRuntimeBarrier(fixture));
        assert(fixture.GetDescriptorSet()->BindTextureCount == 0);
        assert(fixture.GetDescriptorSet()->BindSamplerCount == 0);
        assert(fixture.GetDescriptorSet()->UpdateCount == 0);
        std::cout << "TestMissingInputDoesNotPresentOrEnqueueFullscreen passed\n";
    }

    void TestMissingBlitPipelineDoesNotDeclareBackBufferWhenInputExists()
    {
        PresentationFixture fixture;
        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PresentationPass presentationPass;
        PresentationPassRequest request = fixture.MakeRequest(true);
        request.BlitPipeline.reset();
        presentationPass.SetRequest(request);

        fixture.Graph.AddPass(&toneMappedPass);
        const uint32_t presentationPassIndex = fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        AssertOnlyInputReadDeclaration(fixture.Graph, presentationPassIndex);
        assert(!HasBackBufferCompiledBarrier(fixture.Graph));

        assert(fixture.Graph.Execute(fixture.Context));
        assert(!presentationPass.WasPresented());
        assert(fixture.PendingCommands.empty());
        assert(!HasBackBufferRuntimeBarrier(fixture));
        assert(fixture.GetDescriptorSet()->BindTextureCount == 0);
        assert(fixture.GetDescriptorSet()->BindSamplerCount == 0);
        assert(fixture.GetDescriptorSet()->UpdateCount == 0);
        std::cout << "TestMissingBlitPipelineDoesNotDeclareBackBufferWhenInputExists passed\n";
    }

    void TestMissingSelectedLoadFramebufferDoesNotDeclareBackBufferWhenInputExists()
    {
        PresentationFixture fixture;
        PublishedTexturePass toneMappedPass(RenderGraphResourceNames::ToneMappedColor);
        PresentationPass presentationPass;
        PresentationPassRequest request = fixture.MakeRequest(false);
        request.LoadFramebuffer.reset();
        presentationPass.SetRequest(request);

        fixture.Graph.AddPass(&toneMappedPass);
        const uint32_t presentationPassIndex = fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        AssertOnlyInputReadDeclaration(fixture.Graph, presentationPassIndex);
        assert(!HasBackBufferCompiledBarrier(fixture.Graph));

        assert(fixture.Graph.Execute(fixture.Context));
        assert(!presentationPass.WasPresented());
        assert(fixture.PendingCommands.empty());
        assert(!HasBackBufferRuntimeBarrier(fixture));
        assert(fixture.GetDescriptorSet()->BindTextureCount == 0);
        assert(fixture.GetDescriptorSet()->BindSamplerCount == 0);
        assert(fixture.GetDescriptorSet()->UpdateCount == 0);
        std::cout << "TestMissingSelectedLoadFramebufferDoesNotDeclareBackBufferWhenInputExists passed\n";
    }
} // namespace

int main()
{
    std::cout << "RenderGraphPresentationPassTest start\n";

    TestPresentationColorPreferredOverToneMappedColor();
    TestToneMappedColorUsedWhenPresentationColorMissing();
    TestImportedBackBufferUsesUndefinedToRenderTargetAndFinalPresent();
    TestLoadPresentationUsesPresentInitialStateAndLoadResources();
    TestActiveOutputViewportAndScissorUsed();
    TestMissingInputDoesNotPresentOrEnqueueFullscreen();
    TestMissingBlitPipelineDoesNotDeclareBackBufferWhenInputExists();
    TestMissingSelectedLoadFramebufferDoesNotDeclareBackBufferWhenInputExists();

    std::cout << "RenderGraphPresentationPassTest passed\n";
    return 0;
}
