#pragma once

#include "BridgeDeterministicCapture.h"

#include <cstdint>

namespace Game::Bridge
{
    /** @brief 決定的な撮影と PNG の出力先を指定する。 */
    struct BridgeCaptureParams
    {
        NorvesLib::Core::Container::String CaptureDir;
        NorvesLib::Core::Container::String OutputName;
        uint32_t SettleFrames = 60;
        uint32_t MaxWidth = 640;
        uint32_t MaxHeight = 360;
    };

    /** @brief 有効なら空文字列、無効なら理由コードを返す。 */
    [[nodiscard]] NorvesLib::Core::Container::String ValidateBridgeCaptureParams(const BridgeCaptureParams& params);

    enum class BridgeCaptureAcceptStatus : uint8_t
    {
        Accepted,
        Disabled,
        InvalidParams,
        Busy
    };

    struct BridgeCaptureAcceptResult
    {
        BridgeCaptureAcceptStatus Status = BridgeCaptureAcceptStatus::Disabled;
        NorvesLib::Core::Container::String CaptureId;
        NorvesLib::Core::Container::String Reason;
    };

    /** @brief 成功と失敗のどちらか一方の欄だけを埋める完了通知。 */
    struct BridgeCaptureCompletedEvent
    {
        NorvesLib::Core::Container::String CaptureId;
        bool bSucceeded = false;
        NorvesLib::Core::Container::String Path;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint64_t Frame = 0;
        NorvesLib::Core::Container::String PixelDigestHex;
        uint64_t FileBytes = 0;
        NorvesLib::Core::Container::String FailureCode;
        NorvesLib::Core::Container::String FailureMessage;
    };

    /**
     * @brief SDK に依存せず、撮影の受付から結果の回収と完了通知までを管理する。
     *
     * 各操作は GameThread で呼び、Drive は EndFrame 後、次の AdvanceFrame 前に毎フレーム呼ぶ。
     * 受け付けた供給元の Context は、失敗後の回収を含め IsBusy が偽になるまで生存させる。
     */
    class BridgeCaptureProtocol final
    {
    public:
        BridgeCaptureProtocol() = default;
        BridgeCaptureProtocol(const BridgeCaptureProtocol&) = delete;
        BridgeCaptureProtocol& operator=(const BridgeCaptureProtocol&) = delete;
        BridgeCaptureProtocol(BridgeCaptureProtocol&&) = delete;
        BridgeCaptureProtocol& operator=(BridgeCaptureProtocol&&) = delete;

        [[nodiscard]] BridgeCaptureAcceptResult Accept(
            const BridgeCaptureParams& params, const BridgeDeterministicFrameSource& source);
        [[nodiscard]] bool IsBusy() const;
        [[nodiscard]] static bool IsCaptureAvailable(const BridgeDeterministicFrameSource& source);

        /** @brief 完了通知を組み立てたフレームだけ真を返す。 */
        bool Drive(BridgeCaptureCompletedEvent& outEvent);

    private:
        enum class State : uint8_t
        {
            Idle,
            Pending,
            Running,
            Draining
        };

        [[nodiscard]] BridgeDeterministicFrameSource MakeWrappedSource();
        static bool WrappedIsDeterministicEnabled(void* context);
        static bool WrappedIsSceneReady(void* context);
        static void WrappedBeginEpoch(void* context);
        static uint64_t WrappedGetEpochFrames(void* context);
        static uint64_t WrappedGetNextFrameNumber(void* context);
        static NorvesLib::Core::Rendering::FrameCaptureRequestResult WrappedRequestFrameCapture(void* context);
        static bool WrappedTryConsumeCapturedFrame(
            void* context, NorvesLib::Core::Rendering::CapturedFrame& frame);

        void FinishOrDrain();
        void EnterIdle();

        State m_State = State::Idle;
        BridgeCaptureParams m_Params;
        BridgeDeterministicFrameSource m_Source;
        BridgeDeterministicCaptureSession m_Session;
        NorvesLib::Core::Container::String m_CaptureId;
        uint64_t m_NextCaptureId = 1;
        uint64_t m_RequestId = 0;
        uint32_t m_DrainFrames = 0;
        bool m_bRequestIssued = false;
        bool m_bResultConsumed = false;
    };
} // namespace Game::Bridge
