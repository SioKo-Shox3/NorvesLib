#include "Rendering/CompositePass.h"
#define private public
#include "Rendering/FXAAPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/PresentationPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/UpscalePass.h"
#undef private
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
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
    bool HasUsage(RHI::ResourceUsage value, RHI::ResourceUsage flag)
    {
        return static_cast<uint32_t>(value & flag) != 0;
    }

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
            allocation.Size = static_cast<size_t>(desc.Width) *
                              static_cast<size_t>(desc.Height) *
                              static_cast<size_t>(desc.Depth) *
                              static_cast<size_t>(desc.ArraySize) *
                              4u;
            allocation.Type = type;
            return allocation;
        }

        void FreeTexture(RHI::TextureAllocation& allocation) override
        {
            allocation.Texture = nullptr;
            allocation.Size = 0;
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

        FakeRenderPass()
        {
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = RHI::Format::R8G8B8A8_UNORM;
            Desc.colorAttachments.push_back(colorAttachment);
        }

        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(Desc.colorAttachments.size());
        }

        bool HasDepthStencilAttachment() const override
        {
            return Desc.hasDepthStencil;
        }

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

        explicit FakeFramebuffer(RHI::RenderPassPtr renderPass)
        {
            Desc.renderPass = renderPass;
            Desc.width = 64;
            Desc.height = 32;
        }

        uint32_t GetWidth() const override
        {
            return Desc.width;
        }

        uint32_t GetHeight() const override
        {
            return Desc.height;
        }

        RHI::RenderPassPtr GetRenderPass() const override
        {
            return Desc.renderPass;
        }

        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            return index < Desc.colorTargets.size() ? Desc.colorTargets[index] : nullptr;
        }

        RHI::TexturePtr GetDepthStencilAttachment() const override
        {
            return Desc.depthStencilTarget;
        }

        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(Desc.colorTargets.size());
        }

        bool HasDepthStencilAttachment() const override
        {
            return Desc.depthStencilTarget != nullptr;
        }

        RHI::FramebufferDesc Desc;
    };

    class FakeShader final : public RHI::IShader
    {
    public:
        explicit FakeShader(RHI::ShaderStage stage)
            : m_Stage(stage)
        {
        }

        RHI::ShaderStage GetStage() const override
        {
            return m_Stage;
        }

        Container::String GetEntryPoint() const override
        {
            return "main";
        }

        const Container::VariableArray<uint8_t>& GetByteCode() const override
        {
            return m_ByteCode;
        }

    private:
        RHI::ShaderStage m_Stage = RHI::ShaderStage::None;
        Container::VariableArray<uint8_t> m_ByteCode;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        RHI::PipelineType GetPipelineType() const override
        {
            return RHI::PipelineType::Graphics;
        }

        uint32_t GetBindPointCount() const override
        {
            return 1;
        }
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        RHI::FilterMode GetFilterMin() const override
        {
            return RHI::FilterMode::Linear;
        }

        RHI::FilterMode GetFilterMag() const override
        {
            return RHI::FilterMode::Linear;
        }

        RHI::FilterMode GetFilterMip() const override
        {
            return RHI::FilterMode::Linear;
        }

        RHI::TextureAddressMode GetAddressModeU() const override
        {
            return RHI::TextureAddressMode::Clamp;
        }

        RHI::TextureAddressMode GetAddressModeV() const override
        {
            return RHI::TextureAddressMode::Clamp;
        }

        RHI::TextureAddressMode GetAddressModeW() const override
        {
            return RHI::TextureAddressMode::Clamp;
        }

        uint32_t GetMaxAnisotropy() const override
        {
            return 1;
        }

        RHI::CompareFunc GetCompareFunc() const override
        {
            return RHI::CompareFunc::Never;
        }
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
            else if (binding == 1)
            {
                BoundCanvasTexture = texture;
            }
            ++BindTextureCount;
        }

        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            (void)binding;
            BoundSampler = sampler;
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

        void Update() override
        {
            ++UpdateCount;
        }

        RHI::TexturePtr BoundTexture;
        RHI::TexturePtr BoundCanvasTexture;
        RHI::SamplerPtr BoundSampler;
        uint32_t BindTextureCount = 0;
        uint32_t BindSamplerCount = 0;
        uint32_t UpdateCount = 0;
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            RHI::BufferAllocation allocation = Allocator.AllocateBuffer(desc);
            return allocation.Buffer ? Allocator.Buffers.back() : nullptr;
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            CreatedTextureDescs.push_back(desc);
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
            return RHI::MakeShared<FakeRenderPass>(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            ++CreatedFramebufferCount;
            return RHI::MakeShared<FakeFramebuffer>(desc);
        }

        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) override
        {
            (void)desc;
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

        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return nullptr;
        }

        RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return &Allocator;
        }

        uint32_t CreatedFramebufferCount = 0;

        void WaitIdle() override
        {
        }

        RHI::API GetAPI() const override
        {
            return RHI::API::None;
        }

        const RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return Capabilities;
        }

        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        FakeAllocator Allocator;
        RHI::DeviceCapabilities Capabilities;
        Container::VariableArray<RHI::TextureDesc> CreatedTextureDescs;
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

    struct GraphFixture
    {
        FakeAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderGraph Graph;
        FakeCommandList CommandList;
        FakeDevice Device;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        ViewRenderContext Context;

        GraphFixture()
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

        ~GraphFixture()
        {
            Graph.Shutdown();
            Pool.EndFrame();
            Pool.Shutdown();
        }

        PresentationPassRequest MakePresentationRequest(RHI::TexturePtr backBuffer) const
        {
            RHI::RenderPassPtr clearRenderPass = RHI::MakeShared<FakeRenderPass>();
            RHI::RenderPassPtr loadRenderPass = RHI::MakeShared<FakeRenderPass>();

            PresentationPassRequest request;
            request.BackBufferTexture = backBuffer;
            request.ClearRenderPass = clearRenderPass;
            request.LoadRenderPass = loadRenderPass;
            request.ClearFramebuffer = RHI::MakeShared<FakeFramebuffer>(clearRenderPass);
            request.LoadFramebuffer = RHI::MakeShared<FakeFramebuffer>(loadRenderPass);
            request.BlitPipeline = RHI::MakeShared<FakePipeline>();
            request.BlitDescriptorSet = RHI::MakeShared<FakeDescriptorSet>();
            request.BlitSampler = RHI::MakeShared<FakeSampler>();
            request.bClearPresentation = true;
            return request;
        }
    };

    void AssertTextureHasTransferSrc(const RHI::TexturePtr& texture)
    {
        assert(texture);
        assert(HasUsage(texture->GetUsage(), RHI::ResourceUsage::TransferSrc));
    }

    const RHI::TextureDesc* FindCreatedTextureDesc(const FakeDevice& device, const char* debugName)
    {
        for (const RHI::TextureDesc& desc : device.CreatedTextureDescs)
        {
            if (desc.DebugName && std::strcmp(desc.DebugName, debugName) == 0)
            {
                return &desc;
            }
        }

        return nullptr;
    }

    class TestFXAAPass final : public FXAAPass
    {
    public:
        void SetTestDevice(RHI::IDevice* device)
        {
            m_Device = device;
        }
    };

    class TestLightingPass final : public LightingPass
    {
    public:
        void MarkInitializedForGraphTest()
        {
            m_bInitialized = true;
        }
    };

    void TestRenderTargetDescDoesNotIncludeTransferSrc()
    {
        RGTextureDesc desc = RGTextureDesc::RenderTarget(64,
                                                         32,
                                                         RHI::Format::R8G8B8A8_UNORM,
                                                         "PlainRenderTarget");
        assert(!HasUsage(desc.Usage, RHI::ResourceUsage::TransferSrc));
        std::cout << "TestRenderTargetDescDoesNotIncludeTransferSrc passed\n";
    }

    void TestToneMappingGraphOutputIncludesTransferSrc()
    {
        GraphFixture fixture;
        ToneMappingPass pass;
        fixture.Graph.AddPass(&pass);

        assert(fixture.Graph.Compile(fixture.Context));

        RenderGraphResources resources(&fixture.Graph);
        RHI::TexturePtr outputTexture = resources.GetTexture(pass.GetToneMappedColorHandle());
        AssertTextureHasTransferSrc(outputTexture);
        std::cout << "TestToneMappingGraphOutputIncludesTransferSrc passed\n";
    }

    void TestLightingSceneColorIncludesTransferSrcAndIsExported()
    {
        GraphFixture fixture;
        TestLightingPass pass;
        pass.MarkInitializedForGraphTest();
        fixture.Graph.AddPass(&pass);

        assert(fixture.Graph.Compile(fixture.Context));

        RenderGraphResources resources(&fixture.Graph);
        RHI::TexturePtr sceneColorTexture = resources.GetTexture(pass.GetSceneColorHandle());
        AssertTextureHasTransferSrc(sceneColorTexture);

        const RenderGraphExecutionResult result = fixture.Graph.ExecuteWithResult(fixture.Context);
        assert(result.bSuccess);
        RHI::TexturePtr exportedSceneColor;
        assert(result.TryGetTexture(RenderGraphResourceNames::SceneColor, exportedSceneColor));
        assert(exportedSceneColor.get() == sceneColorTexture.get());
        std::cout << "TestLightingSceneColorIncludesTransferSrcAndIsExported passed\n";
    }

    void TestLightingPersistentSceneColorIncludesTransferSrc()
    {
        GraphFixture fixture;
        LightingPass pass;
        pass.m_Device = &fixture.Device;
        pass.Setup(fixture.Context);

        const RHI::TextureDesc* desc = FindCreatedTextureDesc(fixture.Device, "SceneColor");
        assert(desc);
        assert(HasUsage(desc->Usage, RHI::ResourceUsage::TransferSrc));
        std::cout << "TestLightingPersistentSceneColorIncludesTransferSrc passed\n";
    }

    void TestToneMappingPersistentOutputIncludesTransferSrc()
    {
        GraphFixture fixture;
        ToneMappingPass pass;
        pass.m_Device = &fixture.Device;
        pass.Setup(fixture.Context);

        const RHI::TextureDesc* desc = FindCreatedTextureDesc(fixture.Device, "ToneMappedColor");
        assert(desc);
        assert(HasUsage(desc->Usage, RHI::ResourceUsage::TransferSrc));
        std::cout << "TestToneMappingPersistentOutputIncludesTransferSrc passed\n";
    }

    void TestFXAAGraphOutputIncludesTransferSrc()
    {
        GraphFixture fixture;
        ToneMappingPass toneMappingPass;
        FXAAPass fxaaPass;

        fixture.Graph.AddPass(&toneMappingPass);
        fixture.Graph.AddPass(&fxaaPass);

        assert(fixture.Graph.Compile(fixture.Context));

        RenderGraphResources resources(&fixture.Graph);
        RHI::TexturePtr outputTexture = resources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        AssertTextureHasTransferSrc(outputTexture);
        std::cout << "TestFXAAGraphOutputIncludesTransferSrc passed\n";
    }

    void TestFXAAPersistentOutputIncludesTransferSrc()
    {
        GraphFixture fixture;
        TestFXAAPass pass;
        pass.SetTestDevice(&fixture.Device);
        pass.Setup(fixture.Context);

        const RHI::TextureDesc* desc = FindCreatedTextureDesc(fixture.Device, "FXAAOutput");
        assert(desc);
        assert(HasUsage(desc->Usage, RHI::ResourceUsage::TransferSrc));
        std::cout << "TestFXAAPersistentOutputIncludesTransferSrc passed\n";
    }

    void TestUpscaleFreshPresentationColorIncludesTransferSrc()
    {
        GraphFixture fixture;
        fixture.Context.RenderWidth = 64;
        fixture.Context.RenderHeight = 32;
        fixture.Context.ScreenWidth = 128;
        fixture.Context.ScreenHeight = 64;

        ToneMappingPass toneMappingPass;
        UpscalePass upscalePass;

        fixture.Graph.AddPass(&toneMappingPass);
        fixture.Graph.AddPass(&upscalePass);

        assert(fixture.Graph.Compile(fixture.Context));

        RenderGraphResources resources(&fixture.Graph);
        RHI::TexturePtr outputTexture = resources.GetTexture(upscalePass.GetPresentationColorHandle());
        AssertTextureHasTransferSrc(outputTexture);
        std::cout << "TestUpscaleFreshPresentationColorIncludesTransferSrc passed\n";
    }

    void TestUpscalePersistentOutputIncludesTransferSrc()
    {
        GraphFixture fixture;
        fixture.Context.ScreenWidth = 128;
        fixture.Context.ScreenHeight = 64;

        UpscalePass pass;
        pass.m_Device = &fixture.Device;
        pass.Setup(fixture.Context);

        const RHI::TextureDesc* desc = FindCreatedTextureDesc(fixture.Device, "PresentationColor");
        assert(desc);
        assert(HasUsage(desc->Usage, RHI::ResourceUsage::TransferSrc));
        std::cout << "TestUpscalePersistentOutputIncludesTransferSrc passed\n";
    }

    void TestCompositeAlphaOverFreshColorIncludesTransferSrc()
    {
        GraphFixture fixture;
        RHI::TexturePtr sceneTexture = RHI::MakeShared<FakeTexture>("PhysicalSceneOutput");
        RHI::TexturePtr canvasTexture = RHI::MakeShared<FakeTexture>("PhysicalCanvasOutput");

        CompositePass pass;
        CompositePassRequest request;
        request.SceneTexture = sceneTexture;
        request.CanvasTexture = canvasTexture;
        request.VertexShader = RHI::MakeShared<FakeShader>(RHI::ShaderStage::Vertex);
        request.PixelShader = RHI::MakeShared<FakeShader>(RHI::ShaderStage::Pixel);
        request.DescriptorSet = RHI::MakeShared<FakeDescriptorSet>();
        request.Sampler = RHI::MakeShared<FakeSampler>();
        pass.SetRequest(request);

        fixture.Graph.AddPass(&pass);
        assert(fixture.Graph.Compile(fixture.Context));

        RenderGraphExecutionResult result = fixture.Graph.ExecuteWithResult(fixture.Context);
        assert(result.bSuccess);

        RHI::TexturePtr outputTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::CompositeColor, outputTexture));
        assert(outputTexture.get() != sceneTexture.get());
        AssertTextureHasTransferSrc(outputTexture);
        std::cout << "TestCompositeAlphaOverFreshColorIncludesTransferSrc passed\n";
    }

    void TestCompositePassthroughPresentationKeepsImportedSceneWithoutTransferSrc()
    {
        GraphFixture fixture;

        RHI::TextureDesc sceneDesc =
            RHI::TextureDesc::RenderTarget(64,
                                           32,
                                           RHI::Format::R8G8B8A8_UNORM,
                                           "ImportedSceneWithoutTransferSrc");
        assert(!HasUsage(sceneDesc.Usage, RHI::ResourceUsage::TransferSrc));

        RHI::TexturePtr sceneTexture = RHI::MakeShared<FakeTexture>(sceneDesc);
        RHI::TexturePtr backBuffer = RHI::MakeShared<FakeTexture>("BackBuffer");

        CompositePass compositePass;
        CompositePassRequest compositeRequest;
        compositeRequest.SceneTexture = sceneTexture;
        compositePass.SetRequest(compositeRequest);

        PresentationPass presentationPass;
        PresentationPassRequest presentationRequest = fixture.MakePresentationRequest(backBuffer);
        presentationPass.SetRequest(presentationRequest);

        fixture.Graph.AddPass(&compositePass);
        fixture.Graph.AddPass(&presentationPass);

        assert(fixture.Graph.Compile(fixture.Context));
        RenderGraphExecutionResult result = fixture.Graph.ExecuteWithResult(fixture.Context);
        assert(result.bSuccess);
        assert(presentationPass.WasPresented());
        assert(presentationPass.GetLastResult().InputName == RenderGraphResourceNames::CompositeColor);
        assert(presentationPass.GetLastResult().InputTexture.get() == sceneTexture.get());
        assert(!HasUsage(presentationPass.GetLastResult().InputTexture->GetUsage(),
                         RHI::ResourceUsage::TransferSrc));

        FakeDescriptorSet* descriptorSet =
            static_cast<FakeDescriptorSet*>(presentationRequest.BlitDescriptorSet.get());
        assert(descriptorSet->BoundTexture.get() == sceneTexture.get());
        std::cout << "TestCompositePassthroughPresentationKeepsImportedSceneWithoutTransferSrc passed\n";
    }

    void TestPresentationOverlayFramebufferRingOwnsAndReusesByFrameSlotKey()
    {
        RHI::DevicePtr device = RHI::MakeShared<FakeDevice>();
        auto* fakeDevice = static_cast<FakeDevice*>(device.get());

        RHI::TextureDesc overlayDesc =
            RHI::TextureDesc::RenderTarget(64,
                                           32,
                                           RHI::Format::R16G16B16A16_FLOAT,
                                           "OverlayPresentationColor");
        RHI::TexturePtr firstTarget = RHI::MakeShared<FakeTexture>(overlayDesc);
        RHI::TexturePtr replacementTarget = RHI::MakeShared<FakeTexture>(overlayDesc);

        PresentationPass presentationPass;
        RHI::FramebufferPtr firstSlot =
            presentationPass.AcquireOverlayFramebuffer(device, 0, 2, firstTarget);
        assert(firstSlot);
        RHI::RenderPassPtr overlayRenderPass = presentationPass.GetOverlayRenderPass();
        assert(overlayRenderPass);
        auto* fakeOverlayRenderPass = static_cast<FakeRenderPass*>(overlayRenderPass.get());
        assert(fakeOverlayRenderPass->GetColorAttachmentCount() == 1);
        assert(fakeOverlayRenderPass->GetColorAttachmentFormat(0) == RHI::Format::R16G16B16A16_FLOAT);
        assert(fakeOverlayRenderPass->Desc.colorAttachments[0].loadOp == RHI::AttachmentLoadOp::Load);
        assert(fakeOverlayRenderPass->Desc.colorAttachments[0].storeOp == RHI::AttachmentStoreOp::Store);
        assert(!fakeOverlayRenderPass->Desc.colorAttachments[0].clear);
        assert(fakeDevice->CreatedFramebufferCount == 1);

        RHI::FramebufferPtr sameKey =
            presentationPass.AcquireOverlayFramebuffer(device, 0, 2, firstTarget);
        assert(sameKey.get() == firstSlot.get());
        assert(fakeDevice->CreatedFramebufferCount == 1);

        RHI::FramebufferPtr otherSlot =
            presentationPass.AcquireOverlayFramebuffer(device, 1, 2, firstTarget);
        assert(otherSlot);
        assert(otherSlot.get() != firstSlot.get());
        assert(fakeDevice->CreatedFramebufferCount == 2);

        Container::TWeakPtr<RHI::IFramebuffer> replacedWeak = firstSlot;
        RHI::FramebufferPtr replacement =
            presentationPass.AcquireOverlayFramebuffer(device, 0, 2, replacementTarget);
        assert(replacement);
        assert(replacement.get() != firstSlot.get());
        assert(fakeDevice->CreatedFramebufferCount == 3);
        firstSlot.reset();
        sameKey.reset();
        assert(replacedWeak.expired());

        Container::TWeakPtr<RHI::IRenderPass> renderPassWeak =
            presentationPass.GetOverlayRenderPass();
        overlayRenderPass.reset();
        presentationPass.InvalidateOverlayResources();
        assert(!presentationPass.GetOverlayRenderPass());
        replacement.reset();
        otherSlot.reset();
        assert(renderPassWeak.expired());

        std::cout << "TestPresentationOverlayFramebufferRingOwnsAndReusesByFrameSlotKey passed\n";
    }
} // namespace

int main()
{
    std::cout << "RenderGraphTextureUsageContractTest start\n";

    TestRenderTargetDescDoesNotIncludeTransferSrc();
    TestToneMappingGraphOutputIncludesTransferSrc();
    TestLightingSceneColorIncludesTransferSrcAndIsExported();
    TestLightingPersistentSceneColorIncludesTransferSrc();
    TestToneMappingPersistentOutputIncludesTransferSrc();
    TestFXAAGraphOutputIncludesTransferSrc();
    TestFXAAPersistentOutputIncludesTransferSrc();
    TestUpscaleFreshPresentationColorIncludesTransferSrc();
    TestUpscalePersistentOutputIncludesTransferSrc();
    TestCompositeAlphaOverFreshColorIncludesTransferSrc();
    TestCompositePassthroughPresentationKeepsImportedSceneWithoutTransferSrc();
    TestPresentationOverlayFramebufferRingOwnsAndReusesByFrameSlotKey();

    std::cout << "RenderGraphTextureUsageContractTest passed\n";
    return 0;
}
