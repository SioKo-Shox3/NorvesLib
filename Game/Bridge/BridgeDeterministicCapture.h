#pragma once

#include "Container/String.h"
#include "Rendering/FrameCaptureTypes.h"

#include <cstddef>
#include <cstdint>

namespace Game::Bridge
{
    inline constexpr uint32_t BridgeDeterministicCaptureMaxWidth = 1920;
    inline constexpr uint32_t BridgeDeterministicCaptureMaxHeight = 1080;
    inline constexpr size_t BridgeDeterministicCaptureMaxPngBytes = 8u * 1024u * 1024u;

    struct BridgeDeterministicCaptureRequest
    {
        uint32_t SettleFrames = 60;
        // PNG エンコーダの固定上限（現在 640×360）でも縮小される。拡大はしない。
        uint32_t MaxWidth = 640;
        uint32_t MaxHeight = 360;
    };

    /** @brief 有効なら空文字列、無効なら英語の理由コードを返す。 */
    [[nodiscard]] NorvesLib::Core::Container::String ValidateBridgeDeterministicCaptureRequest(
        const BridgeDeterministicCaptureRequest& request);

    /**
     * @brief GameThread で借用する撮影の供給元。すべての関数ポインタが必須。
     *
     * BeginEpoch は CPU と描画の両方を、次のフレームが 0 番になるようにリセットする。
     * GetNextFrameNumber は、次に提出する描画フレームの通し番号（CapturedFrame::FrameNumber と同じ系列）。
     * エポックの先頭の通し番号を保存するために使い、非同期の受取時刻を撮影時刻と取り違えない。
     * Context の所有者はセッションが終了するまで生存させる。
     */
    struct BridgeDeterministicFrameSource
    {
        void* Context = nullptr;
        bool (*IsDeterministicEnabled)(void*) = nullptr;
        bool (*IsSceneReady)(void*) = nullptr;
        void (*BeginEpoch)(void*) = nullptr;
        uint64_t (*GetEpochFrames)(void*) = nullptr;
        NorvesLib::Core::Rendering::FrameCaptureRequestResult (*RequestFrameCapture)(void*) = nullptr;
        bool (*TryConsumeCapturedFrame)(void*, NorvesLib::Core::Rendering::CapturedFrame&) = nullptr;
        uint64_t (*GetNextFrameNumber)(void*) = nullptr;
    };

    /** @brief 現在の GEngine を借用する供給元。SDK は不要。未初期化なら Begin が拒否する。 */
    [[nodiscard]] BridgeDeterministicFrameSource MakeEngineDeterministicFrameSource();

    enum class BridgeDeterministicCaptureStatus : uint8_t
    {
        Idle,
        WaitingForScene,
        Settling,
        WaitingForCapture,
        Completed,
        Failed
    };

    struct BridgeDeterministicCaptureResult
    {
        BridgeDeterministicCaptureStatus Status = BridgeDeterministicCaptureStatus::Idle;
        NorvesLib::Core::Container::String ErrorCode;
        NorvesLib::Core::Container::VariableArray<uint8_t> PngBytes;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint64_t EpochFrameNumber = 0;
        // 縮小前の読み戻し画素の生バイトを、行の詰め物を除いて FNV-1a 64 ビットで数える。
        // RGBA と BGRA の並べ替えはしないため、比較時は供給元の形式を揃える。
        uint64_t PixelHash = 0;
        uint64_t PngHash = 0;
    };

    /**
     * @brief 1 枚の決定的な撮影を管理する、SDK に依存しない状態機械。
     *
     * Begin と Tick は GameThread の RenderWorld::EndFrame 後、次の AdvanceFrame より前に呼ぶ。
     * Begin の後は次のフレームから Tick を毎フレーム 1 回呼ぶ。待機は合計 SettleFrames + 240 回まで。
     * SettleFrames 番を観測したら次の描画フレームを要求する（通常、結果は SettleFrames + 1 番）。
     * 呼び出しの欠落や描画の取りこぼしで対象フレームが変わったら、異なる画像を成功として返さず失敗する。
     * 実行中は撮影要求と結果の受取、およびエポックの操作をこのセッションに専有させる。
     * タイムアウトしても RenderWorld の撮影要求は取り消せない。呼び出し側が残った結果を回収する。
     */
    class BridgeDeterministicCaptureSession final
    {
    public:
        BridgeDeterministicCaptureSession() = default;
        BridgeDeterministicCaptureSession(const BridgeDeterministicCaptureSession&) = delete;
        BridgeDeterministicCaptureSession& operator=(const BridgeDeterministicCaptureSession&) = delete;

        /** @brief 受理なら空文字列、拒否なら理由コード。実行中の拒否では進行中の結果を保持する。 */
        [[nodiscard]] NorvesLib::Core::Container::String Begin(
            const BridgeDeterministicCaptureRequest& request,
            const BridgeDeterministicFrameSource& source);

        BridgeDeterministicCaptureStatus Tick();
        [[nodiscard]] bool IsInProgress() const;
        [[nodiscard]] const BridgeDeterministicCaptureResult& GetResult() const { return m_Result; }

    private:
        void BeginEpochIfReady();
        void Fail(const char* code);
        void Complete(const NorvesLib::Core::Rendering::CapturedFrame& frame);

        BridgeDeterministicCaptureRequest m_Request;
        BridgeDeterministicFrameSource m_Source;
        BridgeDeterministicCaptureResult m_Result;
        uint32_t m_WaitFrames = 0;
        uint64_t m_EpochFirstFrameNumber = 0;
        uint64_t m_LastEpochFrames = 0;
        uint64_t m_RequestId = 0;
        uint64_t m_RequestedFrameNumber = 0;
    };
} // namespace Game::Bridge
