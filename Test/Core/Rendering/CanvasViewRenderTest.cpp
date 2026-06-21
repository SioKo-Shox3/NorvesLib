#include "Rendering/CanvasView.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
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
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;
namespace RHI = NorvesLib::RHI;

namespace
{
    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const RHI::TextureDesc &desc)
            : m_Desc(desc)
        {
        }

        explicit FakeTexture(const char *name, uint32_t width = 64, uint32_t height = 32)
            : m_Desc(RHI::TextureDesc::RenderTarget(width, height, RHI::Format::R8G8B8A8_UNORM, name))
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

        void Update(const void *data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            LastRowPitch = rowPitch;
            LastSlicePitch = slicePitch;
            LastMipLevel = mipLevel;
            LastArrayIndex = arrayIndex;
            ++UpdateCount;
        }

        uint32_t UpdateCount = 0;
        uint32_t LastRowPitch = 0;
        uint32_t LastSlicePitch = 0;
        uint32_t LastMipLevel = 0;
        uint32_t LastArrayIndex = 0;

    private:
        RHI::TextureDesc m_Desc;
    };

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const RHI::BufferDesc &desc)
            : m_Desc(desc)
        {
        }

        uint64_t GetSize() const override { return m_Desc.Size; }

        void *Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)offset;
            (void)size;
            return nullptr;
        }

        void Unmap() override {}

        void Update(const void *data, uint64_t size, uint64_t offset = 0) override
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
        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc &desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            RHI::BufferPtr buffer = Container::MakeShared<FakeBuffer>(desc);
            Buffers.push_back(buffer);

            RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Size = desc.Size;
            allocation.Type = type;
            return allocation;
        }

        void FreeBuffer(RHI::BufferAllocation &allocation) override
        {
            allocation.Buffer = nullptr;
            allocation.Size = 0;
        }

        RHI::TextureAllocation AllocateTexture(const RHI::TextureDesc &desc,
                                               RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            RHI::TexturePtr texture = Container::MakeShared<FakeTexture>(desc);
            Textures.push_back(texture);

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(desc.Height) * 4u;
            allocation.Type = type;
            return allocation;
        }

        void FreeTexture(RHI::TextureAllocation &allocation) override
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
        explicit FakeRenderPass(const RHI::RenderPassDesc &desc)
            : Desc(desc)
        {
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
        explicit FakeFramebuffer(const RHI::FramebufferDesc &desc)
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
        explicit FakeSampler(const RHI::SamplerDesc &desc)
            : Desc(desc)
        {
        }

        RHI::FilterMode GetFilterMin() const override { return Desc.filterMin; }
        RHI::FilterMode GetFilterMag() const override { return Desc.filterMag; }
        RHI::FilterMode GetFilterMip() const override { return Desc.filterMip; }
        RHI::TextureAddressMode GetAddressModeU() const override { return Desc.addressU; }
        RHI::TextureAddressMode GetAddressModeV() const override { return Desc.addressV; }
        RHI::TextureAddressMode GetAddressModeW() const override { return Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return Desc.maxAnisotropy; }
        RHI::CompareFunc GetCompareFunc() const override { return Desc.compareFunc; }

        RHI::SamplerDesc Desc;
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
            Textures[binding] = texture;
        }

        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            Samplers[binding] = sampler;
        }

        void BindStorageBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t offset, uint32_t size) override
        {
            StorageBuffers[binding] = buffer;
            StorageOffsets[binding] = offset;
            StorageSizes[binding] = size;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            StorageTextures[binding] = texture;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture, uint32_t mipLevel) override
        {
            StorageTextures[binding] = texture;
            StorageTextureMipLevels[binding] = mipLevel;
        }

        void Update() override
        {
            ++UpdateCount;
        }

        Container::UnorderedMap<uint32_t, RHI::TexturePtr> Textures;
        Container::UnorderedMap<uint32_t, RHI::SamplerPtr> Samplers;
        Container::UnorderedMap<uint32_t, RHI::BufferPtr> StorageBuffers;
        Container::UnorderedMap<uint32_t, uint32_t> StorageOffsets;
        Container::UnorderedMap<uint32_t, uint32_t> StorageSizes;
        Container::UnorderedMap<uint32_t, RHI::TexturePtr> StorageTextures;
        Container::UnorderedMap<uint32_t, uint32_t> StorageTextureMipLevels;
        uint32_t UpdateCount = 0;
    };

    class FakeShaderCompiler final : public RHI::IShaderCompiler
    {
    public:
        RHI::ShaderCompileResult CompileFromSource(const Container::String &source,
                                                   RHI::ShaderStage stage,
                                                   const Container::String &filename = "shader",
                                                   const Container::String &entryPoint = "main") override
        {
            (void)source;
            (void)stage;
            (void)filename;
            (void)entryPoint;
            return MakeResult();
        }

        RHI::ShaderCompileResult CompileFromFile(const Container::String &filePath,
                                                 RHI::ShaderStage stage,
                                                 const Container::String &entryPoint = "main") override
        {
            (void)filePath;
            (void)stage;
            (void)entryPoint;
            return MakeResult();
        }

    private:
        RHI::ShaderCompileResult MakeResult() const
        {
            RHI::ShaderCompileResult result;
            result.bSuccess = true;
            result.ByteCode.push_back(0x03);
            result.ByteCode.push_back(0x02);
            result.ByteCode.push_back(0x23);
            result.ByteCode.push_back(0x07);
            return result;
        }
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

        void EndRenderPass() override
        {
            ++EndRenderPassCount;
        }

        void SetViewport(const RHI::Viewport &viewport) override
        {
            LastViewport = viewport;
        }

        void SetScissor(const RHI::ScissorRect &scissor) override
        {
            LastScissor = scissor;
        }

        void SetPipeline(RHI::PipelinePtr pipeline) override
        {
            BoundPipelines.push_back(pipeline);
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
            DescriptorSets.push_back(descriptorSet);
            DescriptorSetSlots.push_back(slot);
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
            DrawInstancedCalls.push_back(
                DrawInstancedCall{vertexCount, instanceCount, startVertexLocation, startInstanceLocation});
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

        struct DrawInstancedCall
        {
            uint32_t VertexCount = 0;
            uint32_t InstanceCount = 0;
            uint32_t StartVertexLocation = 0;
            uint32_t StartInstanceLocation = 0;
        };

        RHI::RenderPassPtr LastRenderPass;
        RHI::FramebufferPtr LastFramebuffer;
        RHI::Viewport LastViewport;
        RHI::ScissorRect LastScissor;
        Container::VariableArray<RHI::PipelinePtr> BoundPipelines;
        Container::VariableArray<RHI::DescriptorSetPtr> DescriptorSets;
        Container::VariableArray<uint32_t> DescriptorSetSlots;
        Container::VariableArray<DrawInstancedCall> DrawInstancedCalls;
        uint32_t BeginRenderPassCount = 0;
        uint32_t EndRenderPassCount = 0;
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc &desc) override
        {
            return Allocator.AllocateBuffer(desc).Buffer ? Allocator.Buffers.back() : nullptr;
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc &desc) override
        {
            RHI::TexturePtr texture = Container::MakeShared<FakeTexture>(desc);
            CreatedTextures.push_back(texture);
            return texture;
        }

        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc &desc) override
        {
            RHI::SamplerPtr sampler = Container::MakeShared<FakeSampler>(desc);
            CreatedSamplers.push_back(sampler);
            LastSamplerDesc = desc;
            return sampler;
        }

        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc &desc) override
        {
            return Container::MakeShared<FakeShader>(desc.stage);
        }

        RHI::CommandListPtr CreateCommandList() override
        {
            return nullptr;
        }

        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc &desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc &desc) override
        {
            LastRenderPassDesc = desc;
            ++CreateRenderPassCount;
            return Container::MakeShared<FakeRenderPass>(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc &desc) override
        {
            LastFramebufferDesc = desc;
            ++CreateFramebufferCount;
            return Container::MakeShared<FakeFramebuffer>(desc);
        }

        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc &desc) override
        {
            LastGraphicsPipelineDescs.push_back(desc);
            ++CreateGraphicsPipelineCount;
            return Container::MakeShared<FakePipeline>();
        }

        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc &desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc &desc) override
        {
            LastDescriptorSetDescs.push_back(desc);
            RHI::DescriptorSetPtr descriptorSet = Container::MakeShared<FakeDescriptorSet>();
            CreatedDescriptorSets.push_back(descriptorSet);
            return descriptorSet;
        }

        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return Container::MakeShared<FakeShaderCompiler>();
        }

        RHI::IGPUResourceAllocator *GetResourceAllocator() override
        {
            return &Allocator;
        }

        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::None; }
        const RHI::DeviceCapabilities &GetCapabilities() const override { return Capabilities; }

        Math::Matrix4x4 AdjustProjectionForClipSpace(const Math::Matrix4x4 &projection,
                                                     bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        FakeAllocator Allocator;
        RHI::DeviceCapabilities Capabilities;
        RHI::RenderPassDesc LastRenderPassDesc;
        RHI::FramebufferDesc LastFramebufferDesc;
        RHI::SamplerDesc LastSamplerDesc;
        Container::VariableArray<RHI::TexturePtr> CreatedTextures;
        Container::VariableArray<RHI::SamplerPtr> CreatedSamplers;
        Container::VariableArray<RHI::GraphicsPipelineDesc> LastGraphicsPipelineDescs;
        Container::VariableArray<RHI::DescriptorSetDesc> LastDescriptorSetDescs;
        Container::VariableArray<RHI::DescriptorSetPtr> CreatedDescriptorSets;
        uint32_t CreateRenderPassCount = 0;
        uint32_t CreateFramebufferCount = 0;
        uint32_t CreateGraphicsPipelineCount = 0;
    };

    struct CanvasFixture
    {
        Container::TSharedPtr<FakeDevice> Device = Container::MakeShared<FakeDevice>();
        RenderResources Resources;
        RHI::TransientResourcePool Pool;
        RenderGraph Graph;
        ShaderManager ShaderManager;
        SceneRenderer Renderer;
        FakeCommandList CommandList;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        ViewRenderContext Context;

        CanvasFixture()
        {
            assert(Resources.Initialize(Container::StaticPointerCast<RHI::IDevice>(Device)));
            assert(Pool.Initialize(Device->GetResourceAllocator(), 1));
            Pool.BeginFrame(0);
            assert(Graph.Initialize(&Pool));
            Graph.BeginFrame(0);
            assert(ShaderManager.Initialize(Device.get(), "Assets/Shaders"));
            assert(Renderer.Initialize(Device.get(), nullptr, &Pool));

            Context.Device = Device.get();
            Context.Graph = &Graph;
            Context.CommandList = &CommandList;
            Context.TransientPool = &Pool;
            Context.PendingFrameCommands = &PendingFrameCommands;
            Context.Renderer = &Renderer;
            Context.ShaderMgr = &ShaderManager;
            Context.RenderWidth = 96;
            Context.RenderHeight = 54;
            Context.ScreenWidth = 96;
            Context.ScreenHeight = 54;
            Context.Resources.Gpu = &Resources.Gpu();
            Context.Resources.Textures = &Resources.Textures();
            Context.Resources.Materials = &Resources.Materials();
            Context.Resources.Meshes = &Resources.Meshes();
            Context.Resources.MegaGeometry = &Resources.MegaGeometry();
            Context.InstanceDataBuffer = Device->CreateBuffer(
                RHI::BufferDesc(sizeof(GPUSceneInstanceData) * 8u,
                                RHI::ResourceUsage::StorageBuffer,
                                false,
                                "CanvasViewRenderTest.InstanceData"));
        }

        ~CanvasFixture()
        {
            Renderer.Shutdown();
            ShaderManager.Shutdown();
            Graph.Shutdown();
            Pool.EndFrame();
            Pool.Shutdown();
            Resources.Shutdown();
        }
    };

    ViewportRenderPlan MakeViewportPlan()
    {
        ViewportRenderPlan plan;
        plan.Camera.CullingMask = RenderLayer::UI;
        plan.PixelRect.Width = 640.0f;
        plan.PixelRect.Height = 480.0f;
        plan.RenderWidth = 640;
        plan.RenderHeight = 480;
        return plan;
    }

    BoardProxy MakeBoardProxy(uint64_t componentId,
                              TextureHandle texture,
                              BlendMode blendMode,
                              uint32_t layerPriority,
                              uint32_t orderInLayer)
    {
        BoardProxy proxy;
        proxy.ComponentId = componentId;
        proxy.ObjectId = componentId + 100u;
        proxy.Texture = texture;
        proxy.BlendModeProp = blendMode;
        proxy.LayerPriority = layerPriority;
        proxy.OrderInLayer = orderInLayer;
        proxy.SortKey = BoardProxy::ComputeSortKey(layerPriority, orderInLayer);
        proxy.LayerMask = RenderLayer::UI;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.bVisible = true;
        proxy.Tint = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        return proxy;
    }

    FakeDescriptorSet *AsFakeDescriptorSet(const RHI::DescriptorSetPtr &descriptorSet)
    {
        return static_cast<FakeDescriptorSet *>(descriptorSet.get());
    }

    void AssertBoardDescriptorLayout(const RHI::DescriptorSetDesc &desc)
    {
        assert(desc.bindings.size() == 2);
        assert(desc.bindings[0].binding == 0);
        assert(desc.bindings[0].type == RHI::ResourceBindType::CombinedImageSampler);
        assert(desc.bindings[0].stages == RHI::ShaderStage::Pixel);
        assert(desc.bindings[1].binding == 7);
        assert(desc.bindings[1].type == RHI::ResourceBindType::StructuredBuffer);
        assert(desc.bindings[1].stages == RHI::ShaderStage::Vertex);
    }

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
        assert(viewport != nullptr);
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
        assert(frameOutput != nullptr);
        assert(frameOutput->GetWidth() == 96);
        assert(frameOutput->GetHeight() == 54);

        RHI::TexturePtr graphOutput;
        assert(fixture.Graph.TryGetLastOutputTexture(RenderGraphResourceNames::CanvasColor, graphOutput));
        assert(graphOutput.get() == frameOutput.get());
        assert(fixture.Context.CurrentGraphExecutionResult != nullptr);
        assert(fixture.Context.CurrentGraphExecutionResult->bSuccess);
        assert(!fixture.Context.bPresentationGraphPassHandled);

        assert(fixture.Device->CreateRenderPassCount == 1);
        assert(fixture.Device->CreateFramebufferCount == 1);
        assert(fixture.Device->LastRenderPassDesc.colorAttachments.size() == 1);
        const RHI::AttachmentDesc &colorAttachment = fixture.Device->LastRenderPassDesc.colorAttachments[0];
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

        assert(fixture.Device->LastFramebufferDesc.colorTargets.size() == 1);
        assert(fixture.Device->LastFramebufferDesc.colorTargets[0].get() == frameOutput.get());
        assert(fixture.Device->LastFramebufferDesc.width == 96);
        assert(fixture.Device->LastFramebufferDesc.height == 54);

        assert(fixture.CommandList.BeginRenderPassCount == 1);
        assert(fixture.CommandList.EndRenderPassCount == 1);
        assert(fixture.CommandList.LastRenderPass != nullptr);
        assert(fixture.CommandList.LastFramebuffer != nullptr);
        assert(fixture.CommandList.LastViewport.width == 96.0f);
        assert(fixture.CommandList.LastViewport.height == 54.0f);
        assert(fixture.CommandList.LastScissor.right == 96);
        assert(fixture.CommandList.LastScissor.bottom == 54);

        assert(canvas.GetRetainedBoardFrameResourceCount() == 1);
        canvas.Shutdown();
        assert(canvas.GetRetainedBoardFrameResourceCount() == 0);
        std::cout << "TestCanvasViewClearsTransparentAndExportsFrameOutput passed\n";
    }

    void TestCanvasViewBindsPerCommandBoardTexturesAndPreservesOrder()
    {
        CanvasFixture fixture;
        CanvasView canvas;
        ViewSettings settings;
        settings.Width = 1;
        settings.Height = 1;
        assert(canvas.Initialize(settings));

        const RHI::TexturePtr textureA = Container::MakeShared<FakeTexture>("BoardTextureA", 32, 32);
        const RHI::TexturePtr textureB = Container::MakeShared<FakeTexture>("BoardTextureB", 48, 48);
        const TextureHandle handleA = fixture.Resources.Textures().RegisterExternalTexture(textureA, "BoardTextureA");
        const TextureHandle handleB = fixture.Resources.Textures().RegisterExternalTexture(textureB, "BoardTextureB");
        assert(handleA.IsValid());
        assert(handleB.IsValid());

        canvas.UpdateBoardProxy(1001u, MakeBoardProxy(1001u, handleA, BlendMode::Translucent, 0u, 0u));
        canvas.UpdateBoardProxy(1002u, MakeBoardProxy(1002u, handleB, BlendMode::Additive, 0u, 1u));
        canvas.PrepareBoardDrawCommands(MakeViewportPlan());

        fixture.Context.SnapshotDrawCommands = DrawCommandView::FromArray(canvas.GetBoardDrawCommands());
        canvas.Render(fixture.Context);

        assert(fixture.Device->CreateGraphicsPipelineCount == 3);
        assert(fixture.Device->LastGraphicsPipelineDescs.size() == 3);
        for (const RHI::GraphicsPipelineDesc &pipelineDesc : fixture.Device->LastGraphicsPipelineDescs)
        {
            assert(pipelineDesc.descriptorSetLayouts.size() == 1);
            AssertBoardDescriptorLayout(pipelineDesc.descriptorSetLayouts[0]);
        }

        assert(fixture.Device->LastDescriptorSetDescs.size() == 2);
        AssertBoardDescriptorLayout(fixture.Device->LastDescriptorSetDescs[0]);
        AssertBoardDescriptorLayout(fixture.Device->LastDescriptorSetDescs[1]);

        assert(fixture.CommandList.DescriptorSets.size() == 2);
        assert(fixture.CommandList.DescriptorSetSlots.size() == 2);
        assert(fixture.CommandList.DescriptorSetSlots[0] == 0u);
        assert(fixture.CommandList.DescriptorSetSlots[1] == 0u);
        assert(fixture.CommandList.DrawInstancedCalls.size() == 2);
        assert(fixture.CommandList.DrawInstancedCalls[0].StartInstanceLocation == 0u);
        assert(fixture.CommandList.DrawInstancedCalls[1].StartInstanceLocation == 1u);

        auto *descriptorSet0 = AsFakeDescriptorSet(fixture.CommandList.DescriptorSets[0]);
        auto *descriptorSet1 = AsFakeDescriptorSet(fixture.CommandList.DescriptorSets[1]);
        assert(descriptorSet0 != nullptr);
        assert(descriptorSet1 != nullptr);
        assert(descriptorSet0->UpdateCount == 1u);
        assert(descriptorSet1->UpdateCount == 1u);
        assert(descriptorSet0->Textures[0].get() == textureA.get());
        assert(descriptorSet1->Textures[0].get() == textureB.get());
        assert(descriptorSet0->Samplers[0] != nullptr);
        assert(descriptorSet1->Samplers[0] != nullptr);
        assert(descriptorSet0->Samplers[0].get() == descriptorSet1->Samplers[0].get());
        assert(descriptorSet0->StorageBuffers[7].get() == fixture.Context.InstanceDataBuffer.get());
        assert(descriptorSet1->StorageBuffers[7].get() == fixture.Context.InstanceDataBuffer.get());
        assert(descriptorSet0->StorageSizes[7] == fixture.Context.InstanceDataBuffer->GetSize());
        assert(descriptorSet1->StorageSizes[7] == fixture.Context.InstanceDataBuffer->GetSize());

        fixture.Resources.Textures().ReleaseTexture(handleA);
        fixture.Resources.Textures().ReleaseTexture(handleB);

        canvas.Shutdown();
        std::cout << "TestCanvasViewBindsPerCommandBoardTexturesAndPreservesOrder passed\n";
    }

    void TestCanvasViewUsesFallbackWhiteTextureForInvalidBoardTexture()
    {
        CanvasFixture fixture;
        CanvasView canvas;
        ViewSettings settings;
        settings.Width = 1;
        settings.Height = 1;
        assert(canvas.Initialize(settings));

        canvas.UpdateBoardProxy(2001u, MakeBoardProxy(2001u, TextureHandle::Invalid(), BlendMode::Opaque, 0u, 0u));
        canvas.PrepareBoardDrawCommands(MakeViewportPlan());

        fixture.Context.SnapshotDrawCommands = DrawCommandView::FromArray(canvas.GetBoardDrawCommands());
        canvas.Render(fixture.Context);

        assert(fixture.CommandList.DescriptorSets.size() == 1);
        assert(fixture.CommandList.DrawInstancedCalls.size() == 1);
        assert(fixture.Device->CreatedTextures.size() >= 1);
        assert(fixture.Device->CreatedSamplers.size() >= 1);

        auto *descriptorSet = AsFakeDescriptorSet(fixture.CommandList.DescriptorSets[0]);
        assert(descriptorSet != nullptr);
        assert(descriptorSet->UpdateCount == 1u);
        assert(descriptorSet->Textures[0] != nullptr);
        assert(descriptorSet->Textures[0].get() == fixture.Device->CreatedTextures.back().get());
        assert(descriptorSet->Textures[0]->GetWidth() == 1u);
        assert(descriptorSet->Textures[0]->GetHeight() == 1u);
        assert(descriptorSet->Samplers[0] != nullptr);
        assert(descriptorSet->Samplers[0].get() == fixture.Device->CreatedSamplers.back().get());
        assert(descriptorSet->StorageBuffers[7].get() == fixture.Context.InstanceDataBuffer.get());

        auto *fallbackTexture = static_cast<FakeTexture *>(descriptorSet->Textures[0].get());
        assert(fallbackTexture->UpdateCount == 1u);
        assert(fixture.Device->LastSamplerDesc.filterMin == RHI::FilterMode::Point);
        assert(fixture.Device->LastSamplerDesc.filterMag == RHI::FilterMode::Point);
        assert(fixture.Device->LastSamplerDesc.filterMip == RHI::FilterMode::Point);
        assert(fixture.Device->LastSamplerDesc.addressU == RHI::TextureAddressMode::Clamp);
        assert(fixture.Device->LastSamplerDesc.addressV == RHI::TextureAddressMode::Clamp);
        assert(fixture.Device->LastSamplerDesc.addressW == RHI::TextureAddressMode::Clamp);

        canvas.Shutdown();
        std::cout << "TestCanvasViewUsesFallbackWhiteTextureForInvalidBoardTexture passed\n";
    }
} // namespace

int main()
{
    std::cout << "CanvasViewRenderTest start\n";

    TestCanvasViewClearsTransparentAndExportsFrameOutput();
    TestCanvasViewBindsPerCommandBoardTexturesAndPreservesOrder();
    TestCanvasViewUsesFallbackWhiteTextureForInvalidBoardTexture();

    std::cout << "CanvasViewRenderTest passed\n";
    return 0;
}
