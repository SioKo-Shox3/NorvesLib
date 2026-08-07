#include "Object/ResourceRegistry.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Rendering/FramePacket.h"
#include "Rendering/DirectionalShadowLightMatrices.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/Screen.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinnedMeshGpuStore.h"
#include "Rendering/VertexLayout.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/ShadowMapPass.h"
#include "Resource/SkinnedMeshResource.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ISwapChain.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/SubmissionSerialAllocator.h"
#include "RHI/Vulkan/VulkanSwapChain.h"
#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include "RHI/ITexture.h"

#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;
namespace RHI = NorvesLib::RHI;
namespace Skeletal = NorvesLib::Core::Skeletal;

namespace NorvesLib::Core::Rendering
{
    struct SkinnedRenderPathContractTestAccess
    {
        static bool PrepareGBuffer(GBufferPass& pass,
                                   ViewRenderContext& context,
                                   const DrawCommand& source,
                                   DrawCommand& outCommand)
        {
            return pass.TryPrepareSkinnedCommand(context, source, outCommand);
        }

        static bool PrepareShadow(ShadowMapPass& pass,
                                  ViewRenderContext& context,
                                  const DrawCommand& source,
                                  DrawCommand& outCommand)
        {
            return pass.TryPrepareSkinnedCommand(context, source, outCommand);
        }
    };
}

namespace
{
    constexpr float Epsilon = 0.0001f;

    Math::Vector3 TransformPointAsShaderColumnMajor(const float* matrix, float x, float y, float z)
    {
        return Math::Vector3(
            matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
            matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
            matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]);
    }

    Math::Vector3 TransformVectorAsShaderColumnMajor(const float* matrix, const Math::Vector3& vector)
    {
        return Math::Vector3(
            matrix[0] * vector.x + matrix[4] * vector.y + matrix[8] * vector.z,
            matrix[1] * vector.x + matrix[5] * vector.y + matrix[9] * vector.z,
            matrix[2] * vector.x + matrix[6] * vector.y + matrix[10] * vector.z);
    }

    Math::Vector3 NormalizeIfNonZero(const Math::Vector3& vector)
    {
        if (vector.LengthSquared() <= Epsilon)
        {
            return vector;
        }
        Math::Vector3 normalized = vector;
        normalized.Normalize();
        return normalized;
    }

    Animation::SkinnedVertexSample EvaluateUploadedShaderSemantics(
        const float* matrices,
        uint32_t boneCount,
        const Skeletal::SkeletalVertex& vertex)
    {
        const Math::Vector3 sourcePosition(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
        const Math::Vector3 sourceNormal(vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z);
        Math::Vector3 skinnedPosition = Math::Vector3::Zero;
        Math::Vector3 skinnedNormal = Math::Vector3::Zero;
        float totalWeight = 0.0f;
        for (uint32_t influenceIndex = 0; influenceIndex < 4; ++influenceIndex)
        {
            const uint32_t jointIndex = vertex.JointIndices[influenceIndex];
            const float weight = vertex.JointWeights[influenceIndex];
            if (weight <= 0.0f || jointIndex >= boneCount)
            {
                continue;
            }
            const float* positionMatrix = matrices + (2 + jointIndex * 2) * 16;
            const float* normalMatrix = positionMatrix + 16;
            skinnedPosition += TransformPointAsShaderColumnMajor(positionMatrix,
                                                                 sourcePosition.x,
                                                                 sourcePosition.y,
                                                                 sourcePosition.z) * weight;
            skinnedNormal += TransformVectorAsShaderColumnMajor(normalMatrix, sourceNormal) * weight;
            totalWeight += weight;
        }

        if (totalWeight <= Epsilon)
        {
            skinnedPosition = sourcePosition;
            skinnedNormal = sourceNormal;
        }
        else
        {
            skinnedPosition /= totalWeight;
            skinnedNormal /= totalWeight;
            skinnedNormal = NormalizeIfNonZero(skinnedNormal);
        }

        Animation::SkinnedVertexSample result;
        result.Position = TransformPointAsShaderColumnMajor(matrices,
                                                            skinnedPosition.x,
                                                            skinnedPosition.y,
                                                            skinnedPosition.z);
        result.Normal = NormalizeIfNonZero(
            TransformVectorAsShaderColumnMajor(matrices + 16, skinnedNormal));
        return result;
    }

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const RHI::BufferDesc& desc)
            : Desc(desc), Bytes(static_cast<size_t>(desc.Size))
        {
        }

        uint64_t GetSize() const override
        {
            return Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Bytes.size() ? Bytes.data() + static_cast<size_t>(offset) : nullptr;
        }

        void Unmap() override
        {
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            if (!data || offset + size > Bytes.size())
            {
                return;
            }
            std::memcpy(Bytes.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
        }

        RHI::ResourceUsage GetUsage() const override
        {
            return Desc.Usage;
        }

        RHI::BufferDesc Desc;
        Container::VariableArray<uint8_t> Bytes;
    };

    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const RHI::TextureDesc& desc)
            : Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }
        void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}

        RHI::TextureDesc Desc;
    };

    class FakeAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        RHI::BufferAllocation AllocateBuffer(
            const RHI::BufferDesc& desc,
            RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto buffer = Container::MakeShared<FakeBuffer>(desc);
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

        RHI::TextureAllocation AllocateTexture(
            const RHI::TextureDesc& desc,
            RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto texture = Container::MakeShared<FakeTexture>(desc);
            Textures.push_back(texture);
            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * desc.Height * 4u;
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

        Container::VariableArray<RHI::BufferPtr> Buffers;
        Container::VariableArray<RHI::TexturePtr> Textures;
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        explicit FakeSampler(const RHI::SamplerDesc& desc)
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

    class FakeShader final : public RHI::IShader
    {
    public:
        explicit FakeShader(RHI::ShaderStage stage)
            : Stage(stage)
        {
        }

        RHI::ShaderStage GetStage() const override { return Stage; }
        Container::String GetEntryPoint() const override { return "main"; }
        const Container::VariableArray<uint8_t>& GetByteCode() const override { return ByteCode; }

        RHI::ShaderStage Stage;
        Container::VariableArray<uint8_t> ByteCode;
    };

    class FakeShaderCompiler final : public RHI::IShaderCompiler
    {
    public:
        RHI::ShaderCompileResult CompileFromSource(const Container::String&,
                                                   RHI::ShaderStage,
                                                   const Container::String& = "shader",
                                                   const Container::String& = "main") override
        {
            return MakeResult();
        }

        RHI::ShaderCompileResult CompileFromFile(const Container::String&,
                                                 RHI::ShaderStage,
                                                 const Container::String& = "main") override
        {
            return MakeResult();
        }

    private:
        static RHI::ShaderCompileResult MakeResult()
        {
            RHI::ShaderCompileResult result;
            result.bSuccess = true;
            result.ByteCode = {0x03, 0x02, 0x23, 0x07};
            return result;
        }
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

    class FakeSwapChain final : public RHI::ISwapChain
    {
    public:
        explicit FakeSwapChain(uint32_t frameSlotCount)
            : m_FrameSlotSerials(frameSlotCount, 0)
        {
            RHI::TextureDesc desc = RHI::TextureDesc::RenderTarget(
                640, 480, RHI::Format::B8G8R8A8_UNORM, "FakeBackBuffer");
            m_BackBuffer = Container::MakeShared<FakeTexture>(desc);
        }

        uint32_t GetWidth() const override { return 640; }
        uint32_t GetHeight() const override { return 480; }
        RHI::Format GetFormat() const override { return RHI::Format::B8G8R8A8_UNORM; }
        bool ConsumePresentationDirty() override { return false; }
        uint32_t GetBufferCount() const override { return 1; }
        uint32_t GetCurrentBackBufferIndex() const override { return 0; }
        RHI::TexturePtr GetBackBuffer(uint32_t index) const override
        {
            return index == 0 ? m_BackBuffer : nullptr;
        }
        RHI::TexturePtr GetCurrentBackBuffer() const override { return m_BackBuffer; }
        void Present(bool = true) override {}

        RHI::SwapChainBeginFrameStatus BeginFrame() override
        {
            ++BeginFrameCallCount;
            const RHI::SwapChainBeginFrameStatus status = m_NextBeginFrameStatus;
            m_NextBeginFrameStatus = RHI::SwapChainBeginFrameStatus::Success;
            if (status != RHI::SwapChainBeginFrameStatus::Success)
            {
                m_bImageAcquired = false;
                return status;
            }
            const uint64_t slotSerial = m_FrameSlotSerials[m_CurrentFrame];
            if (slotSerial > m_CompletedSerial)
            {
                m_CompletedSerial = slotSerial;
            }
            m_bImageAcquired = true;
            return RHI::SwapChainBeginFrameStatus::Success;
        }

        RHI::SwapChainEndFrameResult EndFrame(RHI::CommandListPtr commandList) override
        {
            ++EndFrameCallCount;
            RHI::SwapChainEndFrameResult result;
            if (!commandList)
            {
                result.Status = RHI::SwapChainEndFrameStatus::InvalidCommandList;
                return result;
            }
            if (m_NextEndFrameStatus != RHI::SwapChainEndFrameStatus::Success)
            {
                result.Status = m_NextEndFrameStatus;
                m_NextEndFrameStatus = RHI::SwapChainEndFrameStatus::Success;
                return result;
            }
            uint64_t submittedSerial = 0;
            if (!RHI::Detail::TryAllocateSubmissionSerial(m_NextSerial, submittedSerial))
            {
                result.Status = RHI::SwapChainEndFrameStatus::SubmissionSerialExhausted;
                return result;
            }
            ++FenceResetCallCount;
            ++QueueSubmitCallCount;
            m_NextSerial = submittedSerial;
            m_FrameSlotSerials[m_CurrentFrame] = submittedSerial;
            m_CurrentFrame = (m_CurrentFrame + 1) % static_cast<uint32_t>(m_FrameSlotSerials.size());
            m_bImageAcquired = false;
            result.SubmissionSerial = submittedSerial;
            if (m_bFailNextPresentationAfterSubmit)
            {
                m_bFailNextPresentationAfterSubmit = false;
                result.Status = RHI::SwapChainEndFrameStatus::PresentationFailed;
            }
            else
            {
                result.Status = RHI::SwapChainEndFrameStatus::Success;
            }
            return result;
        }

        void SetNextBeginFrameStatus(RHI::SwapChainBeginFrameStatus status)
        {
            m_NextBeginFrameStatus = status;
        }
        bool HasAcquiredImage() const { return m_bImageAcquired; }
        void SetNextEndFrameStatus(RHI::SwapChainEndFrameStatus status)
        {
            m_NextEndFrameStatus = status;
        }
        void SetNextSubmissionSerialForTest(uint64_t serial) { m_NextSerial = serial; }
        void FailNextPresentationAfterSubmit() { m_bFailNextPresentationAfterSubmit = true; }
        uint64_t GetCompletedSubmissionSerial() const override { return m_CompletedSerial; }
        void Resize(uint32_t, uint32_t) override {}
        uint32_t GetCurrentFrameIndex() const override { return m_CurrentFrame; }
        uint32_t GetMaxFramesInFlight() const override
        {
            return static_cast<uint32_t>(m_FrameSlotSerials.size());
        }

        uint32_t BeginFrameCallCount = 0;
        uint32_t EndFrameCallCount = 0;
        uint32_t FenceResetCallCount = 0;
        uint32_t QueueSubmitCallCount = 0;

    private:
        Container::VariableArray<uint64_t> m_FrameSlotSerials;
        RHI::TexturePtr m_BackBuffer;
        uint32_t m_CurrentFrame = 0;
        uint64_t m_NextSerial = 0;
        uint64_t m_CompletedSerial = 0;
        RHI::SwapChainBeginFrameStatus m_NextBeginFrameStatus =
            RHI::SwapChainBeginFrameStatus::Success;
        RHI::SwapChainEndFrameStatus m_NextEndFrameStatus =
            RHI::SwapChainEndFrameStatus::Success;
        bool m_bImageAcquired = false;
        bool m_bFailNextPresentationAfterSubmit = false;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        RHI::PipelineType GetPipelineType() const override { return RHI::PipelineType::Graphics; }
        uint32_t GetBindPointCount() const override { return 1; }
    };

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        void BindConstantBuffer(uint32_t, RHI::BufferPtr, uint32_t, uint32_t) override {}
        void BindTexture(uint32_t, RHI::TexturePtr) override {}
        void BindSampler(uint32_t, RHI::SamplerPtr) override {}
        void BindStorageBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t, uint32_t) override
        {
            StorageBuffers[binding] = buffer;
        }
        void BindStorageTexture(uint32_t, RHI::TexturePtr) override {}
        void BindStorageTexture(uint32_t, RHI::TexturePtr, uint32_t) override {}
        void Update() override { ++UpdateCount; }

        Container::Map<uint32_t, RHI::BufferPtr> StorageBuffers;
        uint32_t UpdateCount = 0;
    };

    class FakeCommandList;

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            auto buffer = Container::MakeShared<FakeBuffer>(desc);
            CreatedBuffers.push_back(buffer);
            return buffer;
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return Container::MakeShared<FakeTexture>(desc);
        }
        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
        {
            return Container::MakeShared<FakeSampler>(desc);
        }
        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc& desc) override
        {
            return Container::MakeShared<FakeShader>(desc.stage);
        }
        RHI::CommandListPtr CreateCommandList() override;
        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc&) override { return SwapChain; }
        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc& desc) override
        {
            return Container::MakeShared<FakeRenderPass>(desc);
        }
        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            return Container::MakeShared<FakeFramebuffer>(desc);
        }
        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&) override
        {
            return Container::MakeShared<FakePipeline>();
        }
        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc&) override
        {
            return Container::MakeShared<FakePipeline>();
        }
        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc&) override
        {
            auto descriptorSet = Container::MakeShared<FakeDescriptorSet>();
            CreatedDescriptorSets.push_back(descriptorSet);
            return descriptorSet;
        }
        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return Container::MakeShared<FakeShaderCompiler>();
        }
        RHI::IGPUResourceAllocator* GetResourceAllocator() override { return &Allocator; }
        void WaitIdle() override { ++WaitIdleCount; }
        RHI::API GetAPI() const override { return RHI::API::None; }
        const RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }

        Math::Matrix4x4 AdjustProjectionForClipSpace(
            const Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        RHI::DeviceCapabilities Capabilities;
        FakeAllocator Allocator;
        RHI::SwapChainPtr SwapChain;
        uint32_t WaitIdleCount = 0;
        Container::VariableArray<Container::TSharedPtr<FakeBuffer>> CreatedBuffers;
        Container::VariableArray<Container::TSharedPtr<FakeDescriptorSet>> CreatedDescriptorSets;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr, RHI::FramebufferPtr) override {}
        void EndRenderPass() override {}
        void SetViewport(const RHI::Viewport&) override {}
        void SetScissor(const RHI::ScissorRect&) override {}
        void SetPipeline(RHI::PipelinePtr pipeline) override { LastPipeline = pipeline; }
        void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
        {
            (void)offset;
            (void)slot;
            LastVertexBuffer = buffer;
            ++RecordedCommandCount;
        }
        void SetIndexBuffer(RHI::BufferPtr buffer,
                            uint64_t offset = 0,
                            RHI::IndexType type = RHI::IndexType::Uint32) override
        {
            (void)offset;
            (void)type;
            LastIndexBuffer = buffer;
            ++RecordedCommandCount;
        }
        void SetConstantBuffer(RHI::BufferPtr, uint32_t, RHI::ShaderStage) override {}
        void SetTexture(RHI::TexturePtr, uint32_t, RHI::ShaderStage) override {}
        void SetSampler(RHI::SamplerPtr, uint32_t, RHI::ShaderStage) override {}
        void SetDescriptorSet(RHI::DescriptorSetPtr descriptorSet, uint32_t slot = 0) override
        {
            LastDescriptorSet = descriptorSet;
            LastDescriptorSetSlot = slot;
            ++RecordedCommandCount;
        }
        void DrawIndexed(uint32_t indexCount,
                         uint32_t startIndexLocation = 0,
                         int32_t baseVertexLocation = 0) override
        {
            (void)startIndexLocation;
            (void)baseVertexLocation;
            ++DrawIndexedCount;
            LastIndexCount = indexCount;
            ++RecordedCommandCount;
        }
        void Draw(uint32_t, uint32_t = 0) override {}
        void DrawIndexedInstanced(uint32_t,
                                  uint32_t,
                                  uint32_t = 0,
                                  int32_t = 0,
                                  uint32_t = 0) override
        {
            ++DrawIndexedInstancedCount;
        }
        void DrawInstanced(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
        void DrawIndexedIndirect(RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferPtr,
                                      uint64_t,
                                      RHI::BufferPtr,
                                      uint64_t,
                                      uint32_t,
                                      uint32_t) override {}
        void FillBuffer(RHI::BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(RHI::BufferPtr, RHI::BufferPtr, uint64_t = 0, uint64_t = 0, uint64_t = 0) override {}
        void CopyBufferToTexture(RHI::BufferPtr,
                                 RHI::TexturePtr,
                                 uint32_t,
                                 uint32_t,
                                 uint64_t = 0,
                                 uint32_t = 0,
                                 uint32_t = 0) override {}
        void CopyTextureToBuffer(RHI::TexturePtr,
                                 RHI::BufferPtr,
                                 uint32_t,
                                 uint32_t,
                                 uint64_t = 0,
                                 uint32_t = 0,
                                 uint32_t = 0) override {}
        void CopyTexture(RHI::TexturePtr,
                         RHI::TexturePtr,
                         uint32_t,
                         uint32_t,
                         uint32_t = 0,
                         uint32_t = 0,
                         uint32_t = 0,
                         uint32_t = 0) override {}
        void GenerateMipmaps(RHI::TexturePtr) override {}
        void BufferBarrier(RHI::BufferPtr,
                           RHI::ResourceState,
                           RHI::ResourceState,
                           uint64_t = 0,
                           uint64_t = 0) override {}
        void TextureBarrier(RHI::TexturePtr,
                            RHI::ResourceState,
                            RHI::ResourceState,
                            uint32_t = 0,
                            uint32_t = 0,
                            uint32_t = 0,
                            uint32_t = 0) override {}

        RHI::BufferPtr LastVertexBuffer;
        RHI::BufferPtr LastIndexBuffer;
        RHI::PipelinePtr LastPipeline;
        RHI::DescriptorSetPtr LastDescriptorSet;
        uint32_t LastDescriptorSetSlot = 0;
        uint32_t DrawIndexedCount = 0;
        uint32_t DrawIndexedInstancedCount = 0;
        uint32_t LastIndexCount = 0;
        uint32_t RecordedCommandCount = 0;
    };

    RHI::CommandListPtr FakeDevice::CreateCommandList()
    {
        return Container::MakeShared<FakeCommandList>();
    }

    void SeedMesh(const Container::TSharedPtr<SkinnedMeshResource>& mesh, float xOffset = 0.0f)
    {
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(3);
        vertices[0].Position = {xOffset, 0.0f, 0.0f};
        vertices[1].Position = {xOffset + 1.0f, 0.0f, 0.0f};
        vertices[2].Position = {xOffset, 1.0f, 0.0f};
        for (Skeletal::SkeletalVertex& vertex : vertices)
        {
            vertex.Normal = {0.0f, 0.0f, 1.0f};
            vertex.JointIndices[0] = 0;
            vertex.JointWeights[0] = 1.0f;
        }
        Container::VariableArray<uint32_t> indices = {0, 1, 2};
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(std::move(indices));
        assert(mesh->Load());
    }

    Container::VariableArray<Math::Matrix4x4> MakePalette()
    {
        Container::VariableArray<Math::Matrix4x4> palette(1);
        Math::Matrix4x4& matrix = palette[0];
        float value = 1.0f;
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                matrix.m[row][column] = value;
                value += 1.0f;
            }
        }
        return palette;
    }

    void TestOutOfRangeIndexRejectsLeaseBeforeGpuStoreAndDraw()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedInvalidIndexMesh");
        assert(mesh);

        Container::VariableArray<Skeletal::SkeletalVertex> vertices(3);
        for (Skeletal::SkeletalVertex& vertex : vertices)
        {
            vertex.Normal = {0.0f, 0.0f, 1.0f};
            vertex.JointIndices[0] = 0;
            vertex.JointWeights[0] = 1.0f;
        }
        Container::VariableArray<uint32_t> indices = {0, 3, 2};
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(std::move(indices));

        mesh->RefreshRenderAssetLease();
        assert(!mesh->GetRenderAssetLease());
        assert(!mesh->GetRenderMeshHandle().IsValid());
        assert(!mesh->Load());
        assert(!mesh->IsLoaded());

        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);
        const size_t bufferCountBeforePrepare = device->CreatedBuffers.size();
        auto invalidFrameLease = Container::MakeShared<SkinnedMeshFrameLease>(
            mesh->GetRenderAssetLease());
        SkinnedMeshPreparedDraw prepared;
        assert(!resources.SkinnedMeshes().PrepareDraw(
            invalidFrameLease, MakePalette(), Math::Matrix4x4::Identity, prepared));
        assert(device->CreatedBuffers.size() == bufferCountBeforePrepare);

        DrawCommand command = DrawCommand::CreateDrawIndexed();
        command.Draw.PayloadKind = DrawPayloadKind::Skinned;
        command.Draw.InstanceCount = 1;
        command.Skinned.PassKind = SkinnedMeshPassKind::GBuffer;
        command.Skinned.FrameLease = invalidFrameLease;
        command.Pipeline = Container::MakeShared<FakePipeline>();
        FakeCommandList commandList;
        SceneRenderer renderer;
        assert(!renderer.RecordSkinnedDrawCall(
            command,
            &commandList,
            &resources.SkinnedMeshes(),
            Container::MakeShared<FakeDescriptorSet>()));
        assert(commandList.DrawIndexedCount == 0);

        resources.Shutdown();
        mesh->Unload();
        mesh.reset();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestScreenPropagatesSubmissionAndCompletionSerialsForSingleAndMultiSlot()
    {
        for (uint32_t slotCount : {1u, 2u})
        {
            auto swapChain = Container::MakeShared<FakeSwapChain>(slotCount);
            auto device = Container::MakeShared<FakeDevice>();
            device->SwapChain = swapChain;
            Screen screen;
            ScreenSettings settings;
            settings.WindowHandle.WindowType = Platform::NativeWindowHandle::Type::Win32;
            settings.WindowHandle.Handle1 = reinterpret_cast<void*>(1);
            assert(screen.Initialize(device, settings));

            auto commandList = Container::MakeShared<FakeCommandList>();
            assert(screen.BeginFrame() == RHI::SwapChainBeginFrameStatus::Success);
            assert(swapChain->HasAcquiredImage());
            assert(swapChain->GetCompletedSubmissionSerial() == 0);
            const RHI::SwapChainEndFrameResult firstEnd = screen.EndFrame(commandList);
            assert(firstEnd.SubmissionSerial == 1);
            assert(!firstEnd.HasError());

            assert(screen.BeginFrame() == RHI::SwapChainBeginFrameStatus::Success);
            assert(swapChain->HasAcquiredImage());
            assert(swapChain->GetCompletedSubmissionSerial() == (slotCount == 1 ? 1 : 0));
            const RHI::SwapChainEndFrameResult secondEnd = screen.EndFrame(commandList);
            assert(secondEnd.SubmissionSerial == 2);
            assert(!secondEnd.HasError());

            if (slotCount == 2)
            {
                assert(screen.BeginFrame() == RHI::SwapChainBeginFrameStatus::Success);
                assert(swapChain->HasAcquiredImage());
                assert(swapChain->GetCompletedSubmissionSerial() == 1);
                const RHI::SwapChainEndFrameResult thirdEnd = screen.EndFrame(commandList);
                assert(thirdEnd.SubmissionSerial == 3);
                assert(!thirdEnd.HasError());
            }

            for (RHI::SwapChainBeginFrameStatus status : {
                     RHI::SwapChainBeginFrameStatus::OutOfDate,
                     RHI::SwapChainBeginFrameStatus::NotReady,
                     RHI::SwapChainBeginFrameStatus::Fatal})
            {
                swapChain->SetNextBeginFrameStatus(status);
                assert(screen.BeginFrame() == status);
                assert(!swapChain->HasAcquiredImage());
            }
            screen.Shutdown();
        }
    }

    void TestSkinnedVertexLayoutAbi()
    {
        const VertexLayout layout = VertexLayout::CreateSkinned();
        assert(layout.ElementCount == 5);
        assert(layout.Elements[0].Offset == 0);
        assert(layout.Elements[1].Offset == 12);
        assert(layout.Elements[2].Offset == 24);
        assert(layout.Elements[3].Offset == 32);
        assert(layout.Elements[4].Offset == 48);
        assert(layout.Stride == 64);
        assert(offsetof(SkinnedMeshVertex, Position) == 0);
        assert(offsetof(SkinnedMeshVertex, Normal) == 12);
        assert(offsetof(SkinnedMeshVertex, TexCoord) == 24);
        assert(offsetof(SkinnedMeshVertex, BoneIndices) == 32);
        assert(offsetof(SkinnedMeshVertex, BoneWeights) == 48);
        assert(sizeof(SkinnedMeshVertex) == 64);
    }

    void TestFrameLeaseAndPaletteUpload()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedContractMesh");
        assert(mesh);
        SeedMesh(mesh);

        Container::TSharedPtr<const SkinnedMeshAssetLease> assetLease = mesh->GetRenderAssetLease();
        assert(assetLease && assetLease->IsAssetLeaseActive());
        auto frameLease = Container::MakeShared<SkinnedMeshFrameLease>(assetLease);
        Container::TWeakPtr<const SkinnedMeshFrameLease> weakFrameLease = frameLease;

        FramePacket packet;
        packet.SkinnedMeshFrameLeases.push_back(frameLease);
        frameLease.reset();
        assert(!weakFrameLease.expired());

        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);

        SkinnedMeshPreparedDraw prepared;
        Container::VariableArray<Math::Matrix4x4> palette(1);
        palette[0] = Math::Matrix4x4::Identity;
        palette[0].m00 = 0.0f;
        palette[0].m01 = 1.0f;
        palette[0].m10 = -1.0f;
        palette[0].m11 = 0.0f;
        palette[0].m30 = 4.0f;
        palette[0].m31 = 5.0f;
        palette[0].m32 = 6.0f;

        Math::Matrix4x4 meshNodeGlobal = Math::Matrix4x4::Identity;
        meshNodeGlobal.m30 = 2.0f;
        meshNodeGlobal.m31 = -1.0f;
        meshNodeGlobal.m32 = 3.0f;
        const Math::Matrix4x4 jointGlobal = palette[0] * meshNodeGlobal;
        assert(std::fabs(jointGlobal.m01 - 1.0f) <= Epsilon);
        assert(std::fabs(jointGlobal.m30) > Epsilon);
        assert(resources.SkinnedMeshes().PrepareDraw(packet.SkinnedMeshFrameLeases[0],
                                                     palette,
                                                     Math::Matrix4x4::Identity,
                                                     prepared));
        assert(prepared.VertexBuffer && prepared.IndexBuffer && prepared.PaletteBuffer);
        assert(prepared.IndexCount == 3);

        auto paletteBuffer = Container::DynamicPointerCast<FakeBuffer>(prepared.PaletteBuffer);
        assert(paletteBuffer);
        assert(paletteBuffer->Bytes.size() == sizeof(float) * 64);
        const float* uploaded = reinterpret_cast<const float*>(paletteBuffer->Bytes.data());
        const size_t paletteOffset = 32;
        for (size_t valueIndex = 0; valueIndex < 16; ++valueIndex)
        {
            assert(std::fabs(uploaded[paletteOffset + valueIndex] - palette[0].values[valueIndex]) <= Epsilon);
        }

        Skeletal::SkeletalVertex sourceVertex;
        sourceVertex.Position = {1.0f, 2.0f, 3.0f};
        sourceVertex.Normal = {1.0f, 0.0f, 0.0f};
        sourceVertex.JointIndices[0] = 0;
        sourceVertex.JointWeights[0] = 1.0f;
        const Animation::SkinnedVertexSample cpuSample =
            Animation::SkeletalAnimationSampler::SkinVertex(sourceVertex, palette);
        const Math::Vector3 shaderPosition =
            TransformPointAsShaderColumnMajor(uploaded + paletteOffset,
                                              sourceVertex.Position.X,
                                              sourceVertex.Position.Y,
                                              sourceVertex.Position.Z);
        assert(std::fabs(cpuSample.Position.x - 2.0f) <= Epsilon);
        assert(std::fabs(cpuSample.Position.y - 6.0f) <= Epsilon);
        assert(std::fabs(cpuSample.Position.z - 9.0f) <= Epsilon);
        assert(std::fabs(shaderPosition.x - cpuSample.Position.x) <= Epsilon);
        assert(std::fabs(shaderPosition.y - cpuSample.Position.y) <= Epsilon);
        assert(std::fabs(shaderPosition.z - cpuSample.Position.z) <= Epsilon);

        SkinnedMeshGpuLifetimeSnapshot lifetime;
        assert(resources.SkinnedMeshes().GetLifetimeSnapshot(prepared.MeshHandle, lifetime));
        assert(lifetime.bAssetLeaseActive);
        assert(lifetime.FrameLeaseCount == 1);
        assert(lifetime.LastSubmittedSerial == 0);

        packet.Clear();
        assert(weakFrameLease.expired());
        resources.Shutdown();
        mesh->Unload();
        assetLease.reset();
        mesh.reset();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestShaderWeightAndNormalSemanticsMatchCpuReference()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedNormalSemanticMesh");
        assert(mesh);
        SeedMesh(mesh);

        auto frameLease = Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease());
        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);

        Container::VariableArray<Math::Matrix4x4> palette(2, Math::Matrix4x4::Identity);
        palette[0].m00 = 2.0f;
        palette[0].m11 = 1.0f;
        palette[0].m22 = 0.5f;
        palette[0].m30 = 3.0f;
        palette[1].m00 = 0.5f;
        palette[1].m11 = 2.0f;
        palette[1].m22 = 1.0f;
        palette[1].m31 = -2.0f;

        Math::Matrix4x4 world = Math::Matrix4x4::Identity;
        world.m00 = 3.0f;
        world.m11 = 2.0f;
        world.m22 = 0.5f;
        world.m30 = 10.0f;

        SkinnedMeshPreparedDraw prepared;
        assert(resources.SkinnedMeshes().PrepareDraw(frameLease, palette, world, prepared));
        auto paletteBuffer = Container::DynamicPointerCast<FakeBuffer>(prepared.PaletteBuffer);
        assert(paletteBuffer);
        assert(paletteBuffer->Bytes.size() == sizeof(float) * 96);
        const float* uploaded = reinterpret_cast<const float*>(paletteBuffer->Bytes.data());
        assert(std::fabs(uploaded[16 + 0] - (1.0f / 3.0f)) <= Epsilon);
        assert(std::fabs(uploaded[16 + 5] - 0.5f) <= Epsilon);
        assert(std::fabs(uploaded[16 + 10] - 2.0f) <= Epsilon);
        assert(std::fabs(uploaded[3 * 16 + 0] - 0.5f) <= Epsilon);
        assert(std::fabs(uploaded[3 * 16 + 5] - 1.0f) <= Epsilon);
        assert(std::fabs(uploaded[3 * 16 + 10] - 2.0f) <= Epsilon);
        assert(std::fabs(uploaded[5 * 16 + 0] - 2.0f) <= Epsilon);
        assert(std::fabs(uploaded[5 * 16 + 5] - 0.5f) <= Epsilon);
        assert(std::fabs(uploaded[5 * 16 + 10] - 1.0f) <= Epsilon);

        Skeletal::SkeletalVertex source;
        source.Position = {1.0f, 2.0f, 3.0f};
        source.Normal = {1.0f, 1.0f, 1.0f};
        source.JointIndices[0] = 0;
        source.JointWeights[0] = 0.25f;
        source.JointIndices[1] = 1;
        source.JointWeights[1] = 0.5f;
        source.JointIndices[2] = 99;
        source.JointWeights[2] = 0.25f;
        source.JointIndices[3] = 0;
        source.JointWeights[3] = -0.25f;

        const Animation::SkinnedVertexSample cpu =
            Animation::SkeletalAnimationSampler::SkinVertex(source, palette);
        const Animation::SkinnedVertexSample shader =
            EvaluateUploadedShaderSemantics(uploaded, 2, source);
        const Math::Vector3 expectedWorldPosition =
            TransformPointAsShaderColumnMajor(uploaded, cpu.Position.x, cpu.Position.y, cpu.Position.z);
        const Math::Vector3 expectedWorldNormal = NormalizeIfNonZero(
            TransformVectorAsShaderColumnMajor(uploaded + 16, cpu.Normal));
        assert((shader.Position - expectedWorldPosition).LengthSquared() <= Epsilon);
        assert((shader.Normal - expectedWorldNormal).LengthSquared() <= Epsilon);

        Skeletal::SkeletalVertex zeroWeight = source;
        for (uint32_t influenceIndex = 0; influenceIndex < 4; ++influenceIndex)
        {
            zeroWeight.JointIndices[influenceIndex] = 99;
            zeroWeight.JointWeights[influenceIndex] = influenceIndex == 0 ? -1.0f : 0.0f;
        }
        zeroWeight.Normal = {0.0f, 0.0f, 0.0f};
        const Animation::SkinnedVertexSample zeroShader =
            EvaluateUploadedShaderSemantics(uploaded, 2, zeroWeight);
        const Math::Vector3 zeroExpectedPosition =
            TransformPointAsShaderColumnMajor(uploaded, 1.0f, 2.0f, 3.0f);
        assert((zeroShader.Position - zeroExpectedPosition).LengthSquared() <= Epsilon);
        assert(zeroShader.Normal.LengthSquared() <= Epsilon);

        resources.Shutdown();
        mesh->Unload();
        frameLease.reset();
        mesh.reset();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestSameIdReloadUsesGenerationAndActualCompletionTokens()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedGenerationMesh");
        assert(mesh);
        SeedMesh(mesh, 1.0f);

        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);

        const SkinnedMeshHandle oldHandle = mesh->GetRenderMeshHandle();
        assert(oldHandle.IsValid());
        assert(oldHandle.Generation > 0);
        auto oldFrameLease = Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease());
        SkinnedMeshPreparedDraw oldPrepared;
        assert(resources.SkinnedMeshes().PrepareDraw(oldFrameLease,
                                                     MakePalette(),
                                                     Math::Matrix4x4::Identity,
                                                     oldPrepared));
        assert(resources.SkinnedMeshes().MarkLastUse(oldPrepared, oldFrameLease));
        resources.SkinnedMeshes().CommitSubmittedFrame(5);

        mesh->Unload();
        SeedMesh(mesh, 20.0f);
        const SkinnedMeshHandle newHandle = mesh->GetRenderMeshHandle();
        assert(newHandle.Id == oldHandle.Id);
        assert(newHandle.Generation > oldHandle.Generation);

        resources.SkinnedMeshes().BeginFrame(0);
        auto newFrameLease = Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease());
        SkinnedMeshPreparedDraw newPrepared;
        assert(resources.SkinnedMeshes().PrepareDraw(newFrameLease,
                                                     MakePalette(),
                                                     Math::Matrix4x4::Identity,
                                                     newPrepared));
        assert(newPrepared.MeshHandle == newHandle);
        assert(newPrepared.VertexBuffer != oldPrepared.VertexBuffer);
        auto newVertexBuffer = Container::DynamicPointerCast<FakeBuffer>(newPrepared.VertexBuffer);
        assert(newVertexBuffer);
        const SkinnedMeshVertex* uploadedVertices =
            reinterpret_cast<const SkinnedMeshVertex*>(newVertexBuffer->Bytes.data());
        assert(std::fabs(uploadedVertices[0].Position[0] - 20.0f) <= Epsilon);

        SkinnedMeshGpuLifetimeSnapshot oldLifetime;
        assert(resources.SkinnedMeshes().GetLifetimeSnapshot(oldHandle, oldLifetime));
        assert(oldLifetime.LastSubmittedSerial == 5);
        assert(oldLifetime.CompletedSubmissionSerial == 0);
        assert(resources.SkinnedMeshes().IsResident(oldHandle));
        assert(resources.SkinnedMeshes().IsResident(newHandle));

        oldFrameLease.reset();
        oldPrepared = {};
        resources.SkinnedMeshes().BeginFrame(4);
        assert(resources.SkinnedMeshes().IsResident(oldHandle));
        resources.SkinnedMeshes().BeginFrame(5);
        assert(!resources.SkinnedMeshes().IsResident(oldHandle));
        assert(resources.SkinnedMeshes().IsResident(newHandle));

        assert(resources.SkinnedMeshes().MarkLastUse(newPrepared, newFrameLease));
        resources.SkinnedMeshes().AbortFrame();
        SkinnedMeshGpuLifetimeSnapshot newLifetime;
        assert(resources.SkinnedMeshes().GetLifetimeSnapshot(newHandle, newLifetime));
        assert(newLifetime.LastSubmittedSerial == 0);

        resources.Shutdown();
        mesh->Unload();
        newPrepared = {};
        newFrameLease.reset();
        mesh.reset();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestRecordCountersInstancingRejectAndThreeConditionRelease()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedRecordMesh");
        assert(mesh);
        SeedMesh(mesh);

        auto frameLease = Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease());
        Container::TWeakPtr<const SkinnedMeshFrameLease> weakFrameLease = frameLease;
        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);

        DrawCommand command = DrawCommand::CreateDrawIndexed();
        command.Draw.PayloadKind = DrawPayloadKind::Skinned;
        command.Draw.InstanceCount = 1;
        command.Draw.bInstanced = false;
        command.Skinned.PassKind = SkinnedMeshPassKind::GBuffer;
        command.Skinned.FrameLease = frameLease;
        command.Pipeline = Container::MakeShared<FakePipeline>();
        RHI::DescriptorSetPtr descriptorSet = Container::MakeShared<FakeDescriptorSet>();
        assert(resources.SkinnedMeshes().PrepareDraw(frameLease,
                                                     MakePalette(),
                                                     Math::Matrix4x4::Identity,
                                                     command.Skinned.Prepared));

        SceneRenderer renderer;
        FakeCommandList commandList;

        DrawCommand nullPipeline = command;
        nullPipeline.Pipeline.reset();
        assert(!renderer.RecordSkinnedDrawCall(nullPipeline,
                                               &commandList,
                                               &resources.SkinnedMeshes(),
                                               descriptorSet));
        assert(commandList.DrawIndexedCount == 0);
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 0);

        assert(!renderer.RecordSkinnedDrawCall(command,
                                               &commandList,
                                               &resources.SkinnedMeshes(),
                                               {}));
        assert(commandList.DrawIndexedCount == 0);
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 0);

        DrawCommand invalidFrameLease = command;
        invalidFrameLease.Skinned.FrameLease = Container::MakeShared<SkinnedMeshFrameLease>(
            Container::TSharedPtr<const SkinnedMeshAssetLease>{});
        assert(!renderer.RecordSkinnedDrawCall(invalidFrameLease,
                                               &commandList,
                                               &resources.SkinnedMeshes(),
                                               descriptorSet));
        assert(commandList.DrawIndexedCount == 0);
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 0);

        DrawCommand untrackedPalette = command;
        RHI::BufferDesc foreignPaletteDesc;
        foreignPaletteDesc.Size = sizeof(Math::Matrix4x4);
        foreignPaletteDesc.Usage = RHI::ResourceUsage::StorageBuffer;
        untrackedPalette.Skinned.Prepared.PaletteBuffer =
            Container::MakeShared<FakeBuffer>(foreignPaletteDesc);
        FakeCommandList markFailureCommandList;
        assert(!renderer.RecordSkinnedDrawCall(untrackedPalette,
                                               &markFailureCommandList,
                                               &resources.SkinnedMeshes(),
                                               descriptorSet));
        if (markFailureCommandList.RecordedCommandCount != 0)
        {
            std::cerr << "MarkLastUse failure recorded "
                      << markFailureCommandList.RecordedCommandCount
                      << " commands before validation\n";
            std::exit(1);
        }

        assert(renderer.RecordSkinnedDrawCall(command,
                                              &commandList,
                                              &resources.SkinnedMeshes(),
                                              descriptorSet));
        assert(commandList.DrawIndexedCount == 1);
        assert(commandList.DrawIndexedInstancedCount == 0);
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 1);
        assert(renderer.GetStats().SkinnedShadowDrawCallCount == 0);

        DrawCommand failed = command;
        failed.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
        failed.Skinned.Prepared.PaletteBuffer.reset();
        assert(!renderer.RecordSkinnedDrawCall(failed,
                                               &commandList,
                                               &resources.SkinnedMeshes(),
                                               descriptorSet));
        assert(renderer.GetStats().SkinnedShadowDrawCallCount == 0);

        DrawCommand instanced = command;
        instanced.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
        instanced.Draw.InstanceCount = 2;
        instanced.Draw.bInstanced = true;
        assert(!renderer.RecordSkinnedDrawCall(instanced,
                                               &commandList,
                                               &resources.SkinnedMeshes(),
                                               descriptorSet));
        assert(commandList.DrawIndexedInstancedCount == 0);
        assert(renderer.GetStats().SkinnedShadowDrawCallCount == 0);

        command.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
        assert(renderer.RecordSkinnedDrawCall(command,
                                              &commandList,
                                              &resources.SkinnedMeshes(),
                                              descriptorSet));
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 1);
        assert(renderer.GetStats().SkinnedShadowDrawCallCount == 1);
        assert(resources.SkinnedMeshes().CommitSubmittedFrame(1));

        SkinnedMeshGpuLifetimeSnapshot lifetime;
        assert(resources.SkinnedMeshes().GetLifetimeSnapshot(command.Skinned.Prepared.MeshHandle, lifetime));
        assert(lifetime.LastSubmittedSerial == 1);

        mesh->Unload();
        mesh.reset();
        command = DrawCommand{};
        nullPipeline = DrawCommand{};
        invalidFrameLease = DrawCommand{};
        failed = DrawCommand{};
        instanced = DrawCommand{};
        untrackedPalette = DrawCommand{};
        frameLease.reset();
        assert(weakFrameLease.expired());

        resources.SkinnedMeshes().BeginFrame(0);
        assert(resources.SkinnedMeshes().IsResident(lifetime.MeshHandle));
        resources.ClearAllResources();
        assert(resources.SkinnedMeshes().IsResident(lifetime.MeshHandle));
        resources.SkinnedMeshes().BeginFrame(1);
        assert(!resources.SkinnedMeshes().IsResident(lifetime.MeshHandle));

        resources.Shutdown();
        assert(device->WaitIdleCount == 1);
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestExistingPassesPrepareSkinnedCommandsWithoutInstanceBuffer()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedPassMesh");
        assert(mesh);
        SeedMesh(mesh);

        auto frameLease = Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease());
        FramePacket packet;
        packet.SkinnedMeshFrameLeases.push_back(frameLease);

        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);

        DrawCommand source = DrawCommand::CreateDrawIndexed();
        source.Draw.PayloadKind = DrawPayloadKind::Skinned;
        source.Draw.InstanceCount = 1;
        source.Draw.bInstanced = false;
        source.Skinned.FrameLeaseIndex = 0;
        source.Skinned.BonePalette = MakePalette();

        ViewRenderContext context;
        context.SkinnedMeshes = &resources.SkinnedMeshes();
        context.SnapshotSkinnedMeshFrameLeases = &packet.SkinnedMeshFrameLeases;
        assert(!context.InstanceDataBuffer);

        GBufferPass gBuffer;
        DrawCommand gBufferCommand;
        assert(SkinnedRenderPathContractTestAccess::PrepareGBuffer(
            gBuffer, context, source, gBufferCommand));
        assert(gBufferCommand.Skinned.PassKind == SkinnedMeshPassKind::GBuffer);
        assert(gBufferCommand.Skinned.Prepared.IsValid());

        ShadowMapPass shadow;
        DrawCommand shadowCommand;
        assert(SkinnedRenderPathContractTestAccess::PrepareShadow(
            shadow, context, source, shadowCommand));
        assert(shadowCommand.Skinned.PassKind == SkinnedMeshPassKind::Shadow);
        assert(shadowCommand.Skinned.Prepared.IsValid());

        source.Draw.InstanceCount = 2;
        source.Draw.bInstanced = true;
        assert(!SkinnedRenderPathContractTestAccess::PrepareGBuffer(
            gBuffer, context, source, gBufferCommand));
        assert(!SkinnedRenderPathContractTestAccess::PrepareShadow(
            shadow, context, source, shadowCommand));

        packet.Clear();
        frameLease.reset();
        mesh->Unload();
        mesh.reset();
        resources.SkinnedMeshes().BeginFrame(0);
        resources.Shutdown();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestInitializedPassesExecuteThroughFrameCommandsAndSceneRenderer()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedIntegratedPassMesh");
        assert(mesh);
        SeedMesh(mesh);

        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);

        FramePacket packet;
        packet.SkinnedMeshFrameLeases.push_back(
            Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease()));
        DrawCommand source = DrawCommand::CreateDrawIndexed();
        source.Draw.PayloadKind = DrawPayloadKind::Skinned;
        source.Draw.InstanceCount = 1;
        source.Draw.bInstanced = false;
        source.Draw.bCastShadow = true;
        source.Draw.WorldMatrix = Math::Matrix4x4::Identity;
        source.Skinned.FrameLeaseIndex = 0;
        source.Skinned.BonePalette = MakePalette();
        packet.DrawCommands.push_back(source);

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), ""));
        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr));
        renderer.SetSkinnedMeshResources(&resources.SkinnedMeshes());
        renderer.BeginFrame();
        FakeCommandList commandList;
        Container::VariableArray<FrameCommand> pending;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pending;
        context.SkinnedMeshes = &resources.SkinnedMeshes();
        context.Resources.Materials = &resources.Materials();
        context.Resources.Textures = &resources.Textures();
        context.Resources.Meshes = &resources.Meshes();
        context.SnapshotSkinnedMeshFrameLeases = &packet.SkinnedMeshFrameLeases;
        context.SnapshotDrawCommandSource = &packet.DrawCommands;
        context.SnapshotDrawCommands = DrawCommandView::FromArray(packet.DrawCommands);
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(packet.DrawCommands);
        context.RenderWidth = 64;
        context.RenderHeight = 64;
        context.ScreenWidth = 64;
        context.ScreenHeight = 64;

        GBufferPass gBuffer;
        gBuffer.SetSceneRenderer(&renderer);
        assert(gBuffer.Initialize(context));
        gBuffer.Setup(context);
        gBuffer.Execute(context);
        assert(pending.size() == 1);
        assert(pending[0].Type == FrameCommandType::GeometryPass);
        assert(pending[0].GeometryPass.DrawCommands);
        assert(pending[0].GeometryPass.DrawCommands->size() == 1);
        const DrawCommand& gBufferDraw = (*pending[0].GeometryPass.DrawCommands)[0];
        assert(gBufferDraw.Pipeline);
        assert(gBufferDraw.DescriptorSet);
        auto gBufferDescriptor = Container::DynamicPointerCast<FakeDescriptorSet>(gBufferDraw.DescriptorSet);
        assert(gBufferDescriptor);
        assert(gBufferDescriptor->StorageBuffers.find(8) != gBufferDescriptor->StorageBuffers.end());
        assert(gBufferDescriptor->StorageBuffers.find(9) != gBufferDescriptor->StorageBuffers.end());
        renderer.ExecuteFrameCommands(pending, &commandList);
        assert(commandList.LastPipeline == gBufferDraw.Pipeline);
        assert(commandList.LastDescriptorSet == gBufferDraw.DescriptorSet);
        assert(commandList.DrawIndexedCount == 1);
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 1);

        pending.clear();
        Container::VariableArray<LightProxy> lights(1);
        lights[0].LightId = 1;
        lights[0].Type = LightType::Directional;
        lights[0].DirectionY = -1.0f;
        lights[0].Intensity = 1.0f;
        lights[0].bCastShadows = true;
        lights[0].bVisible = true;
        context.SnapshotLightProxies = &lights;

        ShadowMapPass shadow;
        shadow.SetSceneRenderer(&renderer);
        assert(shadow.Initialize(context));
        shadow.Setup(context);
        shadow.Execute(context);
        assert(pending.size() == 1);
        assert(pending[0].GeometryPass.DrawCommands);
        assert(pending[0].GeometryPass.DrawCommands->size() == 1);
        const DrawCommand& shadowDraw = (*pending[0].GeometryPass.DrawCommands)[0];
        assert(shadowDraw.Pipeline);
        assert(shadowDraw.DescriptorSet);
        auto shadowDescriptor = Container::DynamicPointerCast<FakeDescriptorSet>(shadowDraw.DescriptorSet);
        assert(shadowDescriptor);
        assert(shadowDescriptor->StorageBuffers.find(8) != shadowDescriptor->StorageBuffers.end());
        assert(shadowDescriptor->StorageBuffers.find(9) != shadowDescriptor->StorageBuffers.end());
        renderer.ExecuteFrameCommands(pending, &commandList);
        assert(commandList.DrawIndexedCount == 2);
        assert(renderer.GetStats().SkinnedShadowDrawCallCount == 1);
        assert(resources.SkinnedMeshes().CommitSubmittedFrame(1));

        shadow.Shutdown();
        gBuffer.Shutdown();
        renderer.Shutdown();
        shaderManager.Shutdown();
        packet.Clear();
        mesh->Unload();
        resources.Shutdown();
        mesh.reset();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestDirectionalShadowFittingIncludesOnlySkinnedAnimatedWorldBounds()
    {
        DirectionalShadowMatrixSettings base;
        base.OrthoSize = 5.0f;
        base.NearPlane = 0.1f;
        base.FarPlane = 20.0f;
        base.LightDistance = 5.0f;
        base.Target = Math::Vector3::Zero;

        Container::VariableArray<SkinnedMeshProxy> skinned(1);
        skinned[0].MeshHandle = {1, 1};
        skinned[0].ComponentId = 1;
        skinned[0].bVisible = true;
        skinned[0].bCastShadow = true;
        skinned[0].bHasAnimatedBounds = true;
        skinned[0].AnimatedBounds = Math::AABB(
            Math::Vector3(-10.0f, -20.0f, -5.0f),
            Math::Vector3(10.0f, 20.0f, 5.0f));
        skinned[0].WorldTransform = Math::Matrix4x4::Identity;
        skinned[0].WorldTransform.m00 = 2.0f;
        skinned[0].WorldTransform.m11 = 3.0f;
        skinned[0].WorldTransform.m22 = 4.0f;
        skinned[0].WorldTransform.m30 = 100.0f;
        skinned[0].WorldTransform.m31 = -50.0f;
        skinned[0].WorldTransform.m32 = 25.0f;

        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(base, nullptr, &skinned, nullptr);
        assert(std::fabs(fitted.Target.x - 100.0f) <= Epsilon);
        assert(std::fabs(fitted.Target.y + 50.0f) <= Epsilon);
        assert(std::fabs(fitted.Target.z - 25.0f) <= Epsilon);
        assert(fitted.OrthoSize >= 65.0f);
        assert(fitted.LightDistance > base.LightDistance);
        assert(fitted.FarPlane > base.FarPlane);

        skinned[0].bCastShadow = false;
        const DirectionalShadowMatrixSettings ignored =
            FitDirectionalShadowMatrixSettingsToCasters(base, nullptr, &skinned, nullptr);
        assert((ignored.Target - base.Target).LengthSquared() <= Epsilon);
        assert(std::fabs(ignored.OrthoSize - base.OrthoSize) <= Epsilon);
    }

    void TestCoordinatorPropagatesFramePacketStatsAndSubmissionSerials()
    {
        ResourceRegistry registry;
        assert(registry.Initialize());
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("SkinnedCoordinatorMesh");
        assert(mesh);
        SeedMesh(mesh);

        auto swapChain = Container::MakeShared<FakeSwapChain>(1);
        auto device = Container::MakeShared<FakeDevice>();
        device->SwapChain = swapChain;
        RenderResources resources;
        assert(resources.Initialize(device));

        RenderingCoordinator coordinator;
        RenderingCoordinatorSettings settings;
        settings.Device = device;
        settings.WindowHandle.WindowType = Platform::NativeWindowHandle::Type::Win32;
        settings.WindowHandle.Handle1 = reinterpret_cast<void*>(1);
        settings.Width = 640;
        settings.Height = 480;
        settings.BackBufferCount = 1;
        settings.bEnableMultiThreadedRendering = false;
        assert(coordinator.Initialize(settings));
        coordinator.SetRenderResources(&resources);

        FramePacket packet;
        packet.FrameNumber = 42;
        packet.GeneratedDrawCommandCount = 1;
        packet.SkinnedMeshFrameLeases.push_back(
            Container::MakeShared<SkinnedMeshFrameLease>(mesh->GetRenderAssetLease()));
        DrawCommand source = DrawCommand::CreateDrawIndexed();
        source.Draw.PayloadKind = DrawPayloadKind::Skinned;
        source.Draw.InstanceCount = 1;
        source.Draw.bInstanced = false;
        source.Draw.bCastShadow = true;
        source.Draw.WorldMatrix = Math::Matrix4x4::Identity;
        source.Skinned.FrameLeaseIndex = 0;
        source.Skinned.BonePalette = MakePalette();
        packet.DrawCommands.push_back(source);
        packet.DrawCommandRange = {0, 1};
        packet.OpaqueCommandRange = {0, 1};

        LightProxy light;
        light.LightId = 1;
        light.Type = LightType::Directional;
        light.DirectionY = -1.0f;
        light.Intensity = 1.0f;
        light.bCastShadows = true;
        light.bVisible = true;
        packet.Scene.LightProxies.push_back(light);

        SkinnedMeshProxy proxy;
        proxy.MeshHandle = mesh->GetRenderMeshHandle();
        proxy.AssetLease = mesh->GetRenderAssetLease();
        proxy.ComponentId = 1;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.BonePalette = MakePalette();
        proxy.AnimatedBounds = Math::AABB(
            Math::Vector3(-1.0f, -1.0f, -1.0f),
            Math::Vector3(1.0f, 1.0f, 1.0f));
        proxy.bHasAnimatedBounds = true;
        proxy.bCastShadow = true;
        proxy.bVisible = true;
        packet.Scene.SkinnedMeshProxies.push_back(proxy);
        packet.SetState(FramePacketState::Reading);

        coordinator.RenderFrame(&packet);
        assert(swapChain->GetCompletedSubmissionSerial() == 0);
        assert(packet.Stats.SkinnedGBufferRecordedDraws == 1);
        assert(packet.Stats.SkinnedShadowRecordedDraws == 1);
        const RenderingCoordinatorStatsSnapshot diagnostics = coordinator.GetStatsSnapshot();
        assert(diagnostics.SkinnedGBufferRecordedDraws == 1);
        assert(diagnostics.SkinnedShadowRecordedDraws == 1);
        assert(diagnostics.GeneratedDrawCommandCount == 1);

        SkinnedMeshGpuLifetimeSnapshot lifetime;
        assert(resources.SkinnedMeshes().GetLifetimeSnapshot(mesh->GetRenderMeshHandle(), lifetime));
        assert(lifetime.LastSubmittedSerial == 1);
        assert(lifetime.CompletedSubmissionSerial == 0);

        swapChain->FailNextPresentationAfterSubmit();
        bool bPresentationFailurePropagated = false;
        try
        {
            coordinator.RenderFrame(&packet);
        }
        catch (const std::runtime_error&)
        {
            bPresentationFailurePropagated = true;
        }
        assert(bPresentationFailurePropagated);
        assert(resources.SkinnedMeshes().GetLifetimeSnapshot(mesh->GetRenderMeshHandle(), lifetime));
        assert(lifetime.LastSubmittedSerial == 2);
        assert(lifetime.CompletedSubmissionSerial == 1);

        swapChain->SetNextEndFrameStatus(RHI::SwapChainEndFrameStatus::InvalidCommandList);
        bool bInvalidCommandListPropagated = false;
        try
        {
            coordinator.RenderFrame(&packet);
        }
        catch (const std::runtime_error&)
        {
            bInvalidCommandListPropagated = true;
        }
        assert(bInvalidCommandListPropagated);
        assert(swapChain->HasAcquiredImage());

        coordinator.Shutdown();
        packet.Clear();
        mesh->Unload();
        resources.Shutdown();
        mesh.reset();
        registry.CollectGarbage();
        registry.Shutdown();
    }

    void TestCoordinatorDistinguishesRecoverableAndFatalBeginFrameStatuses()
    {
        auto swapChain = Container::MakeShared<FakeSwapChain>(1);
        auto device = Container::MakeShared<FakeDevice>();
        device->SwapChain = swapChain;

        RenderingCoordinator coordinator;
        RenderingCoordinatorSettings settings;
        settings.Device = device;
        settings.WindowHandle.WindowType = Platform::NativeWindowHandle::Type::Win32;
        settings.WindowHandle.Handle1 = reinterpret_cast<void*>(1);
        settings.Width = 640;
        settings.Height = 480;
        settings.BackBufferCount = 1;
        settings.bEnableMultiThreadedRendering = false;
        assert(coordinator.Initialize(settings));

        FramePacket packet;
        packet.SetState(FramePacketState::Reading);
        for (RHI::SwapChainBeginFrameStatus status : {
                 RHI::SwapChainBeginFrameStatus::NotReady,
                 RHI::SwapChainBeginFrameStatus::OutOfDate})
        {
            swapChain->SetNextBeginFrameStatus(status);
            coordinator.RenderFrame(&packet);
            assert(!swapChain->HasAcquiredImage());
        }
        assert(swapChain->EndFrameCallCount == 0);

        swapChain->SetNextBeginFrameStatus(RHI::SwapChainBeginFrameStatus::Fatal);
        bool bFatalPropagated = false;
        try
        {
            coordinator.RenderFrame(&packet);
        }
        catch (const std::runtime_error&)
        {
            bFatalPropagated = true;
        }
        assert(bFatalPropagated);
        assert(!swapChain->HasAcquiredImage());
        assert(swapChain->BeginFrameCallCount == 3);
        assert(swapChain->EndFrameCallCount == 0);

        coordinator.Shutdown();
    }

    void TestRuntimeCompilerAcceptsSkinnedShaders()
    {
        RHI::Vulkan::VulkanShaderCompiler compiler;
        const RHI::ShaderCompileResult gBufferResult = compiler.CompileFromFile(
            NORVES_SOURCE_DIR "/Assets/Shaders/skinned_gbuffer.vert",
            RHI::ShaderStage::Vertex);
        assert(gBufferResult.bSuccess);
        assert(!gBufferResult.ByteCode.empty());

        const RHI::ShaderCompileResult shadowResult = compiler.CompileFromFile(
            NORVES_SOURCE_DIR "/Assets/Shaders/skinned_shadow.vert",
            RHI::ShaderStage::Vertex);
        assert(shadowResult.bSuccess);
        assert(!shadowResult.ByteCode.empty());
    }

    void TestVulkanBeginFrameFaultClassification()
    {
        using RHI::SwapChainBeginFrameStatus;
        using namespace RHI::Vulkan::Detail;

        assert(IsAcquireStateReady(640, 480, true, true));
        assert(!IsAcquireStateReady(0, 480, true, true));
        assert(!IsAcquireStateReady(640, 0, true, true));
        assert(!IsAcquireStateReady(640, 480, false, true));
        assert(!IsAcquireStateReady(640, 480, true, false));

        assert(ClassifyFenceWaitResult(vk::Result::eSuccess) == SwapChainBeginFrameStatus::Success);
        assert(ClassifyFenceWaitResult(vk::Result::eErrorDeviceLost) == SwapChainBeginFrameStatus::Fatal);
        assert(ClassifyAcquireResult(vk::Result::eSuccess) == SwapChainBeginFrameStatus::Success);
        assert(ClassifyAcquireResult(vk::Result::eSuboptimalKHR) == SwapChainBeginFrameStatus::Success);
        assert(ClassifyAcquireResult(vk::Result::eErrorOutOfDateKHR) == SwapChainBeginFrameStatus::OutOfDate);
        assert(ClassifyAcquireResult(vk::Result::eNotReady) == SwapChainBeginFrameStatus::NotReady);
        assert(ClassifyAcquireResult(vk::Result::eTimeout) == SwapChainBeginFrameStatus::NotReady);
        assert(ClassifyAcquireResult(vk::Result::eErrorDeviceLost) == SwapChainBeginFrameStatus::Fatal);
        assert(ClassifyAcquireResult(vk::Result::eErrorSurfaceLostKHR) == SwapChainBeginFrameStatus::Fatal);
    }

    void TestSubmissionSerialExhaustionNeverWrapsOrSubmits()
    {
        uint64_t allocatedSerial = 17;
        assert(RHI::Detail::TryAllocateSubmissionSerial(UINT64_MAX - 1, allocatedSerial));
        assert(allocatedSerial == UINT64_MAX);
        allocatedSerial = 17;
        assert(!RHI::Detail::TryAllocateSubmissionSerial(UINT64_MAX, allocatedSerial));
        assert(allocatedSerial == 0);

        auto swapChain = Container::MakeShared<FakeSwapChain>(1);
        auto device = Container::MakeShared<FakeDevice>();
        device->SwapChain = swapChain;
        Screen screen;
        ScreenSettings settings;
        settings.WindowHandle.WindowType = Platform::NativeWindowHandle::Type::Win32;
        settings.WindowHandle.Handle1 = reinterpret_cast<void*>(1);
        assert(screen.Initialize(device, settings));
        auto commandList = Container::MakeShared<FakeCommandList>();

        swapChain->SetNextSubmissionSerialForTest(UINT64_MAX - 1);
        assert(screen.BeginFrame() == RHI::SwapChainBeginFrameStatus::Success);
        const RHI::SwapChainEndFrameResult maxResult = screen.EndFrame(commandList);
        assert(maxResult.Status == RHI::SwapChainEndFrameStatus::Success);
        assert(maxResult.SubmissionSerial == UINT64_MAX);
        assert(swapChain->FenceResetCallCount == 1);
        assert(swapChain->QueueSubmitCallCount == 1);

        assert(screen.BeginFrame() == RHI::SwapChainBeginFrameStatus::Success);
        const RHI::SwapChainEndFrameResult exhaustedResult = screen.EndFrame(commandList);
        assert(exhaustedResult.Status == RHI::SwapChainEndFrameStatus::SubmissionSerialExhausted);
        assert(exhaustedResult.SubmissionSerial == 0);
        assert(swapChain->FenceResetCallCount == 1);
        assert(swapChain->QueueSubmitCallCount == 1);
        assert(swapChain->HasAcquiredImage());
        screen.Shutdown();
    }
} // namespace

int main()
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::cout << "SkinnedRenderPathContractTest start\n";
    TestSkinnedVertexLayoutAbi();
    TestOutOfRangeIndexRejectsLeaseBeforeGpuStoreAndDraw();
    TestScreenPropagatesSubmissionAndCompletionSerialsForSingleAndMultiSlot();
    TestFrameLeaseAndPaletteUpload();
    TestShaderWeightAndNormalSemanticsMatchCpuReference();
    TestSameIdReloadUsesGenerationAndActualCompletionTokens();
    TestRecordCountersInstancingRejectAndThreeConditionRelease();
    TestExistingPassesPrepareSkinnedCommandsWithoutInstanceBuffer();
    TestInitializedPassesExecuteThroughFrameCommandsAndSceneRenderer();
    TestDirectionalShadowFittingIncludesOnlySkinnedAnimatedWorldBounds();
    TestCoordinatorPropagatesFramePacketStatsAndSubmissionSerials();
    TestCoordinatorDistinguishesRecoverableAndFatalBeginFrameStatuses();
    TestRuntimeCompilerAcceptsSkinnedShaders();
    TestVulkanBeginFrameFaultClassification();
    TestSubmissionSerialExhaustionNeverWrapsOrSubmits();
    std::cout << "SkinnedRenderPathContractTest passed\n";
    return 0;
}
