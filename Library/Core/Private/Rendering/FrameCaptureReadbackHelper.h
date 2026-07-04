#pragma once

#include "Rendering/FrameCaptureTypes.h"
#include "RHI/RHITypes.h"
#include "Thread/Mutex.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    struct FrameCaptureSource
    {
        RHI::TexturePtr Texture;
        RHI::ResourceState CurrentState = RHI::ResourceState::ShaderResource;
        RHI::ResourceState RestoreState = RHI::ResourceState::ShaderResource;
        uint64_t FrameNumber = 0;
    };

    enum class FrameCaptureRecordStatus : uint8_t
    {
        NoRequest,
        Recorded,
        PublishedFailure
    };

    class FrameCaptureReadbackHelper
    {
    public:
        bool Initialize(RHI::IDevice* device, uint32_t frameSlotCount);
        void Shutdown();

        FrameCaptureRequestResult RequestFrameCapture();
        FrameCaptureRecordStatus TryRecordCopy(
            uint32_t frameSlotIndex,
            RHI::ICommandList* commandList,
            const FrameCaptureSource& source);
        void PublishCompletedFrameSlot(uint32_t frameSlotIndex);
        bool TryConsumeCapturedFrame(CapturedFrame& outFrame);

    private:
        enum class State : uint8_t
        {
            Uninitialized,
            Idle,
            Requested,
            PendingGpu,
            CompletedUnconsumed
        };

        struct ReadbackSlot
        {
            RHI::BufferPtr Buffer;
            uint64_t SizeBytes = 0;
        };

        struct PendingReadback
        {
            uint64_t RequestId = 0;
            uint64_t FrameNumber = 0;
            uint32_t SlotIndex = 0;
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::Format Format = RHI::Format::UNKNOWN;
            uint32_t BytesPerPixel = 0;
            uint32_t RowPitchBytes = 0;
            uint64_t ByteCount = 0;
        };

        static bool TryGetBytesPerPixel(RHI::Format format, uint32_t& outBytesPerPixel);
        static bool TryCalculateLayout(
            uint32_t width,
            uint32_t height,
            uint32_t bytesPerPixel,
            uint32_t& outRowPitchBytes,
            uint64_t& outByteCount);

        void PublishFailure(FrameCaptureResultStatus status, uint64_t frameNumber);

        Thread::Mutex m_Mutex;
        RHI::IDevice* m_Device = nullptr;
        State m_State = State::Uninitialized;
        uint32_t m_FrameSlotCount = 0;
        uint64_t m_NextRequestId = 1;
        uint64_t m_ActiveRequestId = 0;
        PendingReadback m_PendingReadback;
        CapturedFrame m_CompletedFrame;
        Container::VariableArray<ReadbackSlot> m_ReadbackSlots;
    };

} // namespace NorvesLib::Core::Rendering
