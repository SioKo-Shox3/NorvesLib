#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/CanvasView.h"
#include "Rendering/CompositePass.h"
#include "Rendering/FramePacket.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/View.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    ViewportRenderPlan MakeViewportPlan(uint32_t viewId, uint32_t viewportId);
    ViewRenderPlan MakeViewPlan(uint32_t viewId, ViewType type);

    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const char* name)
            : m_Desc(RHI::TextureDesc::RenderTarget(64, 32, RHI::Format::R8G8B8A8_UNORM, name))
        {
        }

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
            (void)binding;
            (void)texture;
        }
        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            (void)binding;
            (void)sampler;
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
        void Update() override {}
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            (void)framebuffer;
            RenderPasses.push_back(renderPass.get());
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
            ++DrawCount;
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
            (void)texture;
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
        }

        bool HasRenderPass(const RHI::IRenderPass* renderPass) const
        {
            for (const RHI::IRenderPass* recordedRenderPass : RenderPasses)
            {
                if (recordedRenderPass == renderPass)
                {
                    return true;
                }
            }

            return false;
        }

        Container::VariableArray<const RHI::IRenderPass*> RenderPasses;
        uint32_t DrawCount = 0;
    };

    class PublishedTextureViewPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "PublishedTextureViewPass"; }
        bool Initialize(ViewRenderContext& context) override
        {
            (void)context;
            m_bInitialized = true;
            return true;
        }
        void Shutdown() override { m_bInitialized = false; }
        void Setup(ViewRenderContext& context) override { (void)context; }
        void Execute(ViewRenderContext& context) override { (void)context; }

        void Declare(RenderGraphBuilder& builder) override
        {
            m_Handle = builder.WriteTexture(RenderGraphResourceNames::ToneMappedColor,
                                            RGTextureDesc::RenderTarget(64,
                                                                        32,
                                                                        RHI::Format::R8G8B8A8_UNORM,
                                                                        "ExecutorToneMappedColor"),
                                            RHI::ResourceState::RenderTarget,
                                            RHI::ResourceState::ShaderResource);
            builder.ExportTexture(RenderGraphResourceNames::ToneMappedColor, m_Handle);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGTextureHandle m_Handle;
    };

    class EmptyGraphViewPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "EmptyGraphViewPass"; }
        bool Initialize(ViewRenderContext& context) override
        {
            (void)context;
            m_bInitialized = true;
            return true;
        }
        void Shutdown() override { m_bInitialized = false; }
        void Setup(ViewRenderContext& context) override { (void)context; }
        void Execute(ViewRenderContext& context) override { (void)context; }
        void Declare(RenderGraphBuilder& builder) override { (void)builder; }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    class DirectOutputCanvasView final : public CanvasView
    {
    public:
        RHI::TexturePtr OutputTexture = RHI::MakeShared<FakeTexture>("DirectCanvasOutput");
        uint32_t RenderCount = 0;

        void Render(ViewRenderContext& context) override
        {
            ++RenderCount;
            ResetFrameOutput();
            context.CurrentGraphExecutionResult = nullptr;
            context.bPresentationGraphPassHandled = false;
            if (IsEnabled() && OutputTexture)
            {
                SetFrameOutputTexture(OutputTexture);
            }
        }
    };

    struct ExecutorPresentationFixture
    {
        RHI::TexturePtr BackBuffer = RHI::MakeShared<FakeTexture>("ExecutorBackBuffer");
        RHI::TexturePtr FallbackTexture = RHI::MakeShared<FakeTexture>("ExecutorFallbackToneMappedColor");
        RHI::RenderPassPtr FallbackClearRenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::RenderPassPtr FallbackLoadRenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::RenderPassPtr GraphClearRenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::RenderPassPtr GraphLoadRenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::FramebufferPtr FallbackClearFramebuffer = RHI::MakeShared<FakeFramebuffer>(FallbackClearRenderPass);
        RHI::FramebufferPtr FallbackLoadFramebuffer = RHI::MakeShared<FakeFramebuffer>(FallbackLoadRenderPass);
        RHI::FramebufferPtr GraphClearFramebuffer = RHI::MakeShared<FakeFramebuffer>(GraphClearRenderPass);
        RHI::FramebufferPtr GraphLoadFramebuffer = RHI::MakeShared<FakeFramebuffer>(GraphLoadRenderPass);
        RHI::PipelinePtr BlitPipeline = RHI::MakeShared<FakePipeline>();
        RHI::DescriptorSetPtr BlitDescriptorSet = RHI::MakeShared<FakeDescriptorSet>();
        RHI::SamplerPtr BlitSampler = RHI::MakeShared<FakeSampler>();
        FakeAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderGraph Graph;
        SharedResourceRegistry SharedResources;
        SceneRenderer Renderer;
        FakeCommandList CommandList;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        ViewRenderContext Context;
        FramePacket Packet;
        Container::VariableArray<Container::TSharedPtr<View>> Views;
        PresentationComposer Composer;
        PresentationPass GraphPresentationPass;
        CompositePass GraphCompositePass;

        ExecutorPresentationFixture()
        {
            assert(Pool.Initialize(&Allocator, 1));
            Pool.BeginFrame(0);
            assert(Graph.Initialize(&Pool));
            Graph.BeginFrame(0);
            SharedResources.RegisterTexturePtr(RenderGraphResourceNames::ToneMappedColor, FallbackTexture);

            Context.Graph = &Graph;
            Context.CommandList = &CommandList;
            Context.TransientPool = &Pool;
            Context.PendingFrameCommands = &PendingFrameCommands;
            Context.SharedResources = &SharedResources;
            Context.RenderWidth = 64;
            Context.RenderHeight = 32;
            Context.ScreenWidth = 64;
            Context.ScreenHeight = 32;

            Packet.Views.push_back(MakeViewPlan(0, ViewType::Scene));
        }

        ~ExecutorPresentationFixture()
        {
            Graph.Shutdown();
            Pool.EndFrame();
            Pool.Shutdown();
        }

        PresentationComposeRequest MakeComposeRequest()
        {
            PresentationComposeRequest request;
            request.ClearRenderPass = FallbackClearRenderPass;
            request.LoadRenderPass = FallbackLoadRenderPass;
            request.ClearFramebuffer = FallbackClearFramebuffer;
            request.LoadFramebuffer = FallbackLoadFramebuffer;
            request.BlitPipeline = BlitPipeline;
            request.BlitDescriptorSet = BlitDescriptorSet;
            request.BlitSampler = BlitSampler;
            return request;
        }

        PresentationPassRequest MakeGraphPresentationRequest()
        {
            PresentationPassRequest request;
            request.BackBufferTexture = BackBuffer;
            request.ClearRenderPass = GraphClearRenderPass;
            request.LoadRenderPass = GraphLoadRenderPass;
            request.ClearFramebuffer = GraphClearFramebuffer;
            request.LoadFramebuffer = GraphLoadFramebuffer;
            request.BlitPipeline = BlitPipeline;
            request.BlitDescriptorSet = BlitDescriptorSet;
            request.BlitSampler = BlitSampler;
            return request;
        }

        RenderFrameExecutionRequest MakeExecutionRequest()
        {
            RenderFrameExecutionRequest request;
            request.Packet = &Packet;
            request.Views = &Views;
            request.FallbackView = Views.empty() ? nullptr : Views[0].get();
            request.Context = &Context;
            request.Renderer = &Renderer;
            request.CommandList = &CommandList;
            request.PendingFrameCommands = &PendingFrameCommands;
            request.Presentation = &Composer;
            request.PresentationRequest = MakeComposeRequest();
            request.PresentationGraphPass = &GraphPresentationPass;
            request.GraphPresentationRequest = MakeGraphPresentationRequest();
            request.CompositeGraphPass = &GraphCompositePass;
            return request;
        }
    };

    ViewportRenderPlan MakeViewportPlan(uint32_t viewId, uint32_t viewportId)
    {
        ViewportRenderPlan plan;
        plan.ViewId = viewId;
        plan.ViewportId = viewportId;
        plan.RenderWidth = 1280;
        plan.RenderHeight = 720;
        plan.PixelRect.Width = 1280.0f;
        plan.PixelRect.Height = 720.0f;
        plan.Scissor.Right = 1280;
        plan.Scissor.Bottom = 720;
        return plan;
    }

    ViewRenderPlan MakeViewPlan(uint32_t viewId, ViewType type)
    {
        ViewRenderPlan plan;
        plan.ViewId = viewId;
        plan.ViewType = static_cast<uint8_t>(type);
        plan.Viewports.push_back(MakeViewportPlan(viewId, viewId + 100));
        return plan;
    }
}

int main()
{
    std::cout.setf(std::ios::unitbuf);
    std::cout << "RenderFrameExecutorPlanTest start\n";

    {
        ViewRenderContext context;
        Container::VariableArray<DrawCommand> frameCommands;
        frameCommands.push_back(DrawCommand::CreateDraw());
        frameCommands.push_back(DrawCommand::CreateDrawIndexed());
        frameCommands.push_back(DrawCommand::CreateDraw());
        context.SnapshotDrawCommandSource = &frameCommands;
        RenderGraphExecutionResult previousResult;
        context.CurrentGraphExecutionResult = &previousResult;

        ViewportRenderPlan viewport = MakeViewportPlan(2, 3);
        viewport.bHasCamera = true;
        viewport.Camera.CameraId = 42;
        viewport.OpaqueCommandRange = {1, 1};
        viewport.TransparentCommandRange = {2, 1};
        viewport.DrawCommandRange = {1, 2};

        RenderFrameExecutor::ApplyViewportRenderPlan(context, &viewport);
        assert(context.CurrentGraphExecutionResult == nullptr);
        assert(context.CurrentViewport == &viewport);
        assert(context.CurrentCamera == &viewport.Camera);
        assert(context.CurrentDrawCommands.Data == frameCommands.data() + 1);
        assert(context.CurrentDrawCommands.Count == 2);
        assert(context.CurrentOpaqueCommands.Data == frameCommands.data() + 1);
        assert(context.CurrentOpaqueCommands.Count == 1);
        assert(context.CurrentTransparentCommands.Data == frameCommands.data() + 2);
        assert(context.CurrentTransparentCommands.Count == 1);

        context.CurrentGraphExecutionResult = &previousResult;
        RenderFrameExecutor::ApplyViewportRenderPlan(context, nullptr);
        assert(context.CurrentGraphExecutionResult == nullptr);
        assert(context.CurrentViewport == nullptr);
        assert(context.CurrentCamera == nullptr);
        assert(context.CurrentDrawCommands.empty());
        assert(context.CurrentOpaqueCommands.empty());
        assert(context.CurrentTransparentCommands.empty());
    }

    {
        FramePacket packet;
        packet.Views.push_back(MakeViewPlan(0, ViewType::Debug));
        packet.Views.push_back(MakeViewPlan(1, ViewType::Scene));

        const ViewportRenderPlan *primary = RenderFrameExecutor::FindPrimaryViewportRenderPlan(packet);
        assert(primary != nullptr);
        assert(primary->ViewId == 1);
    }

    {
        FramePacket packet;
        packet.Views.push_back(MakeViewPlan(0, ViewType::Debug));
        packet.Views.push_back(MakeViewPlan(1, ViewType::Custom));

        const ViewportRenderPlan *primary = RenderFrameExecutor::FindPrimaryViewportRenderPlan(packet);
        assert(primary != nullptr);
        assert(primary->ViewId == 0);
    }

    {
        auto canvas = Container::MakeShared<DirectOutputCanvasView>();
        ViewSettings canvasSettings;
        canvasSettings.Type = ViewType::UI;
        assert(canvas->Initialize(canvasSettings));

        Container::VariableArray<Container::TSharedPtr<View>> views;
        views.push_back(canvas);

        FramePacket packet;
        ViewRenderPlan canvasPlan = MakeViewPlan(0, ViewType::UI);
        canvasPlan.Viewports.clear();
        packet.Views.push_back(canvasPlan);
        assert(RenderFrameExecutor::ShouldCompose(packet, views));

        canvas->SetEnabled(false);
        assert(!RenderFrameExecutor::ShouldCompose(packet, views));

        canvas->SetEnabled(true);
        packet.Views[0].bEnabled = false;
        assert(!RenderFrameExecutor::ShouldCompose(packet, views));

        packet.Views[0].bEnabled = true;
        auto genericUi = Container::MakeShared<View>();
        assert(genericUi->Initialize(canvasSettings));
        views[0] = genericUi;
        assert(!RenderFrameExecutor::ShouldCompose(packet, views));
        std::cout << "TestShouldComposeRequiresEnabledCanvasView passed\n";
    }

    {
        FramePacket packet;
        packet.Views.push_back(MakeViewPlan(0, ViewType::Scene));
        packet.Views.push_back(MakeViewPlan(1, ViewType::Scene));

        const ViewportRenderPlan* primaryScene = RenderFrameExecutor::FindPrimarySceneViewportRenderPlan(packet);
        assert(primaryScene != nullptr);
        assert(primaryScene->ViewId == 0);

        packet.Views[0].bEnabled = false;
        primaryScene = RenderFrameExecutor::FindPrimarySceneViewportRenderPlan(packet);
        assert(primaryScene != nullptr);
        assert(primaryScene->ViewId == 1);

        packet.Views[1].Viewports.clear();
        primaryScene = RenderFrameExecutor::FindPrimarySceneViewportRenderPlan(packet);
        assert(primaryScene == nullptr);
        std::cout << "TestFindPrimarySceneViewportIgnoresDisabledOrEmptySceneViews passed\n";
    }

    {
        View view;
        ViewSettings settings;
        assert(view.Initialize(settings));
        RHI::TexturePtr output = RHI::MakeShared<FakeTexture>("ViewFrameOutput");
        view.SetFrameOutputTexture(output);
        assert(view.GetFrameOutputTexture().get() == output.get());
        view.ResetFrameOutput();
        assert(!view.GetFrameOutputTexture());
        view.SetFrameOutputTexture(output);
        view.Shutdown();
        assert(!view.GetFrameOutputTexture());
        std::cout << "TestViewFrameOutputResetAndShutdownLifetime passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto view = Container::MakeShared<View>();
        ViewSettings settings;
        assert(view->Initialize(settings));
        view->AddPass(Container::MakeUnique<PublishedTextureViewPass>());
        fixture.Views.push_back(view);

        RenderFrameExecutor executor;
        RenderFrameExecutionRequest request = fixture.MakeExecutionRequest();
        request.Presentation = nullptr;
        RenderFrameExecutionResult result = executor.Execute(request);

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 1);
        assert(result.PresentationBlitCount == 1);
        assert(fixture.GraphPresentationPass.WasPresented());
        assert(fixture.Context.bPresentationGraphPassHandled);
        assert(fixture.PendingFrameCommands.empty());
        assert(fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        std::cout << "TestGraphPresentationHandledWithoutComposerSkipsFallbackCompose passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto view = Container::MakeShared<View>();
        ViewSettings settings;
        assert(view->Initialize(settings));
        view->AddPass(Container::MakeUnique<EmptyGraphViewPass>());
        fixture.Views.push_back(view);

        RenderFrameExecutor executor;
        RenderFrameExecutionResult result = executor.Execute(fixture.MakeExecutionRequest());

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 1);
        assert(result.PresentationBlitCount == 1);
        assert(!fixture.GraphPresentationPass.WasPresented());
        assert(!fixture.Context.bPresentationGraphPassHandled);
        assert(fixture.PendingFrameCommands.empty());
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        std::cout << "TestMissingGraphPresentationInputFallsBackToComposer passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto view = Container::MakeShared<View>();
        ViewSettings settings;
        assert(view->Initialize(settings));
        view->AddPass(Container::MakeUnique<PublishedTextureViewPass>());
        fixture.Views.push_back(view);

        RenderFrameExecutionRequest request = fixture.MakeExecutionRequest();
        request.GraphPresentationRequest.BlitPipeline.reset();

        RenderFrameExecutor executor;
        RenderFrameExecutionResult result = executor.Execute(request);

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 1);
        assert(result.PresentationBlitCount == 1);
        assert(!fixture.GraphPresentationPass.WasPresented());
        assert(!fixture.Context.bPresentationGraphPassHandled);
        assert(fixture.PendingFrameCommands.empty());
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        std::cout << "TestMissingGraphPresentationResourceFallsBackToComposer passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto view = Container::MakeShared<View>();
        ViewSettings settings;
        assert(view->Initialize(settings));
        view->AddPass(Container::MakeUnique<EmptyGraphViewPass>());
        fixture.Views.push_back(view);

        RenderFrameExecutionRequest request = fixture.MakeExecutionRequest();
        request.Presentation = nullptr;

        RenderFrameExecutor executor;
        RenderFrameExecutionResult result = executor.Execute(request);

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 1);
        assert(result.PresentationBlitCount == 0);
        assert(!fixture.GraphPresentationPass.WasPresented());
        assert(!fixture.Context.bPresentationGraphPassHandled);
        assert(fixture.PendingFrameCommands.empty());
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        std::cout << "TestMissingGraphPresentationInputWithoutComposerSkipsFallback passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto firstSceneView = Container::MakeShared<View>();
        auto secondSceneView = Container::MakeShared<View>();
        ViewSettings settings;
        assert(firstSceneView->Initialize(settings));
        assert(secondSceneView->Initialize(settings));
        firstSceneView->AddPass(Container::MakeUnique<PublishedTextureViewPass>());
        secondSceneView->AddPass(Container::MakeUnique<PublishedTextureViewPass>());
        fixture.Views.push_back(firstSceneView);
        fixture.Views.push_back(secondSceneView);
        fixture.Packet.Views.push_back(MakeViewPlan(1, ViewType::Scene));

        assert(!RenderFrameExecutor::ShouldCompose(fixture.Packet, fixture.Views));

        RenderFrameExecutor executor;
        RenderFrameExecutionResult result = executor.Execute(fixture.MakeExecutionRequest());

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 2);
        assert(result.PresentationBlitCount == 2);
        assert(fixture.GraphPresentationPass.WasPresented());
        assert(fixture.PendingFrameCommands.empty());
        assert(fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        assert(!fixture.GraphCompositePass.GetLastResult().bPublishedComposite);
        std::cout << "TestTwoSceneViewsWithoutCanvasStayOnLegacyExecutorPath passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto sceneView = Container::MakeShared<View>();
        auto canvasView = Container::MakeShared<DirectOutputCanvasView>();
        ViewSettings sceneSettings;
        ViewSettings canvasSettings;
        canvasSettings.Type = ViewType::UI;
        assert(sceneView->Initialize(sceneSettings));
        assert(canvasView->Initialize(canvasSettings));
        sceneView->AddPass(Container::MakeUnique<PublishedTextureViewPass>());
        fixture.Views.push_back(sceneView);
        fixture.Views.push_back(canvasView);

        ViewRenderPlan canvasPlan;
        canvasPlan.ViewId = 1;
        canvasPlan.ViewType = static_cast<uint8_t>(ViewType::UI);
        canvasPlan.Priority = 10;
        fixture.Packet.Views.push_back(canvasPlan);

        RenderFrameExecutor executor;
        RenderFrameExecutionResult result = executor.Execute(fixture.MakeExecutionRequest());

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 2);
        assert(result.PresentationBlitCount == 1);
        assert(canvasView->RenderCount == 1);
        assert(canvasView->GetFrameOutputTexture().get() == canvasView->OutputTexture.get());
        assert(fixture.PendingFrameCommands.empty());
        assert(fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        assert(!fixture.Context.PresentationGraphPass);
        assert(!fixture.Context.bPresentationGraphPassHandled);
        assert(!fixture.GraphCompositePass.GetLastResult().bPublishedComposite);
        std::cout << "TestZeroViewportCanvasRendersDirectlyAndPresentsOnce passed\n";
    }

    {
        ExecutorPresentationFixture fixture;
        auto missingInputView = Container::MakeShared<View>();
        auto publishedInputView = Container::MakeShared<View>();
        ViewSettings settings;
        assert(missingInputView->Initialize(settings));
        assert(publishedInputView->Initialize(settings));
        missingInputView->AddPass(Container::MakeUnique<EmptyGraphViewPass>());
        publishedInputView->AddPass(Container::MakeUnique<PublishedTextureViewPass>());
        fixture.Views.push_back(missingInputView);
        fixture.Views.push_back(publishedInputView);
        fixture.Packet.Views.push_back(MakeViewPlan(1, ViewType::Scene));

        RenderFrameExecutionRequest request = fixture.MakeExecutionRequest();
        request.Presentation = nullptr;

        RenderFrameExecutor executor;
        RenderFrameExecutionResult result = executor.Execute(request);

        assert(result.bRenderedAnyViewport);
        assert(result.RenderedViewportCount == 2);
        assert(result.PresentationBlitCount == 1);
        assert(fixture.GraphPresentationPass.WasPresented());
        assert(fixture.Context.bPresentationGraphPassHandled);
        assert(fixture.PendingFrameCommands.empty());
        assert(fixture.CommandList.HasRenderPass(fixture.GraphClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.GraphLoadRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackClearRenderPass.get()));
        assert(!fixture.CommandList.HasRenderPass(fixture.FallbackLoadRenderPass.get()));
        std::cout << "TestUnpresentedViewportDoesNotConsumeClearPresentation passed\n";
    }

    std::cout << "RenderFrameExecutorPlanTest passed\n";
    return 0;
}
