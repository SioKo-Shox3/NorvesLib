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
    enum class FrameCaptureRecordStatus : uint8_t
    {
        NoRequest,
        Recorded,
        PublishedFailure,
        /** @brief 要求した試料数に届かないため、要求を保留へ戻した */
        Deferred
    };

    class FrameCaptureReadbackHelper
    {
    public:
        bool Initialize(RHI::IDevice* device, uint32_t frameSlotCount);
        void Shutdown();

        FrameCaptureRequestResult RequestFrameCapture();
        FrameCaptureRequestResult RequestFrameCapture(const FrameCaptureRequest& request);
        bool TrySnapshotPendingRequest(FrameCaptureRequestSnapshot& outSnapshot);
        bool TryClaimPendingRequest(
            const FrameCaptureRequestSnapshot& packetSnapshot,
            FrameCaptureRequestSnapshot& outClaimedSnapshot);
        bool AbandonAssignedRequest(
            const FrameCaptureRequestSnapshot& snapshot,
            uint64_t frameNumber,
            FrameCaptureResultStatus failureStatus) noexcept;
        bool CommitRecordedCopy(const FrameCaptureRequestSnapshot& snapshot) noexcept;
        FrameCaptureRecordStatus TryRecordCopy(
            uint32_t frameSlotIndex,
            RHI::ICommandList* commandList,
            const FrameCaptureRequestSnapshot& snapshot,
            const FrameCaptureSourceSet& sources);
        void PublishCompletedFrameSlot(uint32_t frameSlotIndex);
        bool TryConsumeCapturedFrame(CapturedFrame& outFrame);

    private:
        enum class State : uint8_t
        {
            Uninitialized,
            Idle,
            Requested,
            AssignedToFrame,
            RecordedAwaitingSubmit,
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

        /** @brief bIncludeSceneColor の要求で、同じフレームのシーンの色を写した読み出し */
        struct PendingSceneColorReadback
        {
            bool bRecorded = false;
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::Format Format = RHI::Format::UNKNOWN;
            uint32_t BytesPerPixel = 0;
            uint32_t RowPitchBytes = 0;
            uint64_t ByteCount = 0;
            float PreExposure = 0.0f;
            FrameCaptureDisplayInfo Display;
        };

        /** @brief bIncludeAov の要求で写した 1 枚の配置 */
        struct PendingImageReadback
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::Format Format = RHI::Format::UNKNOWN;
            uint32_t BytesPerPixel = 0;
            uint32_t RowPitchBytes = 0;
            uint64_t ByteCount = 0;
        };

        /** @brief AOV の枚数（表面・幾何・照明） */
        static constexpr uint32_t AovImageCount = 3;
        /** @brief bIncludeAov の要求で、同じフレームの GBuffer の生の値と照明の結果（表面・幾何・照明の 3 枚）を写した読み出し */
        struct PendingAovReadback
        {
            bool bRecorded = false;
            PendingImageReadback Images[AovImageCount];
            FrameCaptureAovCamera Camera;
        };
        /** @brief bIncludeAtmosphere の要求で、同じフレームの大気の書き出し（1 枚）を写した読み出し */
        struct PendingAtmosphereReadback
        {
            bool bRecorded = false;
            PendingImageReadback Image;
            FrameCaptureAtmosphereInfo Info;
        };

        static bool TryGetBytesPerPixel(RHI::Format format, uint32_t& outBytesPerPixel);
        /** @brief 読み出し先のバッファを byteCount 以上にそろえる（作れなければ false） */
        bool EnsureReadbackSlot(ReadbackSlot& slot, uint64_t byteCount);
        /** @brief 写す 1 枚の形式・配置を確かめ、読み出し先をそろえる（失敗の理由を outStatus に入れて false） */
        bool TryPrepareImageReadback(const FrameCaptureSource& source,
                                     ReadbackSlot& slot,
                                     PendingImageReadback& outImage,
                                     FrameCaptureResultStatus& outStatus);
        /** @brief 写した 1 枚を読み出し先から outImage へ移す（写せなければ false） */
        static bool TryCopyImageReadback(ReadbackSlot& slot,
                                         const PendingImageReadback& image,
                                         CapturedImage& outImage);
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
        FrameCaptureRequestSnapshot m_ActiveRequest;
        PendingReadback m_PendingReadback;
        PendingSceneColorReadback m_PendingSceneColor;
        PendingAovReadback m_PendingAov;
        PendingAtmosphereReadback m_PendingAtmosphere;
        CapturedFrame m_CompletedFrame;
        Container::VariableArray<ReadbackSlot> m_ReadbackSlots;
        Container::VariableArray<ReadbackSlot> m_SceneColorReadbackSlots;
        /** @brief AOV の表面・幾何・照明の読み出し先（フレームの枠ごと） */
        Container::VariableArray<ReadbackSlot> m_AovReadbackSlots[AovImageCount];
        /** @brief 大気の書き出しの読み出し先（フレームの枠ごと） */
        Container::VariableArray<ReadbackSlot> m_AtmosphereReadbackSlots;
    };

} // namespace NorvesLib::Core::Rendering
