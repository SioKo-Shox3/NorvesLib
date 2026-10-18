#include "Rendering/FrameCaptureReadbackHelper.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"
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
        m_PendingSceneColor = PendingSceneColorReadback{};
        m_ReadbackSlots.clear();
        m_ReadbackSlots.resize(frameSlotCount);
        m_SceneColorReadbackSlots.clear();
        m_SceneColorReadbackSlots.resize(frameSlotCount);
        m_PendingAov = PendingAovReadback{};
        for (Container::VariableArray<ReadbackSlot>& slots : m_AovReadbackSlots)
        {
            slots.clear();
            slots.resize(frameSlotCount);
        }
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
        m_PendingSceneColor = PendingSceneColorReadback{};
        m_ReadbackSlots.clear();
        m_SceneColorReadbackSlots.clear();
        m_PendingAov = PendingAovReadback{};
        for (Container::VariableArray<ReadbackSlot>& slots : m_AovReadbackSlots)
        {
            slots.clear();
        }
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
        m_ActiveRequest.MinimumPathTracingSamples = request.MinimumPathTracingSamples;
        // シーンの色そのものを撮る要求では、同じ画を二重に読まない
        m_ActiveRequest.bIncludeSceneColor =
            request.bIncludeSceneColor && request.SourceKind != FrameCaptureSourceKind::SceneColor;
        m_ActiveRequest.bIncludeAov = request.bIncludeAov;
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

        // パストレーサーの累積が要求の試料数に届くまで、要求を保留へ戻して次のフレームで取り直す。
        if (sources.PathTracingSampleCount < m_ActiveRequest.MinimumPathTracingSamples)
        {
            m_State = State::Requested;
            return FrameCaptureRecordStatus::Deferred;
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
            NORVES_LOG_WARNING("FrameCapture",
                               "FRAME_CAPTURE_SOURCE width=%u height=%u format=%u usage=0x%llx",
                               width, height, static_cast<unsigned>(format),
                               static_cast<unsigned long long>(source->Texture->GetUsage()));
            PublishFailure(FrameCaptureResultStatus::SourceMissingTransferSrc, source->FrameNumber);
            return FrameCaptureRecordStatus::PublishedFailure;
        }

        // シーンの色も要求されていれば、写す前にすべてを確かめる（片方だけ写した結果を出さない）
        PendingSceneColorReadback sceneColorReadback;
        if (m_ActiveRequest.bIncludeSceneColor)
        {
            const FrameCaptureSource& sceneColor = sources.SceneColor;
            if (!sceneColor.Texture || !(sources.SceneColorPreExposure > 0.0f))
            {
                PublishFailure(FrameCaptureResultStatus::SourceUnavailable, source->FrameNumber);
                return FrameCaptureRecordStatus::PublishedFailure;
            }
            sceneColorReadback.Width = sceneColor.Texture->GetWidth();
            sceneColorReadback.Height = sceneColor.Texture->GetHeight();
            sceneColorReadback.Format = sceneColor.Texture->GetFormat();
            sceneColorReadback.PreExposure = sources.SceneColorPreExposure;
            sceneColorReadback.Display = sources.SceneColorDisplay;
            if (!TryGetBytesPerPixel(sceneColorReadback.Format, sceneColorReadback.BytesPerPixel))
            {
                PublishFailure(FrameCaptureResultStatus::UnsupportedFormat, source->FrameNumber);
                return FrameCaptureRecordStatus::PublishedFailure;
            }
            if (!TryCalculateLayout(sceneColorReadback.Width,
                                    sceneColorReadback.Height,
                                    sceneColorReadback.BytesPerPixel,
                                    sceneColorReadback.RowPitchBytes,
                                    sceneColorReadback.ByteCount))
            {
                PublishFailure(FrameCaptureResultStatus::InvalidDimensions, source->FrameNumber);
                return FrameCaptureRecordStatus::PublishedFailure;
            }
            if ((sceneColor.Texture->GetUsage() & RHI::ResourceUsage::TransferSrc) == RHI::ResourceUsage::None)
            {
                PublishFailure(FrameCaptureResultStatus::SourceMissingTransferSrc, source->FrameNumber);
                return FrameCaptureRecordStatus::PublishedFailure;
            }
            if (!EnsureReadbackSlot(m_SceneColorReadbackSlots[frameSlotIndex], sceneColorReadback.ByteCount))
            {
                PublishFailure(FrameCaptureResultStatus::ReadbackBufferCreateFailed, source->FrameNumber);
                return FrameCaptureRecordStatus::PublishedFailure;
            }
            sceneColorReadback.bRecorded = true;
        }

        // GBuffer の生の値も要求されていれば、表面・幾何の 2 枚とも同じフレームから写せることを先に確かめる
        PendingAovReadback aovReadback;
        if (m_ActiveRequest.bIncludeAov)
        {
            const FrameCaptureSource* aovSources[2] = {&sources.AovSurface, &sources.AovGeometry};
            for (uint32_t imageIndex = 0; imageIndex < 2u; ++imageIndex)
            {
                FrameCaptureResultStatus aovStatus = FrameCaptureResultStatus::SourceUnavailable;
                if (aovSources[imageIndex]->FrameNumber != source->FrameNumber ||
                    !TryPrepareImageReadback(*aovSources[imageIndex],
                                             m_AovReadbackSlots[imageIndex][frameSlotIndex],
                                             aovReadback.Images[imageIndex],
                                             aovStatus))
                {
                    PublishFailure(aovStatus, source->FrameNumber);
                    return FrameCaptureRecordStatus::PublishedFailure;
                }
            }
            aovReadback.Camera = sources.AovCamera;
            aovReadback.bRecorded = true;
        }

        ReadbackSlot& slot = m_ReadbackSlots[frameSlotIndex];
        if (!EnsureReadbackSlot(slot, byteCount))
        {
            PublishFailure(FrameCaptureResultStatus::ReadbackBufferCreateFailed, source->FrameNumber);
            return FrameCaptureRecordStatus::PublishedFailure;
        }

        commandList->TextureBarrier(source->Texture, source->CurrentState, RHI::ResourceState::CopySource);
        commandList->CopyTextureToBuffer(source->Texture, slot.Buffer, width, height, 0);
        commandList->TextureBarrier(source->Texture, RHI::ResourceState::CopySource, source->RestoreState);
        if (sceneColorReadback.bRecorded)
        {
            const FrameCaptureSource& sceneColor = sources.SceneColor;
            commandList->TextureBarrier(sceneColor.Texture, sceneColor.CurrentState, RHI::ResourceState::CopySource);
            commandList->CopyTextureToBuffer(sceneColor.Texture,
                                             m_SceneColorReadbackSlots[frameSlotIndex].Buffer,
                                             sceneColorReadback.Width,
                                             sceneColorReadback.Height,
                                             0);
            commandList->TextureBarrier(sceneColor.Texture, RHI::ResourceState::CopySource, sceneColor.RestoreState);
        }
        m_PendingSceneColor = sceneColorReadback;
        if (aovReadback.bRecorded)
        {
            const FrameCaptureSource* aovSources[2] = {&sources.AovSurface, &sources.AovGeometry};
            for (uint32_t imageIndex = 0; imageIndex < 2u; ++imageIndex)
            {
                const FrameCaptureSource& aovSource = *aovSources[imageIndex];
                commandList->TextureBarrier(aovSource.Texture, aovSource.CurrentState, RHI::ResourceState::CopySource);
                commandList->CopyTextureToBuffer(aovSource.Texture,
                                                 m_AovReadbackSlots[imageIndex][frameSlotIndex].Buffer,
                                                 aovReadback.Images[imageIndex].Width,
                                                 aovReadback.Images[imageIndex].Height,
                                                 0);
                commandList->TextureBarrier(aovSource.Texture, RHI::ResourceState::CopySource, aovSource.RestoreState);
            }
        }
        m_PendingAov = aovReadback;

        m_PendingReadback.RequestId = m_ActiveRequest.RequestId;
        m_PendingReadback.FrameNumber = source->FrameNumber;
        m_PendingReadback.SlotIndex = frameSlotIndex;
        m_PendingReadback.Width = width;
        m_PendingReadback.Height = height;
        m_PendingReadback.Format = format;
        m_PendingReadback.BytesPerPixel = bytesPerPixel;
        m_PendingReadback.RowPitchBytes = rowPitchBytes;
        m_PendingReadback.ByteCount = byteCount;
        m_CompletedFrame = CapturedFrame{};
        m_CompletedFrame.ColorSpace = source->ColorSpace;
        m_CompletedFrame.Transfer = source->Transfer;
        m_CompletedFrame.bHardwareSrgbEncode = source->bHardwareSrgbEncode;
        m_CompletedFrame.bShaderSrgbEncode = source->bShaderSrgbEncode;
        m_CompletedFrame.PathTracingSampleCount = sources.PathTracingSampleCount;
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
        frame.ColorSpace = m_CompletedFrame.ColorSpace;
        frame.Transfer = m_CompletedFrame.Transfer;
        frame.bHardwareSrgbEncode = m_CompletedFrame.bHardwareSrgbEncode;
        frame.bShaderSrgbEncode = m_CompletedFrame.bShaderSrgbEncode;
        frame.PathTracingSampleCount = m_CompletedFrame.PathTracingSampleCount;
        frame.Pixels.resize(static_cast<size_t>(m_PendingReadback.ByteCount));
        if (!frame.Pixels.empty())
        {
            std::memcpy(frame.Pixels.data(), mappedData, frame.Pixels.size());
        }
        slot.Buffer->Unmap();

        if (m_PendingSceneColor.bRecorded)
        {
            ReadbackSlot& sceneColorSlot = m_SceneColorReadbackSlots[frameSlotIndex];
            void* mappedSceneColor = nullptr;
            if (sceneColorSlot.Buffer)
            {
                try
                {
                    mappedSceneColor = sceneColorSlot.Buffer->Map(0, m_PendingSceneColor.ByteCount);
                }
                catch (...)
                {
                    mappedSceneColor = nullptr;
                }
            }
            if (!mappedSceneColor)
            {
                PublishFailure(FrameCaptureResultStatus::MapFailed, m_PendingReadback.FrameNumber);
                return;
            }
            CapturedSceneColor& sceneColor = frame.SceneColor;
            sceneColor.bValid = true;
            sceneColor.Width = m_PendingSceneColor.Width;
            sceneColor.Height = m_PendingSceneColor.Height;
            sceneColor.Format = m_PendingSceneColor.Format;
            sceneColor.BytesPerPixel = m_PendingSceneColor.BytesPerPixel;
            sceneColor.RowPitchBytes = m_PendingSceneColor.RowPitchBytes;
            sceneColor.PreExposure = m_PendingSceneColor.PreExposure;
            sceneColor.Display = m_PendingSceneColor.Display;
            sceneColor.Pixels.resize(static_cast<size_t>(m_PendingSceneColor.ByteCount));
            if (!sceneColor.Pixels.empty())
            {
                std::memcpy(sceneColor.Pixels.data(), mappedSceneColor, sceneColor.Pixels.size());
            }
            sceneColorSlot.Buffer->Unmap();
        }

        if (m_PendingAov.bRecorded)
        {
            CapturedImage* aovImages[2] = {&frame.Aov.Surface, &frame.Aov.Geometry};
            for (uint32_t imageIndex = 0; imageIndex < 2u; ++imageIndex)
            {
                if (!TryCopyImageReadback(m_AovReadbackSlots[imageIndex][frameSlotIndex],
                                          m_PendingAov.Images[imageIndex],
                                          *aovImages[imageIndex]))
                {
                    PublishFailure(FrameCaptureResultStatus::MapFailed, m_PendingReadback.FrameNumber);
                    return;
                }
            }
            frame.Aov.Camera = m_PendingAov.Camera;
            frame.Aov.bValid = true;
        }

        m_CompletedFrame = frame;
        m_PendingReadback = PendingReadback{};
        m_PendingSceneColor = PendingSceneColorReadback{};
        m_PendingAov = PendingAovReadback{};
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
        m_PendingSceneColor = PendingSceneColorReadback{};
        m_PendingAov = PendingAovReadback{};
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
        case RHI::Format::R32G32B32A32_FLOAT:
            outBytesPerPixel = 16;
            return true;
        case RHI::Format::R16G16_FLOAT:
            outBytesPerPixel = 4;
            return true;
        case RHI::Format::R16_FLOAT:
            outBytesPerPixel = 2;
            return true;
        default:
            outBytesPerPixel = 0;
            return false;
        }
    }

    bool FrameCaptureReadbackHelper::EnsureReadbackSlot(ReadbackSlot& slot, uint64_t byteCount)
    {
        if (slot.Buffer && slot.SizeBytes >= byteCount)
        {
            return true;
        }
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
            return false;
        }
        slot.SizeBytes = byteCount;
        return true;
    }

    bool FrameCaptureReadbackHelper::TryPrepareImageReadback(const FrameCaptureSource& source,
                                                             ReadbackSlot& slot,
                                                             PendingImageReadback& outImage,
                                                             FrameCaptureResultStatus& outStatus)
    {
        outImage = PendingImageReadback{};
        outStatus = FrameCaptureResultStatus::SourceUnavailable;
        if (!source.Texture)
        {
            return false;
        }
        outImage.Width = source.Texture->GetWidth();
        outImage.Height = source.Texture->GetHeight();
        outImage.Format = source.Texture->GetFormat();
        if (!TryGetBytesPerPixel(outImage.Format, outImage.BytesPerPixel))
        {
            outStatus = FrameCaptureResultStatus::UnsupportedFormat;
            return false;
        }
        if (!TryCalculateLayout(outImage.Width, outImage.Height, outImage.BytesPerPixel, outImage.RowPitchBytes,
                                outImage.ByteCount))
        {
            outStatus = FrameCaptureResultStatus::InvalidDimensions;
            return false;
        }
        if ((source.Texture->GetUsage() & RHI::ResourceUsage::TransferSrc) == RHI::ResourceUsage::None)
        {
            outStatus = FrameCaptureResultStatus::SourceMissingTransferSrc;
            return false;
        }
        if (!EnsureReadbackSlot(slot, outImage.ByteCount))
        {
            outStatus = FrameCaptureResultStatus::ReadbackBufferCreateFailed;
            return false;
        }
        outStatus = FrameCaptureResultStatus::Success;
        return true;
    }

    bool FrameCaptureReadbackHelper::TryCopyImageReadback(ReadbackSlot& slot,
                                                          const PendingImageReadback& image,
                                                          CapturedImage& outImage)
    {
        if (!slot.Buffer)
        {
            return false;
        }
        void* mappedData = nullptr;
        try
        {
            mappedData = slot.Buffer->Map(0, image.ByteCount);
        }
        catch (...)
        {
            mappedData = nullptr;
        }
        if (!mappedData)
        {
            return false;
        }
        outImage.Width = image.Width;
        outImage.Height = image.Height;
        outImage.Format = image.Format;
        outImage.BytesPerPixel = image.BytesPerPixel;
        outImage.RowPitchBytes = image.RowPitchBytes;
        outImage.Pixels.resize(static_cast<size_t>(image.ByteCount));
        if (!outImage.Pixels.empty())
        {
            std::memcpy(outImage.Pixels.data(), mappedData, outImage.Pixels.size());
        }
        slot.Buffer->Unmap();
        return true;
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

        // 撮影の失敗は利用者側で理由が失われやすいので、状態の名前をここで残す。
        NORVES_LOG_WARNING("FrameCapture",
                           "FRAME_CAPTURE_FAILED status=%s request=%llu frame=%llu source_kind=%u",
                           GetFrameCaptureResultStatusName(status),
                           static_cast<unsigned long long>(m_ActiveRequest.RequestId),
                           static_cast<unsigned long long>(frameNumber),
                           static_cast<unsigned>(m_ActiveRequest.SourceKind));

        m_CompletedFrame = frame;
        m_PendingReadback = PendingReadback{};
        m_PendingSceneColor = PendingSceneColorReadback{};
        m_PendingAov = PendingAovReadback{};
        m_State = State::CompletedUnconsumed;
    }

} // namespace NorvesLib::Core::Rendering
