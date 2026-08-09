#include "Rendering/FrameCaptureReadbackHelper.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ITexture.h"
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    bool FrameCaptureReadbackHelper::Initialize(RHI::IDevice* device, uint32_t frameSlotCount)
    {
        Thread::ScopedLock lock(m_Mutex);

        if (!device || frameSlotCount == 0)
        {
            return false;
        }

        m_Device = device;
        m_FrameSlotCount = frameSlotCount;
        m_State = State::Idle;
        m_ActiveRequest = FrameCaptureRequestSnapshot{};
        m_PendingReadback = PendingReadback{};
        m_CompletedFrame = CapturedFrame{};
        m_ReadbackSlots.clear();
        m_ReadbackSlots.resize(frameSlotCount);
        return true;
    }

    void FrameCaptureReadbackHelper::Shutdown()
    {
        Thread::ScopedLock lock(m_Mutex);

        m_Device = nullptr;
        m_FrameSlotCount = 0;
        m_State = State::Uninitialized;
        m_ActiveRequest = FrameCaptureRequestSnapshot{};
        m_PendingReadback = PendingReadback{};
        m_CompletedFrame = CapturedFrame{};
        m_ReadbackSlots.clear();
    }

    FrameCaptureRequestResult FrameCaptureReadbackHelper::RequestFrameCapture()
    {
        return RequestFrameCapture(FrameCaptureRequest{});
    }

    FrameCaptureRequestResult FrameCaptureReadbackHelper::RequestFrameCapture(
        const FrameCaptureRequest& request)
    {
        Thread::ScopedLock lock(m_Mutex);

        if (m_State == State::Uninitialized || !m_Device)
        {
            return {};
        }

        if (m_State != State::Idle)
        {
            return {FrameCaptureRequestStatus::AlreadyPending, m_ActiveRequest.RequestId};
        }

        m_ActiveRequest.RequestId = m_NextRequestId++;
        m_ActiveRequest.SourceKind = request.SourceKind;
        m_State = State::Requested;
        return {FrameCaptureRequestStatus::Accepted, m_ActiveRequest.RequestId};
    }

    bool FrameCaptureReadbackHelper::TrySnapshotPendingRequest(
        FrameCaptureRequestSnapshot& outSnapshot)
    {
        outSnapshot = FrameCaptureRequestSnapshot{};
        Thread::ScopedLock lock(m_Mutex);

        if (m_State != State::Requested || !m_ActiveRequest.IsValid())
        {
            return false;
        }

        outSnapshot = m_ActiveRequest;
        return true;
    }

    bool FrameCaptureReadbackHelper::TryClaimPendingRequest(
        const FrameCaptureRequestSnapshot& packetSnapshot,
        FrameCaptureRequestSnapshot& outClaimedSnapshot)
    {
        outClaimedSnapshot = FrameCaptureRequestSnapshot{};
        Thread::ScopedLock lock(m_Mutex);

        if (m_State != State::Requested ||
            !packetSnapshot.IsValid() ||
            packetSnapshot.RequestId != m_ActiveRequest.RequestId ||
            packetSnapshot.SourceKind != m_ActiveRequest.SourceKind)
        {
            return false;
        }

        outClaimedSnapshot = m_ActiveRequest;
        m_State = State::AssignedToFrame;
        return true;
    }

    bool FrameCaptureReadbackHelper::AbandonAssignedRequest(
        const FrameCaptureRequestSnapshot& snapshot,
        uint64_t frameNumber,
        FrameCaptureResultStatus failureStatus) noexcept
    {
        Thread::ScopedLock lock(m_Mutex);

        if (failureStatus == FrameCaptureResultStatus::Success ||
            !snapshot.IsValid() ||
            snapshot.RequestId != m_ActiveRequest.RequestId ||
            snapshot.SourceKind != m_ActiveRequest.SourceKind ||
            (m_State != State::AssignedToFrame && m_State != State::RecordedAwaitingSubmit))
        {
            return false;
        }

        PublishFailure(failureStatus, frameNumber);
        return true;
    }

    bool FrameCaptureReadbackHelper::CommitRecordedCopy(
        const FrameCaptureRequestSnapshot& snapshot) noexcept
    {
        Thread::ScopedLock lock(m_Mutex);

        if (m_State != State::RecordedAwaitingSubmit ||
            !snapshot.IsValid() ||
            snapshot.RequestId != m_ActiveRequest.RequestId ||
            snapshot.SourceKind != m_ActiveRequest.SourceKind)
        {
            return false;
        }

        m_State = State::PendingGpu;
        return true;
    }

    FrameCaptureRecordStatus FrameCaptureReadbackHelper::TryRecordCopy(
        uint32_t frameSlotIndex,
        RHI::ICommandList* commandList,
        const FrameCaptureRequestSnapshot& snapshot,
        const FrameCaptureSourceSet& sources)
    {
        Thread::ScopedLock lock(m_Mutex);

        if (m_State != State::AssignedToFrame ||
            !snapshot.IsValid() ||
            snapshot.RequestId != m_ActiveRequest.RequestId ||
            snapshot.SourceKind != m_ActiveRequest.SourceKind)
        {
            return FrameCaptureRecordStatus::NoRequest;
        }

        const FrameCaptureSource* source = sources.Find(snapshot.SourceKind);

        if (!commandList || !source || !source->Texture || frameSlotIndex >= m_FrameSlotCount)
        {
            PublishFailure(
                FrameCaptureResultStatus::SourceUnavailable,
                source ? source->FrameNumber : 0);
            return FrameCaptureRecordStatus::PublishedFailure;
        }

        const uint32_t width = source->Texture->GetWidth();
        const uint32_t height = source->Texture->GetHeight();
        const RHI::Format format = source->Texture->GetFormat();

        uint32_t bytesPerPixel = 0;
        if (!TryGetBytesPerPixel(format, bytesPerPixel))
        {
            PublishFailure(FrameCaptureResultStatus::UnsupportedFormat, source->FrameNumber);
            return FrameCaptureRecordStatus::PublishedFailure;
        }

        uint32_t rowPitchBytes = 0;
        uint64_t byteCount = 0;
        if (!TryCalculateLayout(width, height, bytesPerPixel, rowPitchBytes, byteCount))
        {
            PublishFailure(FrameCaptureResultStatus::InvalidDimensions, source->FrameNumber);
            return FrameCaptureRecordStatus::PublishedFailure;
        }

        if ((source->Texture->GetUsage() & RHI::ResourceUsage::TransferSrc) == RHI::ResourceUsage::None)
        {
            PublishFailure(FrameCaptureResultStatus::SourceMissingTransferSrc, source->FrameNumber);
            return FrameCaptureRecordStatus::PublishedFailure;
        }

        ReadbackSlot& slot = m_ReadbackSlots[frameSlotIndex];
        if (!slot.Buffer || slot.SizeBytes < byteCount)
        {
            RHI::BufferDesc desc(byteCount, RHI::ResourceUsage::TransferDst, true, "FrameCaptureReadback");
            try
            {
                slot.Buffer = m_Device->CreateBuffer(desc);
            }
            catch (...)
            {
                slot.Buffer.reset();
            }

            if (!slot.Buffer)
            {
                slot.SizeBytes = 0;
                PublishFailure(FrameCaptureResultStatus::ReadbackBufferCreateFailed, source->FrameNumber);
                return FrameCaptureRecordStatus::PublishedFailure;
            }
            slot.SizeBytes = byteCount;
        }

        commandList->TextureBarrier(source->Texture, source->CurrentState, RHI::ResourceState::CopySource);
        commandList->CopyTextureToBuffer(source->Texture, slot.Buffer, width, height, 0);
        commandList->TextureBarrier(source->Texture, RHI::ResourceState::CopySource, source->RestoreState);

        m_PendingReadback.RequestId = m_ActiveRequest.RequestId;
        m_PendingReadback.FrameNumber = source->FrameNumber;
        m_PendingReadback.SlotIndex = frameSlotIndex;
        m_PendingReadback.Width = width;
        m_PendingReadback.Height = height;
        m_PendingReadback.Format = format;
        m_PendingReadback.BytesPerPixel = bytesPerPixel;
        m_PendingReadback.RowPitchBytes = rowPitchBytes;
        m_PendingReadback.ByteCount = byteCount;
        m_State = State::RecordedAwaitingSubmit;
        return FrameCaptureRecordStatus::Recorded;
    }

    void FrameCaptureReadbackHelper::PublishCompletedFrameSlot(uint32_t frameSlotIndex)
    {
        Thread::ScopedLock lock(m_Mutex);

        if (m_State != State::PendingGpu || frameSlotIndex != m_PendingReadback.SlotIndex)
        {
            return;
        }

        ReadbackSlot& slot = m_ReadbackSlots[frameSlotIndex];
        if (!slot.Buffer)
        {
            PublishFailure(FrameCaptureResultStatus::MapFailed, m_PendingReadback.FrameNumber);
            return;
        }

        void* mappedData = nullptr;
        try
        {
            mappedData = slot.Buffer->Map(0, m_PendingReadback.ByteCount);
        }
        catch (...)
        {
            PublishFailure(FrameCaptureResultStatus::MapFailed, m_PendingReadback.FrameNumber);
            return;
        }

        if (!mappedData)
        {
            PublishFailure(FrameCaptureResultStatus::MapFailed, m_PendingReadback.FrameNumber);
            return;
        }

        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::Success;
        frame.RequestId = m_PendingReadback.RequestId;
        frame.FrameNumber = m_PendingReadback.FrameNumber;
        frame.Width = m_PendingReadback.Width;
        frame.Height = m_PendingReadback.Height;
        frame.Format = m_PendingReadback.Format;
        frame.BytesPerPixel = m_PendingReadback.BytesPerPixel;
        frame.RowPitchBytes = m_PendingReadback.RowPitchBytes;
        frame.Pixels.resize(static_cast<size_t>(m_PendingReadback.ByteCount));
        if (!frame.Pixels.empty())
        {
            std::memcpy(frame.Pixels.data(), mappedData, frame.Pixels.size());
        }
        slot.Buffer->Unmap();

        m_CompletedFrame = frame;
        m_PendingReadback = PendingReadback{};
        m_State = State::CompletedUnconsumed;
    }

    bool FrameCaptureReadbackHelper::TryConsumeCapturedFrame(CapturedFrame& outFrame)
    {
        Thread::ScopedLock lock(m_Mutex);

        if (m_State != State::CompletedUnconsumed)
        {
            outFrame = CapturedFrame{};
            return false;
        }

        outFrame = m_CompletedFrame;
        m_CompletedFrame = CapturedFrame{};
        m_PendingReadback = PendingReadback{};
        m_ActiveRequest = FrameCaptureRequestSnapshot{};
        m_State = State::Idle;
        return true;
    }

    bool FrameCaptureReadbackHelper::TryGetBytesPerPixel(RHI::Format format, uint32_t& outBytesPerPixel)
    {
        switch (format)
        {
        case RHI::Format::R8G8B8A8_UNORM:
        case RHI::Format::R8G8B8A8_SRGB:
        case RHI::Format::B8G8R8A8_UNORM:
        case RHI::Format::B8G8R8A8_SRGB:
            outBytesPerPixel = 4;
            return true;
        case RHI::Format::R16G16B16A16_FLOAT:
            outBytesPerPixel = 8;
            return true;
        default:
            outBytesPerPixel = 0;
            return false;
        }
    }

    bool FrameCaptureReadbackHelper::TryCalculateLayout(
        uint32_t width,
        uint32_t height,
        uint32_t bytesPerPixel,
        uint32_t& outRowPitchBytes,
        uint64_t& outByteCount)
    {
        if (width == 0 || height == 0 || bytesPerPixel == 0)
        {
            outRowPitchBytes = 0;
            outByteCount = 0;
            return false;
        }

        if (width > std::numeric_limits<uint32_t>::max() / bytesPerPixel)
        {
            outRowPitchBytes = 0;
            outByteCount = 0;
            return false;
        }

        outRowPitchBytes = width * bytesPerPixel;
        if (static_cast<uint64_t>(height) > std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(outRowPitchBytes))
        {
            outRowPitchBytes = 0;
            outByteCount = 0;
            return false;
        }

        outByteCount = static_cast<uint64_t>(outRowPitchBytes) * static_cast<uint64_t>(height);
        return true;
    }

    void FrameCaptureReadbackHelper::PublishFailure(FrameCaptureResultStatus status, uint64_t frameNumber)
    {
        CapturedFrame frame;
        frame.Status = status;
        frame.RequestId = m_ActiveRequest.RequestId;
        frame.FrameNumber = frameNumber;

        m_CompletedFrame = frame;
        m_PendingReadback = PendingReadback{};
        m_State = State::CompletedUnconsumed;
    }

} // namespace NorvesLib::Core::Rendering
