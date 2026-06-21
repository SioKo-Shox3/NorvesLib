#include "Rendering/CanvasView.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/Viewport.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
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
        explicit FakeRenderPass(const RHI::RenderPassDesc& desc)
            : Desc(desc)
        {
        }

        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(Desc.colorAttachments.size());
        }
        bool HasDepthStencilAttachment() const override { return Desc.hasDepthStencil; }
        RHI::Format GetColorAttachmentFormat(uint32_t index) const override
        {
            return index < Desc.colorAttachments.size()
                       ? Desc.colorAttachments[index].format
                       : RHI::Format::UNKNOWN;
        }
        RHI::Format GetDepthStencilFormat() const override
        {
            return Desc.hasDepthStencil ? Desc.depthStencilAttachment.format : RHI::Format::UNKNOWN;
        }

        RHI::RenderPassDesc Desc;
    };

    class FakeFramebuffer final : public RHI::IFramebuffer
    {
    public:
        explicit FakeFramebuffer(const RHI::FramebufferDesc& desc)
            : Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return Desc.width; }
        uint32_t GetHeight() const override { return Desc.height; }
        RHI::RenderPassPtr GetRenderPass() const override { return Desc.renderPass; }
        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            return index < Desc.colorTargets.size() ? Desc.colorTargets[index] : nullptr;
        }
        RHI::TexturePtr GetDepthStencilAttachment() const override { return Desc.depthStencilTarget; }
        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(Desc.colorTargets.size());
        }
        bool HasDepthStencilAttachment() const override { return Desc.depthStencilTarget != nullptr; }

        RHI::FramebufferDesc Desc;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            LastRenderPass = renderPass;
            LastFramebuffer = framebuffer;
            ++BeginRenderPassCount;
        }
        void EndRenderPass() override { ++EndRenderPassCount; }
        void SetViewport(const RHI::Viewport& viewport) override { LastViewport = viewport; }
        void SetScissor(const RHI::ScissorRect& scissor) override { LastScissor = scissor; }
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
            (void)texture;
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
        }

        RHI::RenderPassPtr LastRenderPass;
        RHI::FramebufferPtr LastFramebuffer;
        RHI::Viewport LastViewport;
        RHI::ScissorRect LastScissor;
        uint32_t BeginRenderPassCount = 0;
        uint32_t EndRenderPassCount = 0;
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            return Allocator.AllocateBuffer(desc).Buffer ? Allocator.Buffers.back() : nullptr;
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return RHI::MakeShared<FakeTexture>(desc);
        }

        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::CommandListPtr CreateCommandList() override
        {
            return nullptr;
        }

        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc& desc) override
        {
            LastRenderPassDesc = desc;
            ++CreateRenderPassCount;
            return RHI::MakeShared<FakeRenderPass>(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            LastFramebufferDesc = desc;
            ++CreateFramebufferCount;
            return RHI::MakeShared<FakeFramebuffer>(desc);
        }

        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return nullptr;
        }

        RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return &Allocator;
        }

        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::None; }
        const RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        FakeAllocator Allocator;
        RHI::DeviceCapabilities Capabilities;
        RHI::RenderPassDesc LastRenderPassDesc;
        RHI::FramebufferDesc LastFramebufferDesc;
        uint32_t CreateRenderPassCount = 0;
        uint32_t CreateFramebufferCount = 0;
    };

    struct CanvasFixture
    {
        FakeDevice Device;
        RHI::TransientResourcePool Pool;
        RenderGraph Graph;
        FakeCommandList CommandList;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        ViewRenderContext Context;

        CanvasFixture()
        {
            assert(Pool.Initialize(Device.GetResourceAllocator(), 1));
            Pool.BeginFrame(0);
            assert(Graph.Initialize(&Pool));
            Graph.BeginFrame(0);
            Context.Device = &Device;
            Context.Graph = &Graph;
            Context.CommandList = &CommandList;
            Context.TransientPool = &Pool;
            Context.PendingFrameCommands = &PendingFrameCommands;
            Context.RenderWidth = 96;
            Context.RenderHeight = 54;
            Context.ScreenWidth = 96;
            Context.ScreenHeight = 54;
        }

        ~CanvasFixture()
        {
            Graph.Shutdown();
            Pool.EndFrame();
            Pool.Shutdown();
        }
    };

    void TestCanvasViewClearsTransparentAndExportsFrameOutput()
    {
        CanvasFixture fixture;
        CanvasView canvas;
        ViewSettings settings;
        settings.Type = ViewType::Scene;
        settings.Width = 1;
        settings.Height = 1;
        settings.ClearColor[3] = 1.0f;
        assert(canvas.Initialize(settings));
        assert(canvas.GetViewportCount() == 1);
        auto viewport = canvas.GetMainViewport();
        assert(viewport);
        assert(viewport->IsEnabled());

        float x = 1.0f;
        float y = 1.0f;
        float width = 0.0f;
        float height = 0.0f;
        viewport->GetRect(x, y, width, height);
        assert(x == 0.0f);
        assert(y == 0.0f);
        assert(width == 1.0f);
        assert(height == 1.0f);

        canvas.Render(fixture.Context);

        RHI::TexturePtr frameOutput = canvas.GetFrameOutputTexture();
        assert(frameOutput);
        assert(frameOutput->GetWidth() == 96);
        assert(frameOutput->GetHeight() == 54);

        RHI::TexturePtr graphOutput;
        assert(fixture.Graph.TryGetLastOutputTexture(RenderGraphResourceNames::CanvasColor, graphOutput));
        assert(graphOutput.get() == frameOutput.get());
        assert(fixture.Context.CurrentGraphExecutionResult != nullptr);
        assert(fixture.Context.CurrentGraphExecutionResult->bSuccess);
        assert(!fixture.Context.bPresentationGraphPassHandled);

        assert(fixture.Device.CreateRenderPassCount == 1);
        assert(fixture.Device.CreateFramebufferCount == 1);
        assert(fixture.Device.LastRenderPassDesc.colorAttachments.size() == 1);
        const RHI::AttachmentDesc& colorAttachment = fixture.Device.LastRenderPassDesc.colorAttachments[0];
        assert(colorAttachment.format == RHI::Format::R8G8B8A8_UNORM);
        assert(colorAttachment.clear);
        assert(colorAttachment.clearColor[0] == 0.0f);
        assert(colorAttachment.clearColor[1] == 0.0f);
        assert(colorAttachment.clearColor[2] == 0.0f);
        assert(colorAttachment.clearColor[3] == 0.0f);
        assert(colorAttachment.loadOp == RHI::AttachmentLoadOp::Clear);
        assert(colorAttachment.storeOp == RHI::AttachmentStoreOp::Store);
        assert(colorAttachment.initialState == RHI::ResourceState::RenderTarget);
        assert(colorAttachment.finalState == RHI::ResourceState::ShaderResource);

        assert(fixture.Device.LastFramebufferDesc.colorTargets.size() == 1);
        assert(fixture.Device.LastFramebufferDesc.colorTargets[0].get() == frameOutput.get());
        assert(fixture.Device.LastFramebufferDesc.width == 96);
        assert(fixture.Device.LastFramebufferDesc.height == 54);

        assert(fixture.CommandList.BeginRenderPassCount == 1);
        assert(fixture.CommandList.EndRenderPassCount == 1);
        assert(fixture.CommandList.LastRenderPass);
        assert(fixture.CommandList.LastFramebuffer);
        assert(fixture.CommandList.LastViewport.width == 96.0f);
        assert(fixture.CommandList.LastViewport.height == 54.0f);
        assert(fixture.CommandList.LastScissor.right == 96);
        assert(fixture.CommandList.LastScissor.bottom == 54);

        assert(canvas.GetRetainedBoardFrameResourceCount() == 1);
        canvas.Shutdown();
        assert(canvas.GetRetainedBoardFrameResourceCount() == 0);
        std::cout << "TestCanvasViewClearsTransparentAndExportsFrameOutput passed\n";
    }
} // namespace

int main()
{
    std::cout << "CanvasViewRenderTest start\n";

    TestCanvasViewClearsTransparentAndExportsFrameOutput();

    std::cout << "CanvasViewRenderTest passed\n";
    return 0;
}
