#include "Rendering/CompositePass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/ICommandList.h"
#include "RHI/IGPUResourceAllocator.h"
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

    class FakeShader final : public RHI::IShader
    {
    public:
        explicit FakeShader(RHI::ShaderStage stage)
            : m_Stage(stage)
        {
        }

        RHI::ShaderStage GetStage() const override { return m_Stage; }
        Container::String GetEntryPoint() const override { return "main"; }
        const Container::VariableArray<uint8_t> &GetByteCode() const override { return m_ByteCode; }

    private:
        RHI::ShaderStage m_Stage = RHI::ShaderStage::None;
        Container::VariableArray<uint8_t> m_ByteCode;
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
                SceneTexture = texture;
            }
            else if (binding == 1)
            {
                CanvasTexture = texture;
            }
        }
        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            if (binding == 0)
            {
                SceneSampler = sampler;
            }
            else if (binding == 1)
            {
                CanvasSampler = sampler;
            }
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

        RHI::TexturePtr SceneTexture;
        RHI::TexturePtr CanvasTexture;
        RHI::SamplerPtr SceneSampler;
        RHI::SamplerPtr CanvasSampler;
        uint32_t UpdateCount = 0;
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
            return RHI::MakeShared<FakeSampler>();
        }
        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc& desc) override
        {
            return RHI::MakeShared<FakeShader>(desc.stage);
        }
        RHI::CommandListPtr CreateCommandList() override { return nullptr; }
        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }
        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc& desc) override
        {
            ++CreateRenderPassCount;
            LastRenderPassDesc = desc;
            return RHI::MakeShared<FakeRenderPass>(desc);
        }
        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            ++CreateFramebufferCount;
            LastFramebufferDesc = desc;
            return RHI::MakeShared<FakeFramebuffer>(desc);
        }
        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) override
        {
            ++CreateGraphicsPipelineCount;
            LastPipelineDesc = desc;
            return RHI::MakeShared<FakePipeline>();
        }
        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }
        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc& desc) override
        {
            (void)desc;
            return RHI::MakeShared<FakeDescriptorSet>();
        }
        RHI::ShaderCompilerPtr CreateShaderCompiler() override { return nullptr; }
        RHI::IGPUResourceAllocator* GetResourceAllocator() override { return &Allocator; }
        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::None; }
        const RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4 &projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        FakeAllocator Allocator;
        RHI::DeviceCapabilities Capabilities;
        RHI::RenderPassDesc LastRenderPassDesc;
        RHI::FramebufferDesc LastFramebufferDesc;
        RHI::GraphicsPipelineDesc LastPipelineDesc;
        uint32_t CreateRenderPassCount = 0;
        uint32_t CreateFramebufferCount = 0;
        uint32_t CreateGraphicsPipelineCount = 0;
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
            (void)texture;
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
        }
    };

    struct CompositeFixture
    {
        FakeAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderGraph Graph;
        FakeCommandList CommandList;
        FakeDevice Device;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        ViewRenderContext Context;

        CompositeFixture()
        {
            assert(Pool.Initialize(&Allocator, 1));
            Pool.BeginFrame(0);
            assert(Graph.Initialize(&Pool));
            Graph.BeginFrame(0);
            Context.Graph = &Graph;
            Context.CommandList = &CommandList;
            Context.TransientPool = &Pool;
            Context.Device = &Device;
            Context.PendingFrameCommands = &PendingFrameCommands;
            Context.RenderWidth = 64;
            Context.RenderHeight = 32;
            Context.ScreenWidth = 64;
            Context.ScreenHeight = 32;
        }

        ~CompositeFixture()
        {
            Graph.Shutdown();
            Pool.EndFrame();
            Pool.Shutdown();
        }
    };

    void AssertReadAccess(RenderGraph& graph, uint32_t passIndex, uint32_t accessIndex)
    {
        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Write;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        bool bAttachment = true;

        assert(graph.TryGetDeclaredPassAccess(passIndex,
                                              accessIndex,
                                              resource,
                                              mode,
                                              state,
                                              finalState,
                                              &bAttachment));
        assert(resource.IsValid());
        assert(mode == RGAccessMode::Read);
        assert(state == RHI::ResourceState::ShaderResource);
        assert(finalState == RHI::ResourceState::ShaderResource);
        assert(!bAttachment);
    }

    void TestResourceNamesIncludeCanvasAndComposite()
    {
        assert(RenderGraphResourceNames::CanvasColor.IsValid());
        assert(RenderGraphResourceNames::CompositeColor.IsValid());
        assert(RenderGraphResourceNames::CanvasColor != RenderGraphResourceNames::SceneColor);
        assert(RenderGraphResourceNames::CompositeColor != RenderGraphResourceNames::SceneColor);
        assert(RenderGraphResourceNames::CompositeColor != RenderGraphResourceNames::PresentationColor);
        std::cout << "TestResourceNamesIncludeCanvasAndComposite passed\n";
    }

    void TestScenePassthroughWithoutCanvasPublishesCompositeColor()
    {
        CompositeFixture fixture;
        RHI::TexturePtr sceneTexture = RHI::MakeShared<FakeTexture>("PhysicalSceneOutput");

        CompositePass pass;
        CompositePassRequest request;
        request.SceneTexture = sceneTexture;
        pass.SetRequest(request);

        const uint32_t passIndex = fixture.Graph.AddPass(&pass);
        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.GetDeclaredPassAccessCount(passIndex) == 1);
        AssertReadAccess(fixture.Graph, passIndex, 0);

        RenderGraphExecutionResult result = fixture.Graph.ExecuteWithResult(fixture.Context);
        assert(result.bSuccess);

        RHI::TexturePtr outputTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::CompositeColor, outputTexture));
        assert(outputTexture.get() == sceneTexture.get());
        assert(!result.TryGetTexture(RenderGraphResourceNames::CanvasColor, outputTexture));

        const CompositePassResult& passResult = pass.GetLastResult();
        assert(passResult.bPublishedComposite);
        assert(!passResult.bImportedCanvas);
        assert(passResult.bScenePassthrough);
        assert(passResult.SceneTexture.get() == sceneTexture.get());
        assert(passResult.OutputTexture.get() == sceneTexture.get());
        std::cout << "TestScenePassthroughWithoutCanvasPublishesCompositeColor passed\n";
    }

    void TestCanvasTextureImportedPhysicallyButSceneStillPassthrough()
    {
        CompositeFixture fixture;
        RHI::TexturePtr sceneTexture = RHI::MakeShared<FakeTexture>("PhysicalSceneOutput");
        RHI::TexturePtr canvasTexture = RHI::MakeShared<FakeTexture>("PhysicalCanvasOutput");

        CompositePass pass;
        CompositePassRequest request;
        request.SceneTexture = sceneTexture;
        request.CanvasTexture = canvasTexture;
        pass.SetRequest(request);

        const uint32_t passIndex = fixture.Graph.AddPass(&pass);
        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.GetDeclaredPassAccessCount(passIndex) == 2);
        AssertReadAccess(fixture.Graph, passIndex, 0);
        AssertReadAccess(fixture.Graph, passIndex, 1);

        RenderGraphExecutionResult result = fixture.Graph.ExecuteWithResult(fixture.Context);
        assert(result.bSuccess);

        RHI::TexturePtr outputTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::CompositeColor, outputTexture));
        assert(outputTexture.get() == sceneTexture.get());

        const CompositePassResult& passResult = pass.GetLastResult();
        assert(passResult.bPublishedComposite);
        assert(passResult.bImportedCanvas);
        assert(passResult.bScenePassthrough);
        assert(passResult.SceneTexture.get() == sceneTexture.get());
        assert(passResult.CanvasTexture.get() == canvasTexture.get());
        assert(passResult.OutputTexture.get() == sceneTexture.get());
        std::cout << "TestCanvasTextureImportedPhysicallyButSceneStillPassthrough passed\n";
    }

    void TestCanvasAlphaOverPublishesCompositeTexture()
    {
        CompositeFixture fixture;
        RHI::TexturePtr sceneTexture = RHI::MakeShared<FakeTexture>("PhysicalSceneOutput");
        RHI::TexturePtr canvasTexture = RHI::MakeShared<FakeTexture>("PhysicalCanvasOutput");
        RHI::ShaderPtr vertexShader = RHI::MakeShared<FakeShader>(RHI::ShaderStage::Vertex);
        RHI::ShaderPtr pixelShader = RHI::MakeShared<FakeShader>(RHI::ShaderStage::Pixel);
        RHI::SamplerPtr sampler = RHI::MakeShared<FakeSampler>();
        RHI::DescriptorSetPtr descriptorSet = RHI::MakeShared<FakeDescriptorSet>();

        CompositePass pass;
        CompositePassRequest request;
        request.SceneTexture = sceneTexture;
        request.CanvasTexture = canvasTexture;
        request.VertexShader = vertexShader;
        request.PixelShader = pixelShader;
        request.Sampler = sampler;
        request.DescriptorSet = descriptorSet;
        pass.SetRequest(request);

        const uint32_t passIndex = fixture.Graph.AddPass(&pass);
        assert(fixture.Graph.Compile(fixture.Context));
        assert(fixture.Graph.GetDeclaredPassAccessCount(passIndex) == 3);
        AssertReadAccess(fixture.Graph, passIndex, 0);
        AssertReadAccess(fixture.Graph, passIndex, 1);

        RenderGraphExecutionResult result = fixture.Graph.ExecuteWithResult(fixture.Context);
        assert(result.bSuccess);

        RHI::TexturePtr outputTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::CompositeColor, outputTexture));
        assert(outputTexture.get() != sceneTexture.get());
        assert(outputTexture->GetWidth() == sceneTexture->GetWidth());
        assert(outputTexture->GetHeight() == sceneTexture->GetHeight());

        const CompositePassResult& passResult = pass.GetLastResult();
        assert(passResult.bPublishedComposite);
        assert(passResult.bImportedCanvas);
        assert(!passResult.bScenePassthrough);
        assert(passResult.SceneTexture.get() == sceneTexture.get());
        assert(passResult.CanvasTexture.get() == canvasTexture.get());
        assert(passResult.OutputTexture.get() == outputTexture.get());
        assert(passResult.RenderPass);
        assert(passResult.Framebuffer);
        assert(passResult.Pipeline);
        assert(fixture.PendingFrameCommands.size() == 1);
        assert(fixture.Device.CreateRenderPassCount == 1);
        assert(fixture.Device.CreateFramebufferCount == 1);
        assert(fixture.Device.CreateGraphicsPipelineCount == 1);

        auto fakeDescriptorSet = static_cast<FakeDescriptorSet*>(descriptorSet.get());
        assert(fakeDescriptorSet->SceneTexture.get() == sceneTexture.get());
        assert(fakeDescriptorSet->CanvasTexture.get() == canvasTexture.get());
        assert(fakeDescriptorSet->SceneSampler.get() == sampler.get());
        assert(fakeDescriptorSet->CanvasSampler.get() == sampler.get());
        assert(fakeDescriptorSet->UpdateCount == 1);
        std::cout << "TestCanvasAlphaOverPublishesCompositeTexture passed\n";
    }
} // namespace

int main()
{
    std::cout << "CompositePassTest start\n";

    TestResourceNamesIncludeCanvasAndComposite();
    TestScenePassthroughWithoutCanvasPublishesCompositeColor();
    TestCanvasTextureImportedPhysicallyButSceneStillPassthrough();
    TestCanvasAlphaOverPublishesCompositeTexture();

    std::cout << "CompositePassTest passed\n";
    return 0;
}
