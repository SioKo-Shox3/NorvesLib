#include "CoreTypes.h"
#include "Rendering/FrameCaptureReadbackHelper.h"
#define private public
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#undef private
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ITexture.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
namespace
{
    using Container::DynamicPointerCast;
    using Container::MakeShared;
    using Container::TSharedPtr;
    using Container::TWeakPtr;
    using Container::VariableArray;

    constexpr uint32_t BytesPerPixel = 4;

    uint32_t BytesPerPixelForFormat(RHI::Format format)
    {
        return format == RHI::Format::R16G16B16A16_FLOAT ? 8u : BytesPerPixel;
    }

    enum class FakeCommandEventType : uint8_t
    {
        TextureBarrier,
        CopyTextureToBuffer
    };

    struct FakeCommandEvent
    {
        FakeCommandEventType Type = FakeCommandEventType::TextureBarrier;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    enum class FakeMapMode : uint8_t
    {
        Success,
        ReturnNull,
        Throw
    };

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const RHI::BufferDesc& desc)
            : m_Desc(desc)
        {
            Bytes.resize(static_cast<size_t>(desc.Size));
        }

        uint64_t GetSize() const override
        {
            return m_Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            ++MapCount;
            if (MapMode == FakeMapMode::Throw)
            {
                throw std::runtime_error("fake map failed");
            }
            if (MapMode == FakeMapMode::ReturnNull)
            {
                return nullptr;
            }
            if (offset > Bytes.size())
            {
                return nullptr;
            }
            const uint64_t mappedSize = size == 0 ? static_cast<uint64_t>(Bytes.size()) - offset : size;
            if (mappedSize > static_cast<uint64_t>(Bytes.size()) - offset)
            {
                return nullptr;
            }
            return Bytes.data() + offset;
        }

        void Unmap() override
        {
            ++UnmapCount;
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            assert(data != nullptr);
            assert(offset + size <= Bytes.size());
            std::memcpy(Bytes.data() + offset, data, static_cast<size_t>(size));
        }

        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }

        const RHI::BufferDesc& GetDesc() const
        {
            return m_Desc;
        }

        VariableArray<uint8_t> Bytes;
        FakeMapMode MapMode = FakeMapMode::Success;
        uint32_t MapCount = 0;
        uint32_t UnmapCount = 0;

    private:
        RHI::BufferDesc m_Desc;
    };

    class FakeTexture final : public RHI::ITexture
    {
    public:
        FakeTexture(uint32_t width, uint32_t height, RHI::Format format, RHI::ResourceUsage usage, bool bAllocateBytes = true)
        {
            m_Desc.Width = width;
            m_Desc.Height = height;
            m_Desc.TextureFormat = format;
            m_Desc.Usage = usage;
            if (!bAllocateBytes)
            {
                return;
            }

            Bytes.resize(
                static_cast<size_t>(width) * static_cast<size_t>(height) * BytesPerPixelForFormat(format));
            for (size_t index = 0; index < Bytes.size(); ++index)
            {
                Bytes[index] = static_cast<uint8_t>((index * 7u + 3u) & 0xFFu);
            }
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

        void Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override
        {
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
            assert(data != nullptr);
            std::memcpy(Bytes.data(), data, Bytes.size());
        }

        VariableArray<uint8_t> Bytes;

    private:
        RHI::TextureDesc m_Desc;
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
        void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, RHI::IndexType type = RHI::IndexType::Uint32) override
        {
            (void)buffer;
            (void)offset;
            (void)type;
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
        void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                  uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0, uint32_t startInstanceLocation = 0) override
        {
            (void)indexCount;
            (void)instanceCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
            (void)startInstanceLocation;
        }
        void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                           uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) override
        {
            (void)vertexCount;
            (void)instanceCount;
            (void)startVertexLocation;
            (void)startInstanceLocation;
        }
        void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer, uint64_t offset,
                                 uint32_t drawCount, uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)offset;
            (void)drawCount;
            (void)stride;
        }
        void DrawIndexedIndirectCount(RHI::BufferPtr indirectBuffer, uint64_t indirectOffset,
                                      RHI::BufferPtr countBuffer, uint64_t countOffset,
                                      uint32_t maxDrawCount, uint32_t stride) override
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
        void CopyBuffer(RHI::BufferPtr src, RHI::BufferPtr dst, uint64_t size = 0,
                        uint64_t srcOffset = 0, uint64_t dstOffset = 0) override
        {
            (void)src;
            (void)dst;
            (void)size;
            (void)srcOffset;
            (void)dstOffset;
        }
        void CopyBufferToTexture(RHI::BufferPtr src, RHI::TexturePtr dst,
                                 uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTextureToBuffer(RHI::TexturePtr src, RHI::BufferPtr dst,
                                 uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override
        {
            (void)mipLevel;
            (void)arrayIndex;
            FakeCommandEvent event;
            event.Type = FakeCommandEventType::CopyTextureToBuffer;
            event.Width = width;
            event.Height = height;
            Events.push_back(event);

            auto fakeTexture = DynamicPointerCast<FakeTexture>(src);
            auto fakeBuffer = DynamicPointerCast<FakeBuffer>(dst);
            assert(fakeTexture);
            assert(fakeBuffer);
            const uint64_t byteCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
                BytesPerPixelForFormat(src->GetFormat());
            LastCopyByteCount = byteCount;
            assert(bufferOffset + byteCount <= fakeBuffer->Bytes.size());
            assert(byteCount <= fakeTexture->Bytes.size());
            std::memcpy(fakeBuffer->Bytes.data() + bufferOffset, fakeTexture->Bytes.data(), static_cast<size_t>(byteCount));
        }
        void CopyTexture(RHI::TexturePtr src, RHI::TexturePtr dst,
                         uint32_t width, uint32_t height,
                         uint32_t srcMipLevel = 0, uint32_t srcArrayIndex = 0,
                         uint32_t dstMipLevel = 0, uint32_t dstArrayIndex = 0) override
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
        void BufferBarrier(RHI::BufferPtr buffer, RHI::ResourceState beforeState, RHI::ResourceState afterState,
                           uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)buffer;
            (void)beforeState;
            (void)afterState;
            (void)offset;
            (void)size;
        }
        void TextureBarrier(RHI::TexturePtr texture, RHI::ResourceState beforeState, RHI::ResourceState afterState,
                            uint32_t mipLevel = 0, uint32_t arrayIndex = 0, uint32_t mipCount = 0, uint32_t arrayCount = 0) override
        {
            (void)texture;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
            FakeCommandEvent event;
            event.Type = FakeCommandEventType::TextureBarrier;
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            Events.push_back(event);
        }

        VariableArray<FakeCommandEvent> Events;
        uint64_t LastCopyByteCount = 0;
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            CreatedBufferDescs.push_back(desc);
            if (bThrowCreateBuffer)
            {
                throw std::runtime_error("fake create buffer failed");
            }
            if (bReturnNullCreateBuffer)
            {
                return nullptr;
            }
            auto buffer = MakeShared<FakeBuffer>(desc);
            CreatedBuffers.push_back(buffer);
            return buffer;
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return MakeShared<FakeTexture>(desc.Width, desc.Height, desc.TextureFormat, desc.Usage);
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
            return MakeShared<FakeCommandList>();
        }

        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            (void)desc;
            return nullptr;
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
            return nullptr;
        }

        void WaitIdle() override
        {
            ++WaitIdleCount;
        }

        RHI::API GetAPI() const override
        {
            return RHI::API::None;
        }

        const RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return Capabilities;
        }

        Math::Matrix4x4 AdjustProjectionForClipSpace(
            const Math::Matrix4x4& projection, bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        VariableArray<RHI::BufferDesc> CreatedBufferDescs;
        VariableArray<TSharedPtr<FakeBuffer>> CreatedBuffers;
        RHI::DeviceCapabilities Capabilities;
        uint32_t WaitIdleCount = 0;
        bool bThrowCreateBuffer = false;
        bool bReturnNullCreateBuffer = false;
    };

    RHI::TexturePtr MakeTexture(
        uint32_t width = 3,
        uint32_t height = 2,
        RHI::Format format = RHI::Format::R8G8B8A8_UNORM,
        RHI::ResourceUsage usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferSrc,
        bool bAllocateBytes = true)
    {
        return MakeShared<FakeTexture>(width, height, format, usage, bAllocateBytes);
    }

    FrameCaptureSource MakeSource(RHI::TexturePtr texture, uint64_t frameNumber = 11)
    {
        FrameCaptureSource source;
        source.Texture = texture;
        source.CurrentState = RHI::ResourceState::ShaderResource;
        source.RestoreState = RHI::ResourceState::ShaderResource;
        source.FrameNumber = frameNumber;
        return source;
    }

    void AssertConsumeStatus(FrameCaptureReadbackHelper& helper, FrameCaptureResultStatus status)
    {
        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::Success;
        assert(helper.TryConsumeCapturedFrame(frame));
        assert(frame.Status == status);
    }

    void TestCapturedFrameType()
    {
        CapturedFrame frame;
        static_assert(std::is_same_v<decltype(frame.Pixels), VariableArray<uint8_t>>);
        frame.Pixels.push_back(42);
        assert(frame.Pixels[0] == 42);
        assert(!frame.IsSuccess());
    }

    void TestInitializeRejectsInvalidInputs()
    {
        FakeDevice device;
        FrameCaptureReadbackHelper helper;
        assert(!helper.Initialize(nullptr, 1));
        assert(!helper.Initialize(&device, 0));
    }

    void TestRequestCoalescing()
    {
        FakeDevice device;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));

        FrameCaptureRequestResult first = helper.RequestFrameCapture();
        assert(first.Status == FrameCaptureRequestStatus::Accepted);
        assert(first.RequestId != 0);

        FrameCaptureRequestResult second = helper.RequestFrameCapture();
        assert(second.Status == FrameCaptureRequestStatus::AlreadyPending);
        assert(second.RequestId == first.RequestId);

        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::Success;
        assert(!helper.TryConsumeCapturedFrame(frame));
        assert(frame.Status == FrameCaptureResultStatus::SourceUnavailable);
        assert(frame.RequestId == 0);
    }

    void TestNoRequestNoOp()
    {
        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));

        assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::NoRequest);
        assert(device.CreatedBufferDescs.empty());
        assert(commandList.Events.empty());

        CapturedFrame frame;
        assert(!helper.TryConsumeCapturedFrame(frame));
    }

    void TestValidRecordAndPublish()
    {
        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));
        FrameCaptureRequestResult request = helper.RequestFrameCapture();
        auto texture = MakeTexture();
        auto fakeTexture = DynamicPointerCast<FakeTexture>(texture);

        assert(helper.TryRecordCopy(1, &commandList, MakeSource(texture, 37)) == FrameCaptureRecordStatus::Recorded);
        assert(commandList.Events.size() == 3);
        assert(commandList.Events[0].Type == FakeCommandEventType::TextureBarrier);
        assert(commandList.Events[0].BeforeState == RHI::ResourceState::ShaderResource);
        assert(commandList.Events[0].AfterState == RHI::ResourceState::CopySource);
        assert(commandList.Events[1].Type == FakeCommandEventType::CopyTextureToBuffer);
        assert(commandList.Events[2].Type == FakeCommandEventType::TextureBarrier);
        assert(commandList.Events[2].BeforeState == RHI::ResourceState::CopySource);
        assert(commandList.Events[2].AfterState == RHI::ResourceState::ShaderResource);

        assert(device.CreatedBufferDescs.size() == 1);
        assert(device.CreatedBufferDescs[0].Size == 3u * 2u * BytesPerPixel);
        assert(device.CreatedBufferDescs[0].Usage == RHI::ResourceUsage::TransferDst);
        assert(device.CreatedBufferDescs[0].CPUAccessible);
        assert(device.WaitIdleCount == 0);

        helper.PublishCompletedFrameSlot(0);
        CapturedFrame frame;
        assert(!helper.TryConsumeCapturedFrame(frame));

        helper.PublishCompletedFrameSlot(1);
        assert(helper.TryConsumeCapturedFrame(frame));
        assert(frame.IsSuccess());
        assert(frame.RequestId == request.RequestId);
        assert(frame.FrameNumber == 37);
        assert(frame.Width == 3);
        assert(frame.Height == 2);
        assert(frame.Format == RHI::Format::R8G8B8A8_UNORM);
        assert(frame.BytesPerPixel == 4);
        assert(frame.RowPitchBytes == 12);
        assert(frame.Pixels.size() == fakeTexture->Bytes.size());
        assert(std::memcmp(frame.Pixels.data(), fakeTexture->Bytes.data(), frame.Pixels.size()) == 0);
    }

    void TestRgba16FloatRecordLayout()
    {
        constexpr uint32_t Width = 2u;
        constexpr uint32_t Height = 3u;
        constexpr uint32_t FloatBytesPerPixel = 8u;

        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 1));
        assert(helper.RequestFrameCapture().IsAccepted());

        RHI::TexturePtr texture = MakeTexture(
            Width,
            Height,
            RHI::Format::R16G16B16A16_FLOAT,
            RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferSrc);
        const FrameCaptureRecordStatus recordStatus =
            helper.TryRecordCopy(0u, &commandList, MakeSource(texture));
        if (recordStatus != FrameCaptureRecordStatus::Recorded)
        {
            std::cerr << "RGBA16F record must be supported" << std::endl;
            std::exit(1);
        }
        assert(commandList.LastCopyByteCount == Width * Height * FloatBytesPerPixel);
        assert(device.CreatedBufferDescs.size() == 1u);
        assert(device.CreatedBufferDescs[0].Size == Width * Height * FloatBytesPerPixel);

        helper.PublishCompletedFrameSlot(0u);
        CapturedFrame frame;
        assert(helper.TryConsumeCapturedFrame(frame));
        assert(frame.IsSuccess());
        assert(frame.Format == RHI::Format::R16G16B16A16_FLOAT);
        assert(frame.BytesPerPixel == FloatBytesPerPixel);
        assert(frame.RowPitchBytes == Width * FloatBytesPerPixel);
        assert(frame.Pixels.size() == Width * Height * FloatBytesPerPixel);
    }

    void TestFailureBeforeAllocation(
        RHI::TexturePtr texture,
        FrameCaptureResultStatus expectedStatus)
    {
        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));
        assert(helper.RequestFrameCapture().IsAccepted());

        assert(helper.TryRecordCopy(0, &commandList, MakeSource(texture)) == FrameCaptureRecordStatus::PublishedFailure);
        assert(device.CreatedBufferDescs.empty());
        assert(commandList.Events.empty());
        AssertConsumeStatus(helper, expectedStatus);
    }

    void TestSourceValidationFailures()
    {
        TestFailureBeforeAllocation(
            MakeTexture(3, 2, RHI::Format::R8G8B8A8_UNORM, RHI::ResourceUsage::ShaderRead),
            FrameCaptureResultStatus::SourceMissingTransferSrc);
        TestFailureBeforeAllocation(
            MakeTexture(3, 2, RHI::Format::R32_FLOAT, RHI::ResourceUsage::TransferSrc),
            FrameCaptureResultStatus::UnsupportedFormat);
        TestFailureBeforeAllocation(
            MakeTexture(3, 2, RHI::Format::R32G32B32A32_FLOAT, RHI::ResourceUsage::TransferSrc),
            FrameCaptureResultStatus::UnsupportedFormat);
        TestFailureBeforeAllocation(
            MakeTexture(0, 2, RHI::Format::R8G8B8A8_UNORM, RHI::ResourceUsage::TransferSrc),
            FrameCaptureResultStatus::InvalidDimensions);
        TestFailureBeforeAllocation(
            MakeTexture(3, 0, RHI::Format::R8G8B8A8_UNORM, RHI::ResourceUsage::TransferSrc),
            FrameCaptureResultStatus::InvalidDimensions);
        TestFailureBeforeAllocation(
            MakeTexture(
                std::numeric_limits<uint32_t>::max() / BytesPerPixel + 1,
                1,
                RHI::Format::R8G8B8A8_UNORM,
                RHI::ResourceUsage::TransferSrc,
                false),
            FrameCaptureResultStatus::InvalidDimensions);
    }

    void TestSourceUnavailableFailures()
    {
        {
            FakeDevice device;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            assert(helper.TryRecordCopy(0, nullptr, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::PublishedFailure);
            assert(device.CreatedBufferDescs.empty());
            AssertConsumeStatus(helper, FrameCaptureResultStatus::SourceUnavailable);
        }

        {
            FakeDevice device;
            FakeCommandList commandList;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            assert(helper.TryRecordCopy(0, &commandList, MakeSource(nullptr)) == FrameCaptureRecordStatus::PublishedFailure);
            assert(device.CreatedBufferDescs.empty());
            assert(commandList.Events.empty());
            AssertConsumeStatus(helper, FrameCaptureResultStatus::SourceUnavailable);
        }

        {
            FakeDevice device;
            FakeCommandList commandList;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            assert(helper.TryRecordCopy(2, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::PublishedFailure);
            assert(device.CreatedBufferDescs.empty());
            assert(commandList.Events.empty());
            AssertConsumeStatus(helper, FrameCaptureResultStatus::SourceUnavailable);
        }
    }

    void TestCreateBufferFailures()
    {
        {
            FakeDevice device;
            FakeCommandList commandList;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            device.bThrowCreateBuffer = true;

            assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::PublishedFailure);
            assert(commandList.Events.empty());
            AssertConsumeStatus(helper, FrameCaptureResultStatus::ReadbackBufferCreateFailed);
        }

        {
            FakeDevice device;
            FakeCommandList commandList;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            device.bReturnNullCreateBuffer = true;

            assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::PublishedFailure);
            assert(commandList.Events.empty());
            AssertConsumeStatus(helper, FrameCaptureResultStatus::ReadbackBufferCreateFailed);
        }
    }

    void TestMapFailures()
    {
        {
            FakeDevice device;
            FakeCommandList commandList;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::Recorded);
            device.CreatedBuffers[0]->MapMode = FakeMapMode::Throw;

            helper.PublishCompletedFrameSlot(0);
            assert(device.CreatedBuffers[0]->MapCount == 1);
            assert(device.CreatedBuffers[0]->UnmapCount == 0);
            AssertConsumeStatus(helper, FrameCaptureResultStatus::MapFailed);
        }

        {
            FakeDevice device;
            FakeCommandList commandList;
            FrameCaptureReadbackHelper helper;
            assert(helper.Initialize(&device, 2));
            assert(helper.RequestFrameCapture().IsAccepted());
            assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::Recorded);
            device.CreatedBuffers[0]->MapMode = FakeMapMode::ReturnNull;

            helper.PublishCompletedFrameSlot(0);
            assert(device.CreatedBuffers[0]->MapCount == 1);
            assert(device.CreatedBuffers[0]->UnmapCount == 0);
            AssertConsumeStatus(helper, FrameCaptureResultStatus::MapFailed);
        }
    }

    void TestConsumeOneShot()
    {
        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));
        assert(helper.RequestFrameCapture().IsAccepted());
        assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture())) == FrameCaptureRecordStatus::Recorded);
        helper.PublishCompletedFrameSlot(0);

        CapturedFrame frame;
        assert(helper.TryConsumeCapturedFrame(frame));
        assert(frame.IsSuccess());
        frame.Status = FrameCaptureResultStatus::Success;
        assert(!helper.TryConsumeCapturedFrame(frame));
        assert(frame.Status == FrameCaptureResultStatus::SourceUnavailable);
    }

    void TestBufferReuseAndSlotIsolation()
    {
        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));

        assert(helper.RequestFrameCapture().IsAccepted());
        assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture(2, 2))) == FrameCaptureRecordStatus::Recorded);
        TSharedPtr<FakeBuffer> firstSlotBuffer = device.CreatedBuffers[0];
        helper.PublishCompletedFrameSlot(0);
        CapturedFrame frame;
        assert(helper.TryConsumeCapturedFrame(frame));

        assert(helper.RequestFrameCapture().IsAccepted());
        assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture(1, 1))) == FrameCaptureRecordStatus::Recorded);
        assert(device.CreatedBuffers.size() == 1);
        assert(device.CreatedBuffers[0] == firstSlotBuffer);
        helper.PublishCompletedFrameSlot(0);
        assert(helper.TryConsumeCapturedFrame(frame));

        assert(helper.RequestFrameCapture().IsAccepted());
        assert(helper.TryRecordCopy(0, &commandList, MakeSource(MakeTexture(3, 2))) == FrameCaptureRecordStatus::Recorded);
        assert(device.CreatedBuffers.size() == 2);
        TSharedPtr<FakeBuffer> largerSlotBuffer = device.CreatedBuffers[1];
        assert(largerSlotBuffer != firstSlotBuffer);
        helper.PublishCompletedFrameSlot(0);
        assert(helper.TryConsumeCapturedFrame(frame));

        assert(helper.RequestFrameCapture().IsAccepted());
        assert(helper.TryRecordCopy(1, &commandList, MakeSource(MakeTexture(3, 2))) == FrameCaptureRecordStatus::Recorded);
        assert(device.CreatedBuffers.size() == 3);
        assert(device.CreatedBuffers[2] != largerSlotBuffer);
        helper.PublishCompletedFrameSlot(1);
        assert(helper.TryConsumeCapturedFrame(frame));
    }

    void TestSourceTextureIsNotRetainedPastRecord()
    {
        FakeDevice device;
        FakeCommandList commandList;
        FrameCaptureReadbackHelper helper;
        assert(helper.Initialize(&device, 2));
        assert(helper.RequestFrameCapture().IsAccepted());

        RHI::TexturePtr texture = MakeTexture(3, 2);
        auto fakeTexture = DynamicPointerCast<FakeTexture>(texture);
        TWeakPtr<FakeTexture> weakTexture = fakeTexture;
        VariableArray<uint8_t> expectedPixels = fakeTexture->Bytes;

        assert(helper.TryRecordCopy(0, &commandList, MakeSource(texture, 91)) == FrameCaptureRecordStatus::Recorded);
        texture.reset();
        fakeTexture.reset();
        assert(weakTexture.expired());

        helper.PublishCompletedFrameSlot(0);

        CapturedFrame frame;
        assert(helper.TryConsumeCapturedFrame(frame));
        assert(frame.IsSuccess());
        assert(frame.FrameNumber == 91);
        assert(frame.Pixels.size() == expectedPixels.size());
        assert(std::memcmp(frame.Pixels.data(), expectedPixels.data(), frame.Pixels.size()) == 0);
        assert(device.WaitIdleCount == 0);
    }

    void TestPublicPreInitWrappers()
    {
        RenderingCoordinator coordinator;
        RenderWorld renderWorld;
        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::Success;
        frame.RequestId = 99;
        frame.Pixels.push_back(1);

        FrameCaptureRequestResult coordinatorRequest = coordinator.RequestFrameCapture();
        assert(coordinatorRequest.Status == FrameCaptureRequestStatus::NotInitialized);
        assert(coordinatorRequest.RequestId == 0);
        assert(!coordinator.TryConsumeCapturedFrame(frame));
        assert(frame.Status == FrameCaptureResultStatus::SourceUnavailable);
        assert(frame.RequestId == 0);
        assert(frame.Pixels.empty());

        frame.Status = FrameCaptureResultStatus::Success;
        frame.RequestId = 99;
        frame.Pixels.push_back(1);
        FrameCaptureRequestResult worldRequest = renderWorld.RequestFrameCapture();
        assert(worldRequest.Status == FrameCaptureRequestStatus::NotInitialized);
        assert(worldRequest.RequestId == 0);
        assert(!renderWorld.TryConsumeCapturedFrame(frame));
        assert(frame.Status == FrameCaptureResultStatus::SourceUnavailable);
        assert(frame.RequestId == 0);
        assert(frame.Pixels.empty());
    }

    void TestPublicWrappersDelegateWhenInitialized()
    {
        FakeDevice device;

        RenderingCoordinator coordinator;
        coordinator.m_bInitialized = true;
        coordinator.m_FrameCaptureReadbackHelper = Container::MakeUnique<FrameCaptureReadbackHelper>();
        assert(coordinator.m_FrameCaptureReadbackHelper->Initialize(&device, 2));

        FrameCaptureRequestResult coordinatorRequest = coordinator.RequestFrameCapture();
        assert(coordinatorRequest.Status == FrameCaptureRequestStatus::Accepted);
        assert(coordinatorRequest.RequestId != 0);
        FrameCaptureRequestResult coordinatorSecondRequest = coordinator.RequestFrameCapture();
        assert(coordinatorSecondRequest.Status == FrameCaptureRequestStatus::AlreadyPending);
        assert(coordinatorSecondRequest.RequestId == coordinatorRequest.RequestId);

        RenderWorld renderWorld;
        renderWorld.m_bInitialized = true;
        renderWorld.m_RenderingCoordinator.m_bInitialized = true;
        renderWorld.m_RenderingCoordinator.m_FrameCaptureReadbackHelper =
            Container::MakeUnique<FrameCaptureReadbackHelper>();
        assert(renderWorld.m_RenderingCoordinator.m_FrameCaptureReadbackHelper->Initialize(&device, 2));

        FrameCaptureRequestResult worldRequest = renderWorld.RequestFrameCapture();
        assert(worldRequest.Status == FrameCaptureRequestStatus::Accepted);
        assert(worldRequest.RequestId != 0);
        FrameCaptureRequestResult worldSecondRequest = renderWorld.RequestFrameCapture();
        assert(worldSecondRequest.Status == FrameCaptureRequestStatus::AlreadyPending);
        assert(worldSecondRequest.RequestId == worldRequest.RequestId);
    }

    int RunTest()
    {
        TestCapturedFrameType();
        TestInitializeRejectsInvalidInputs();
        TestRequestCoalescing();
        TestNoRequestNoOp();
        TestValidRecordAndPublish();
        TestRgba16FloatRecordLayout();
        TestSourceValidationFailures();
        TestSourceUnavailableFailures();
        TestCreateBufferFailures();
        TestMapFailures();
        TestConsumeOneShot();
        TestBufferReuseAndSlotIsolation();
        TestSourceTextureIsNotRetainedPastRecord();
        TestPublicPreInitWrappers();
        TestPublicWrappersDelegateWhenInitialized();

        std::cout << "FrameCaptureReadbackHelperTest passed" << std::endl;
        return 0;
    }

} // namespace
} // namespace NorvesLib::Core::Rendering

int main()
{
    return NorvesLib::Core::Rendering::RunTest();
}
