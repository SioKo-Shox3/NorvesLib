#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/BloomPass.h"
#include "Rendering/FXAAPass.h"
#include "Rendering/ForwardPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/NeuralMaterialDecodePass.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/SSRPass.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/UpscalePass.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "Container/PointerTypes.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
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
        FakeTexture()
        {
            m_Desc.Width = 64;
            m_Desc.Height = 32;
            m_Desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            m_Desc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
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
        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }
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
        FakeBuffer()
            : m_Desc(256, RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::ShaderRead)
        {
        }

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
        Container::VariableArray<BarrierEvent> Barriers;
        uint32_t BeginRenderPassCount = 0;
        uint32_t EndRenderPassCount = 0;
        uint32_t DrawCallCount = 0;

        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            assert(renderPass);
            assert(framebuffer);
            ++BeginRenderPassCount;
        }
        void EndRenderPass() override { ++EndRenderPassCount; }
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
            ++DrawCallCount;
        }
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
        {
            (void)vertexCount;
            (void)startVertexLocation;
            ++DrawCallCount;
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
            ++DrawCallCount;
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
            ++DrawCallCount;
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
            ++DrawCallCount;
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
            ++DrawCallCount;
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

    class FakeRenderPass final : public RHI::IRenderPass
    {
    public:
        explicit FakeRenderPass(const RHI::RenderPassDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(m_Desc.colorAttachments.size());
        }

        bool HasDepthStencilAttachment() const override
        {
            return m_Desc.hasDepthStencil;
        }

        RHI::Format GetColorAttachmentFormat(uint32_t index) const override
        {
            return index < m_Desc.colorAttachments.size()
                       ? m_Desc.colorAttachments[index].format
                       : RHI::Format::UNKNOWN;
        }

        RHI::Format GetDepthStencilFormat() const override
        {
            return m_Desc.depthStencilAttachment.format;
        }

    private:
        RHI::RenderPassDesc m_Desc;
    };

    class FakeFramebuffer final : public RHI::IFramebuffer
    {
    public:
        explicit FakeFramebuffer(const RHI::FramebufferDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.width; }
        uint32_t GetHeight() const override { return m_Desc.height; }
        RHI::RenderPassPtr GetRenderPass() const override { return m_Desc.renderPass; }
        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            return index < m_Desc.colorTargets.size() ? m_Desc.colorTargets[index] : nullptr;
        }
        RHI::TexturePtr GetDepthStencilAttachment() const override { return m_Desc.depthStencilTarget; }
        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(m_Desc.colorTargets.size());
        }
        bool HasDepthStencilAttachment() const override { return m_Desc.depthStencilTarget != nullptr; }

    private:
        RHI::FramebufferDesc m_Desc;
    };

    class FakeShader final : public RHI::IShader
    {
    public:
        explicit FakeShader(const RHI::ShaderDesc& desc)
            : m_Desc(desc)
        {
        }

        RHI::ShaderStage GetStage() const override { return m_Desc.stage; }
        Container::String GetEntryPoint() const override { return m_Desc.entryPoint; }
        const Container::VariableArray<uint8_t>& GetByteCode() const override
        {
            return m_Desc.byteCode;
        }

    private:
        RHI::ShaderDesc m_Desc;
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        explicit FakeSampler(const RHI::SamplerDesc& desc)
            : m_Desc(desc)
        {
        }

        RHI::FilterMode GetFilterMin() const override { return m_Desc.filterMin; }
        RHI::FilterMode GetFilterMag() const override { return m_Desc.filterMag; }
        RHI::FilterMode GetFilterMip() const override { return m_Desc.filterMip; }
        RHI::TextureAddressMode GetAddressModeU() const override { return m_Desc.addressU; }
        RHI::TextureAddressMode GetAddressModeV() const override { return m_Desc.addressV; }
        RHI::TextureAddressMode GetAddressModeW() const override { return m_Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return m_Desc.maxAnisotropy; }
        RHI::CompareFunc GetCompareFunc() const override { return m_Desc.compareFunc; }

    private:
        RHI::SamplerDesc m_Desc;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        FakePipeline(RHI::PipelineType type, uint32_t bindPointCount)
            : m_Type(type), m_BindPointCount(bindPointCount)
        {
        }

        RHI::PipelineType GetPipelineType() const override { return m_Type; }
        uint32_t GetBindPointCount() const override { return m_BindPointCount; }

    private:
        RHI::PipelineType m_Type = RHI::PipelineType::Graphics;
        uint32_t m_BindPointCount = 0;
    };

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        void BindConstantBuffer(uint32_t binding,
                                RHI::BufferPtr buffer,
                                uint32_t offset,
                                uint32_t size) override
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

        void BindStorageBuffer(uint32_t binding,
                               RHI::BufferPtr buffer,
                               uint32_t offset,
                               uint32_t size) override
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

    class FakeShaderCompiler final : public RHI::IShaderCompiler
    {
    public:
        RHI::ShaderCompileResult CompileFromSource(const Container::String& source,
                                                   RHI::ShaderStage stage,
                                                   const Container::String& filename = "shader",
                                                   const Container::String& entryPoint = "main") override
        {
            (void)source;
            (void)stage;
            (void)filename;
            (void)entryPoint;
            return MakeResult();
        }

        RHI::ShaderCompileResult CompileFromFile(const Container::String& filePath,
                                                 RHI::ShaderStage stage,
                                                 const Container::String& entryPoint = "main") override
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

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            return RHI::MakeShared<FakeBuffer>(desc);
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return RHI::MakeShared<FakeTexture>(desc);
        }

        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
        {
            return RHI::MakeShared<FakeSampler>(desc);
        }

        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc& desc) override
        {
            return RHI::MakeShared<FakeShader>(desc);
        }

        RHI::CommandListPtr CreateCommandList() override
        {
            return RHI::MakeShared<FakeCommandList>();
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
            return RHI::MakeShared<FakeFramebuffer>(desc);
        }

        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) override
        {
            return RHI::MakeShared<FakePipeline>(RHI::PipelineType::Graphics,
                                                static_cast<uint32_t>(desc.descriptorSetLayouts.size()));
        }

        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc& desc) override
        {
            return RHI::MakeShared<FakePipeline>(RHI::PipelineType::Compute,
                                                static_cast<uint32_t>(desc.descriptorSetLayouts.size()));
        }

        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc& desc) override
        {
            (void)desc;
            return RHI::MakeShared<FakeDescriptorSet>();
        }

        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return RHI::MakeShared<FakeShaderCompiler>();
        }

        RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }

        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::Vulkan; }
        const RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return m_Capabilities;
        }

        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

    private:
        RHI::DeviceCapabilities m_Capabilities;
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
            auto texture = Container::MakeUnique<FakeTexture>(desc);

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

    class ContextProbePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "ContextProbePass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            assert(context);
            m_Width = context->GetActiveRenderWidth();
            m_Height = context->GetActiveRenderHeight();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        uint32_t GetWidth() const
        {
            return m_Width;
        }

        uint32_t GetHeight() const
        {
            return m_Height;
        }

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };

    class PublishSceneDepthAliasPass final : public IRenderGraphPass
    {
    public:
        explicit PublishSceneDepthAliasPass(RGResourceHandle* publishedHandle)
            : m_PublishedHandle(publishedHandle)
        {
        }

        const char* GetName() const override { return "PublishSceneDepthAliasPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle sceneDepth = builder.CreateTextureHandle(
                RGTextureDesc::DepthStencil(32, 16, RHI::Format::D32_FLOAT, "PrepublishedSceneDepth"));
            assert(sceneDepth.IsValid());
            assert(builder.PublishTexture(RenderGraphResourceNames::SceneDepth, sceneDepth));
            if (m_PublishedHandle)
            {
                *m_PublishedHandle = sceneDepth.ToResourceHandle();
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_PublishedHandle = nullptr;
    };

    class FinalStateProducerPass final : public IRenderGraphPass
    {
    public:
        explicit FinalStateProducerPass(RGResourceHandle* textureHandle)
            : m_TextureHandle(textureHandle)
        {
        }

        const char* GetName() const override { return "FinalStateProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.CreateTexture(
                RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, "FinalStateTexture"));
            builder.Write(*m_TextureHandle,
                          RHI::ResourceState::RenderTarget,
                          RHI::ResourceState::ShaderResource);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
    };

    class ShaderReadConsumerPass final : public IRenderGraphPass
    {
    public:
        explicit ShaderReadConsumerPass(RGResourceHandle* textureHandle)
            : m_TextureHandle(textureHandle)
        {
        }

        const char* GetName() const override { return "ShaderReadConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(*m_TextureHandle, RHI::ResourceState::ShaderResource);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
    };

    class LegacyFXAAProducerPass final : public FXAAPass
    {
    public:
        const char* GetName() const override { return "LegacyFXAAProducerPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
            const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

            m_OutputTextureHandle = builder.CreateTextureHandle(
                RGTextureDesc::RenderTarget(width, height, RHI::Format::R8G8B8A8_UNORM, "LegacyFXAAOutput"));
            m_OutputHandle = m_OutputTextureHandle.ToResourceHandle();
            builder.Write(m_OutputHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
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

        LogicalPass producer(0, &executed);
        producer.m_CreateTarget = &resource;
        LogicalPass reader(1, &executed);
        reader.m_ReadA = &resource;
        LogicalPass laterWrite(2, &executed);
        laterWrite.m_WriteExisting = &resource;

        graph.AddPass(&producer);
        graph.AddPass(&reader);
        graph.AddPass(&laterWrite);

        assert(graph.Compile());
        AssertOrder(graph.GetCompiledPassOrder(), 0, 1, 2);
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

    void TestCompileContextPassedToDeclare()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ContextProbePass pass;
        graph.AddPass(&pass);

        ViewRenderContext context;
        context.RenderWidth = 320;
        context.RenderHeight = 180;

        assert(graph.Compile(context));
        assert(pass.GetWidth() == 320);
        assert(pass.GetHeight() == 180);
    }

    void TestWriteFinalStateSuppressesFollowupReadBarrier()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle textureHandle;
        FinalStateProducerPass producer(&textureHandle);
        ShaderReadConsumerPass consumer(&textureHandle);
        graph.AddPass(&producer);
        graph.AddPass(&consumer);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 1);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[0].CompiledOrderIndex == 0);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    void TestShadowMapNativeDeclareImportsDepthOutput()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;

        ShadowMapPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(pass.GetShadowMapHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        assert(graph.TryGetDeclaredPassAccess(passIndex, 0, resource, mode, state, finalState));
        assert(resource == pass.GetShadowMapHandle());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::DepthWrite);
        assert(finalState == RHI::ResourceState::ShaderResource);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr shadowMap = graphResources.GetTexture(pass.GetShadowMapHandle());
        assert(shadowMap.get() == pass.GetShadowMapTexture());
        assert(graph.GetCompiledBarriers().empty());

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestShadowMapNativeExecuteRegistersBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;
        context.SharedResources = &sharedResources;

        ShadowMapPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr shadowMap = graphResources.GetTexture(pass.GetShadowMapHandle());
        assert(shadowMap.get() == pass.GetShadowMapTexture());
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ShadowMap"));
        assert(sharedResources.GetTexturePtr("ShadowMap").get() == pass.GetShadowMapTexture());
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestNeuralDecodeNativeDeclareWritesLogicalCompletion()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        NeuralMaterialDecodePass pass;
        const uint32_t passIndex = graph.AddPass(&pass);

        ViewRenderContext context;
        assert(graph.Compile(context));
        assert(pass.GetDecodeCompleteHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        assert(graph.TryGetDeclaredPassAccess(passIndex, 0, resource, mode, state, finalState));
        assert(resource == pass.GetDecodeCompleteHandle());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::Common);
        assert(finalState == RHI::ResourceState::Common);
        assert(graph.GetCompiledBarriers().empty());
    }

    void TestNeuralDecodeNativeExecuteSkipsUnsupportedPath()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        RHI::DeviceCapabilities capabilities;
        capabilities.NeuralShaders.bSupported = false;

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.Capabilities = &capabilities;

        NeuralMaterialDecodePass pass;
        assert(pass.Initialize(context));
        assert(!pass.IsCooperativeVectorSupported());

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
    }

    void TestMegaGeometryNativeDeclareImportsPersistentBuffers()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;

        MegaGeometryPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(pass.GetIndirectDrawBufferHandle().IsValid());
        assert(pass.GetDrawCountBufferHandle().IsValid());
        assert(pass.GetMegaGeometryCompleteHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 3);

        bool bHasIndirectBufferWrite = false;
        bool bHasDrawCountBufferWrite = false;
        bool bHasCompleteWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(passIndex, accessIndex, resource, mode, state, finalState));
            assert(mode == RGAccessMode::Write);
            assert(state == RHI::ResourceState::Common);
            assert(finalState == RHI::ResourceState::Common);

            if (resource == pass.GetIndirectDrawBufferHandle())
            {
                bHasIndirectBufferWrite = true;
            }
            else if (resource == pass.GetDrawCountBufferHandle())
            {
                bHasDrawCountBufferWrite = true;
            }
            else if (resource == pass.GetMegaGeometryCompleteHandle())
            {
                bHasCompleteWrite = true;
            }
        }

        RenderGraphResources graphResources(&graph);
        assert(graphResources.GetBuffer(pass.GetIndirectDrawBufferHandle()));
        assert(graphResources.GetBuffer(pass.GetDrawCountBufferHandle()));
        assert(bHasIndirectBufferWrite);
        assert(bHasDrawCountBufferWrite);
        assert(bHasCompleteWrite);
        assert(graph.GetCompiledBarriers().empty());

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeExecuteSkipsWhenNoInstances()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;

        MegaGeometryPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestGBufferNativeDeclareCreatesTransientOutputs()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass pass;
        graph.AddPass(&pass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(pass.GetAlbedoHandle().IsValid());
        assert(pass.GetNormalHandle().IsValid());
        assert(pass.GetMaterialHandle().IsValid());
        assert(pass.GetEmissiveHandle().IsValid());
        assert(pass.GetDepthHandle().IsValid());

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 5);
        for (uint32_t i = 0; i < 4; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == 0);
            assert(barriers[i].CompiledOrderIndex == 0);
        }
        assert(barriers[4].Kind == RGBarrierKind::Texture);
        assert(barriers[4].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[4].AfterState == RHI::ResourceState::DepthWrite);
        assert(barriers[4].PassIndex == 0);
        assert(barriers[4].CompiledOrderIndex == 0);
    }

    void TestGBufferSSAONativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(gbufferPass.GetDepthHandle().IsValid());
        assert(gbufferPass.GetNormalHandle().IsValid());
        assert(ssaoPass.GetSSAORawHandle().IsValid());
        assert(ssaoPass.GetSSAOBlurredHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssaoPassIndex) == 4);

        bool bHasDepthRead = false;
        bool bHasNormalRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(ssaoPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Write;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(ssaoPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }

            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
        }
        assert(bHasDepthRead);
        assert(bHasNormalRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 7);
        for (uint32_t i = 0; i < 4; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == gbufferPassIndex);
            assert(barriers[i].CompiledOrderIndex == 0);
        }
        assert(barriers[4].Kind == RGBarrierKind::Texture);
        assert(barriers[4].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[4].AfterState == RHI::ResourceState::DepthWrite);
        assert(barriers[4].PassIndex == gbufferPassIndex);
        assert(barriers[4].CompiledOrderIndex == 0);

        assert(barriers[5].Kind == RGBarrierKind::Texture);
        assert(barriers[5].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[5].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[5].PassIndex == ssaoPassIndex);
        assert(barriers[5].CompiledOrderIndex == 1);
        assert(barriers[6].Kind == RGBarrierKind::Texture);
        assert(barriers[6].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[6].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[6].PassIndex == ssaoPassIndex);
        assert(barriers[6].CompiledOrderIndex == 1);
    }

    void TestGBufferSSAOLightingNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(lightingPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 7);

        bool bHasAlbedoRead = false;
        bool bHasNormalRead = false;
        bool bHasMaterialRead = false;
        bool bHasDepthRead = false;
        bool bHasEmissiveRead = false;
        bool bHasSSAORead = false;
        bool bHasSceneColorWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(lightingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(lightingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == gbufferPass.GetAlbedoHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasAlbedoRead = true;
            }
            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
            if (resource == gbufferPass.GetMaterialHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasMaterialRead = true;
            }
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }
            if (resource == gbufferPass.GetEmissiveHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasEmissiveRead = true;
            }
            if (resource == ssaoPass.GetSSAOBlurredHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSSAORead = true;
            }
            if (resource == lightingPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorWrite = true;
            }
        }

        assert(bHasAlbedoRead);
        assert(bHasNormalRead);
        assert(bHasMaterialRead);
        assert(bHasDepthRead);
        assert(bHasEmissiveRead);
        assert(bHasSSAORead);
        assert(bHasSceneColorWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 8);
        assert(barriers[7].Kind == RGBarrierKind::Texture);
        assert(barriers[7].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[7].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[7].PassIndex == lightingPassIndex);
        assert(barriers[7].CompiledOrderIndex == 2);
    }

    void TestGBufferSSAOLightingNativeDeclareUsesNamedResourcesWithoutPassPointers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(gbufferPass.GetDepthHandle().IsValid());
        assert(gbufferPass.GetNormalHandle().IsValid());
        assert(ssaoPass.GetSSAOBlurredHandle().IsValid());
        assert(lightingPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssaoPassIndex) == 4);
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 7);

        auto hasShaderReadAccess = [&graph](uint32_t passIndex, RGResourceHandle expected) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Write;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState));
                if (resource == expected)
                {
                    return mode == RGAccessMode::Read &&
                           state == RHI::ResourceState::ShaderResource &&
                           finalState == RHI::ResourceState::ShaderResource;
                }
            }

            return false;
        };

        assert(hasShaderReadAccess(ssaoPassIndex, gbufferPass.GetDepthHandle()));
        assert(hasShaderReadAccess(ssaoPassIndex, gbufferPass.GetNormalHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetAlbedoHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetNormalHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetMaterialHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetDepthHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetEmissiveHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, ssaoPass.GetSSAOBlurredHandle()));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
    }

    void TestLightingSceneDepthAliasDuplicateFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        RGResourceHandle prepublishedSceneDepth;
        PublishSceneDepthAliasPass prepublishPass(&prepublishedSceneDepth);
        LightingPass lightingPass;

        graph.AddPass(&gbufferPass);
        graph.AddPass(&prepublishPass);
        graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(!graph.Compile(context));
        assert(prepublishedSceneDepth.IsValid());
    }

    void TestForwardTransparentNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(forwardPassIndex) == 2);

        bool bHasSceneColorLoadStore = false;
        bool bHasDepthRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(forwardPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            bool bColorAttachmentLoadStore = false;
            RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
            assert(graph.TryGetDeclaredPassAccess(forwardPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState,
                                                  &bColorAttachmentLoadStore,
                                                  &loadOp,
                                                  &storeOp));

            if (resource == lightingPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                assert(bColorAttachmentLoadStore);
                assert(loadOp == RHI::AttachmentLoadOp::Load);
                assert(storeOp == RHI::AttachmentStoreOp::Store);
                bHasSceneColorLoadStore = true;
            }

            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::DepthRead);
                assert(finalState == RHI::ResourceState::DepthRead);
                assert(!bColorAttachmentLoadStore);
                bHasDepthRead = true;
            }
        }

        assert(bHasSceneColorLoadStore);
        assert(bHasDepthRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 10);
        assert(barriers[8].Kind == RGBarrierKind::Texture);
        assert(barriers[8].Resource == lightingPass.GetSceneColorHandle());
        assert(barriers[8].BeforeState == RHI::ResourceState::ShaderResource);
        assert(barriers[8].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[8].PassIndex == forwardPassIndex);
        assert(barriers[8].CompiledOrderIndex == 3);
        assert(barriers[9].Kind == RGBarrierKind::Texture);
        assert(barriers[9].Resource == gbufferPass.GetDepthHandle());
        assert(barriers[9].BeforeState == RHI::ResourceState::ShaderResource);
        assert(barriers[9].AfterState == RHI::ResourceState::DepthRead);
        assert(barriers[9].PassIndex == forwardPassIndex);
        assert(barriers[9].CompiledOrderIndex == 3);
    }

    void TestSSRNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(ssrPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssrPassIndex) == 5);

        bool bHasNormalRead = false;
        bool bHasMaterialRead = false;
        bool bHasDepthRead = false;
        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(ssrPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(ssrPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
            if (resource == gbufferPass.GetMaterialHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasMaterialRead = true;
            }
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }
            if (resource == lightingPass.GetSceneColorHandle() && mode == RGAccessMode::Read)
            {
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }
            if (resource == ssrPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasNormalRead);
        assert(bHasMaterialRead);
        assert(bHasDepthRead);
        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 5);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);

        bool bHasDepthToShaderResource = false;
        bool bHasSSROutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != ssrPassIndex)
            {
                continue;
            }

            if (barrier.Resource == gbufferPass.GetDepthHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::DepthRead);
                assert(barrier.AfterState == RHI::ResourceState::ShaderResource);
                bHasDepthToShaderResource = true;
            }

            if (barrier.Resource == ssrPass.GetSceneColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasSSROutputWrite = true;
            }
        }

        assert(bHasDepthToShaderResource);
        assert(bHasSSROutputWrite);
    }

    void TestBloomNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(bloomPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(bloomPassIndex) == 2);

        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(bloomPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(bloomPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == ssrPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }

            if (resource == bloomPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 6);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);

        bool bHasBloomOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != bloomPassIndex)
            {
                continue;
            }

            if (barrier.Resource == bloomPass.GetSceneColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasBloomOutputWrite = true;
            }
        }

        assert(bHasBloomOutputWrite);
    }

    void TestToneMappingNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(toneMappingPassIndex) == 2);

        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(toneMappingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(toneMappingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == bloomPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 7);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);

        bool bHasToneMappedOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != toneMappingPassIndex)
            {
                continue;
            }

            if (barrier.Resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasToneMappedOutputWrite = true;
            }
        }

        assert(bHasToneMappedOutputWrite);
    }

    void TestPostProcessNativeDeclareUsesNamedResourcesWithoutPassPointers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        SSRPass ssrPass;
        BloomPass bloomPass;
        ToneMappingPass toneMappingPass;

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(forwardPassIndex) == 2);
        assert(graph.GetDeclaredPassAccessCount(ssrPassIndex) == 5);
        assert(graph.GetDeclaredPassAccessCount(bloomPassIndex) == 2);
        assert(graph.GetDeclaredPassAccessCount(toneMappingPassIndex) == 2);

        auto hasAccess = [&graph](uint32_t passIndex,
                                  RGResourceHandle expected,
                                  RGAccessMode expectedMode,
                                  RHI::ResourceState expectedState,
                                  RHI::ResourceState expectedFinalState) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Read;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState));
                if (resource == expected)
                {
                    return mode == expectedMode &&
                           state == expectedState &&
                           finalState == expectedFinalState;
                }
            }

            return false;
        };

        assert(hasAccess(forwardPassIndex,
                         lightingPass.GetSceneColorHandle(),
                         RGAccessMode::Write,
                         RHI::ResourceState::RenderTarget,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(ssrPassIndex,
                         lightingPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(bloomPassIndex,
                         ssrPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(toneMappingPassIndex,
                         bloomPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 7);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
    }

    void TestPostProcessMissingNamedInputsCompileWithoutErrors()
    {
        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            ForwardPass forwardPass(nullptr, nullptr);
            forwardPass.SetTransparentOnly(true);
            const uint32_t passIndex = graph.AddPass(&forwardPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 0);
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            SSRPass ssrPass;
            const uint32_t passIndex = graph.AddPass(&ssrPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(ssrPass.GetSceneColorHandle().IsValid());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            BloomPass bloomPass;
            const uint32_t passIndex = graph.AddPass(&bloomPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(bloomPass.GetSceneColorHandle().IsValid());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            ToneMappingPass toneMappingPass;
            const uint32_t passIndex = graph.AddPass(&toneMappingPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        }
    }

    void TestFXAANativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(fxaaPass.GetToneMappedColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(fxaaPassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(fxaaPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(fxaaPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 8);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
        assert(order[7] == fxaaPassIndex);

        bool bHasFXAAOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != fxaaPassIndex)
            {
                continue;
            }

            if (barrier.Resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasFXAAOutputWrite = true;
            }
        }

        assert(bHasFXAAOutputWrite);
    }

    void TestUpscaleNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(upscalePass.GetPresentationColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(upscalePassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasPresentationWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == upscalePass.GetPresentationColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasPresentationWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasPresentationWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 9);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
        assert(order[7] == fxaaPassIndex);
        assert(order[8] == upscalePassIndex);

        RGCompiledResourceLifetime presentationLifetime;
        assert(graph.TryGetCompiledResourceLifetime(upscalePass.GetPresentationColorHandle(),
                                                    presentationLifetime));
        assert(presentationLifetime.bExported);
        assert(presentationLifetime.bPinnedUntilGraphEnd);
        assert(presentationLifetime.LifetimeEndOrderIndex == graph.GetCompiledPassOrder().size());

        bool bHasPresentationOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != upscalePassIndex)
            {
                continue;
            }

            if (barrier.Resource == upscalePass.GetPresentationColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasPresentationOutputWrite = true;
            }
        }

        assert(bHasPresentationOutputWrite);
    }

    void TestGBufferSSAOLightingNativeExecuteWithoutSharedResources()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr sceneColorTexture = graphResources.GetTexture(lightingPass.GetSceneColorHandle());
        assert(sceneColorTexture);
        assert(graph.GetLastExecutedPassCount() == 3);
        assert(result.TextureOutputs.empty());
        RHI::TexturePtr exportedTexture;
        assert(!result.TryGetTexture(RenderGraphResourceNames::SceneColor, exportedTexture));
        assert(exportedTexture == nullptr);
        assert(!result.TryGetTexture(RenderGraphResourceNames::SceneDepth, exportedTexture));
        assert(exportedTexture == nullptr);
        assert(commandList.Barriers.size() == 8);
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.EndRenderPassCount == 4);
        assert(commandList.DrawCallCount == 3);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestForwardTransparentNativeExecuteKeepsBridgeWhenDrawInputsMissing()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        transparentCommands.push_back(DrawCommand{});
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 4);
        assert(commandList.Barriers.size() == 10);
        assert(commandList.BeginRenderPassCount == 5);
        assert(commandList.EndRenderPassCount == 5);
        assert(commandList.DrawCallCount == 3);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.HasTexture("GBuffer_Depth"));
        assert(sharedResources.HasTexture("SceneDepth"));
        assert(sharedResources.GetTexturePtr("SceneColor") != nullptr);
        assert(sharedResources.GetTexturePtr("SceneDepth") != nullptr);

        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSRNativeExecuteUsesGraphSceneColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 5);
        assert(commandList.Barriers.size() == 12);
        assert(commandList.BeginRenderPassCount == 6);
        assert(commandList.EndRenderPassCount == 6);
        assert(commandList.DrawCallCount == 4);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("SceneColor"));
        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr ssrOutput = graphResources.GetTexture(ssrPass.GetSceneColorHandle());
        assert(ssrOutput);
        assert(sharedResources.GetTexturePtr("SceneColor").get() != ssrOutput.get());

        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestBloomNativeExecuteUsesGraphSceneColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr bloomOutput = graphResources.GetTexture(bloomPass.GetSceneColorHandle());
        assert(bloomOutput);

        assert(graph.GetLastExecutedPassCount() == 6);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() != bloomOutput.get());

        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestToneMappingNativeExecuteExportsToneMappedColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr toneMappedOutput = graphResources.GetTexture(toneMappingPass.GetToneMappedColorHandle());
        assert(toneMappedOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == toneMappedOutput.get());

        assert(graph.GetLastExecutedPassCount() == 7);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestFXAANativeExecuteExportsToneMappedColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 8);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr presentationOutput = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == presentationOutput.get());

        assert(graph.GetLastExecutedPassCount() == 9);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("PresentationColor"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSRNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SSRPass ssrPass;
        graph.AddPass(&ssrPass);

        RHI::TexturePtr normalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal"));
        RHI::TexturePtr materialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackMaterial"));
        RHI::TexturePtr depthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth"));
        RHI::TexturePtr sceneColorTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackSceneColor"));
        assert(normalTexture);
        assert(materialTexture);
        assert(depthTexture);
        assert(sceneColorTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", materialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("SceneColor", sceneColorTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr ssrOutput = graphResources.GetTexture(ssrPass.GetSceneColorHandle());
        assert(ssrOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() == ssrOutput.get());

        ssrPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestBloomNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        BloomPass bloomPass;
        graph.AddPass(&bloomPass);

        RHI::TexturePtr sceneColorTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackSceneColor"));
        assert(sceneColorTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("SceneColor", sceneColorTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr bloomOutput = graphResources.GetTexture(bloomPass.GetSceneColorHandle());
        assert(bloomOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() == bloomOutput.get());

        bloomPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestFXAANativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        FXAAPass fxaaPass;
        graph.AddPass(&fxaaPass);

        RHI::TexturePtr toneMappedTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackToneMappedColor"));
        assert(toneMappedTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ToneMappedColor", toneMappedTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == fxaaOutput.get());

        fxaaPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationAliasWhenNoUpscale()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 128;
        context.ScreenHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr presentationOutput = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationOutput.get() == fxaaOutput.get());
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 9);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("PresentationColor"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationAliasFromInputPassFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LegacyFXAAProducerPass legacyFXAAPass;
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&legacyFXAAPass);

        graph.AddPass(&legacyFXAAPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 128;
        context.ScreenHeight = 64;

        assert(graph.Compile(context));
        uint32_t toneMappedVersion = 0;
        assert(!graph.TryGetNamedResourceVersion(RenderGraphResourceNames::ToneMappedColor, toneMappedVersion));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr inputTexture = graphResources.GetTexture(legacyFXAAPass.GetToneMappedColorHandle());
        assert(inputTexture);
        RHI::TexturePtr presentationTexture = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationTexture.get() == inputTexture.get());
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == inputTexture.get());

        assert(graph.GetLastExecutedPassCount() == 2);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("PresentationColor"));
        assert(sharedResources.GetTexturePtr("PresentationColor").get() == inputTexture.get());

        upscalePass.Shutdown();
        legacyFXAAPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestToneMappingNativeExecuteAcceptsRawSceneColorBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        ToneMappingPass toneMappingPass;
        graph.AddPass(&toneMappingPass);

        RHI::TextureDesc rawSceneColorDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RawForwardSceneColor");
        RHI::TexturePtr rawSceneColor = device->CreateTexture(rawSceneColorDesc);
        assert(rawSceneColor);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexture("SceneColor", rawSceneColor.get());
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr toneMappedOutput = graphResources.GetTexture(toneMappingPass.GetToneMappedColorHandle());
        assert(toneMappedOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == toneMappedOutput.get());

        toneMappingPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteClearsWhenInputsMissingWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LightingPass lightingPass;
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 1);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        renderer.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteDoesNotPublishBridgeWhenFallbackInputsMissing()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LightingPass lightingPass;
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        RHI::TextureDesc albedoDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackAlbedo");
        RHI::TextureDesc normalDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal");
        RHI::TextureDesc depthDesc =
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth");
        RHI::TexturePtr albedoTexture = device->CreateTexture(albedoDesc);
        RHI::TexturePtr normalTexture = device->CreateTexture(normalDesc);
        RHI::TexturePtr depthTexture = device->CreateTexture(depthDesc);
        assert(albedoTexture);
        assert(normalTexture);
        assert(depthTexture);

        sharedResources.RegisterTexturePtr("GBuffer_Albedo", albedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 1);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("SceneDepth"));

        lightingPass.Shutdown();
        renderer.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestGBufferNativeExecuteClearsWhenOpaqueCommandsEmpty()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass pass;
        pass.SetSceneRenderer(&renderer);
        graph.AddPass(&pass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 5);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("GBuffer_Albedo"));
        assert(sharedResources.HasTexture("GBuffer_Normal"));
        assert(sharedResources.HasTexture("GBuffer_Material"));
        assert(sharedResources.HasTexture("GBuffer_Emissive"));
        assert(sharedResources.HasTexture("GBuffer_Depth"));

        pass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSAONativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SSAOPass ssaoPass;
        graph.AddPass(&ssaoPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        RHI::TextureDesc depthDesc =
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth");
        RHI::TextureDesc normalDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal");
        RHI::TexturePtr depthTexture = device->CreateTexture(depthDesc);
        RHI::TexturePtr normalTexture = device->CreateTexture(normalDesc);
        assert(depthTexture);
        assert(normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr blurredOutput = graphResources.GetTexture(ssaoPass.GetSSAOBlurredHandle());
        assert(blurredOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 2);
        assert(sharedResources.HasTexture("SSAO"));
        assert(sharedResources.GetTexturePtr("SSAO").get() == blurredOutput.get());

        ssaoPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
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
    TestCompileContextPassedToDeclare();
    TestWriteFinalStateSuppressesFollowupReadBarrier();
    TestShadowMapNativeDeclareImportsDepthOutput();
    TestNeuralDecodeNativeDeclareWritesLogicalCompletion();
    TestMegaGeometryNativeDeclareImportsPersistentBuffers();
    TestGBufferNativeDeclareCreatesTransientOutputs();
    TestGBufferSSAONativeDeclareDependencies();
    TestGBufferSSAOLightingNativeDeclareDependencies();
    TestGBufferSSAOLightingNativeDeclareUsesNamedResourcesWithoutPassPointers();
    TestLightingSceneDepthAliasDuplicateFailsCompile();
    TestForwardTransparentNativeDeclareDependencies();
    TestSSRNativeDeclareDependencies();
    TestBloomNativeDeclareDependencies();
    TestToneMappingNativeDeclareDependencies();
    TestPostProcessNativeDeclareUsesNamedResourcesWithoutPassPointers();
    TestPostProcessMissingNamedInputsCompileWithoutErrors();
    TestFXAANativeDeclareDependencies();
    TestUpscaleNativeDeclareDependencies();
    TestShadowMapNativeExecuteRegistersBridge();
    TestNeuralDecodeNativeExecuteSkipsUnsupportedPath();
    TestMegaGeometryNativeExecuteSkipsWhenNoInstances();
    TestGBufferNativeExecuteClearsWhenOpaqueCommandsEmpty();
    TestSSAONativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestGBufferSSAOLightingNativeExecuteWithoutSharedResources();
    TestLightingNativeExecuteClearsWhenInputsMissingWithoutBridge();
    TestLightingNativeExecuteDoesNotPublishBridgeWhenFallbackInputsMissing();
    TestForwardTransparentNativeExecuteKeepsBridgeWhenDrawInputsMissing();
    TestSSRNativeExecuteUsesGraphSceneColorWithoutBridge();
    TestBloomNativeExecuteUsesGraphSceneColorWithoutBridge();
    TestToneMappingNativeExecuteExportsToneMappedColorWithoutBridge();
    TestFXAANativeExecuteExportsToneMappedColorWithoutBridge();
    TestUpscaleNativeExecuteExportsPresentationColorWithoutBridge();
    TestSSRNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestBloomNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestFXAANativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestUpscaleNativeExecuteExportsPresentationAliasWhenNoUpscale();
    TestUpscaleNativeExecuteExportsPresentationAliasFromInputPassFallback();
    TestToneMappingNativeExecuteAcceptsRawSceneColorBridge();

    std::cout << "RenderGraphCompileTest passed\n";
    return 0;
}
