// ストリーミングするセルの状態遷移を定義する。
#pragma once

#include "Container/PointerTypes.h"
#include "Streaming/StreamingTypes.h"

namespace NorvesLib::Core::Streaming
{
    class StreamingCancelToken;

    struct StreamingRetryPolicy
    {
        uint32_t MaxPrepareAttempts = 3;
        double RetryDelayMs = 1000.0;
    };

    inline constexpr uint32_t kInvalidStreamingPayloadIndex = UINT32_MAX;

    class StreamingCell
    {
    public:
        // 記述は非所有で、セルより長く生存し、内容が変わらないこと。
        StreamingCell(const StreamingCellDesc* desc, StreamingRetryPolicy policy);

        EStreamingCellState GetState() const;
        StreamingCellId GetId() const;
        const StreamingCellDesc& GetDesc() const;
        uint32_t GetFailedAttempts() const;

        bool CanBeginPrepare(double nowMs) const;
        bool BeginPrepare(double nowMs);
        Container::TSharedPtr<StreamingCancelToken> GetCancelToken() const;
        void RequestUnload();
        void SetWantDormant(bool bDormant);

        bool OnPrepareFinished(EStreamingPrepareResult result, double nowMs);
        bool BeginCommit();
        bool OnCommitStep(EStreamingStepResult result);
        // コミット失敗後の待ち時間は最後のアンロード歩の時刻から数える。
        bool OnUnloadStep(EStreamingStepResult result, double nowMs);

        // 添字とカーソルは Committing または Unloading の間だけ有効。
        uint32_t GetCurrentPayloadIndex() const;
        // アンロードに入るとペイロードごとに初期値から始まる。
        StreamingStepCursor& GetCurrentCursor();

        // 破棄が必要な間は保持し、呼び手が破棄してから解除する。
        bool NeedsDiscardPrepared() const;
        void ClearDiscardPrepared();

    private:
        void BeginUnloading();
        void FinishUnloading(double nowMs);

        const StreamingCellDesc* m_Desc;
        StreamingRetryPolicy m_RetryPolicy;
        EStreamingCellState m_State = EStreamingCellState::Unloaded;
        Container::TSharedPtr<StreamingCancelToken> m_CancelToken;
        Container::VariableArray<StreamingStepCursor> m_Cursors;
        StreamingStepCursor m_InvalidCursor;
        uint32_t m_CurrentPayloadIndex = kInvalidStreamingPayloadIndex;
        uint32_t m_StartedPayloadCount = 0;
        uint32_t m_FailedAttempts = 0;
        double m_NextPrepareTimeMs = 0.0;
        bool m_bRetryWaiting = false;
        bool m_bUnloadRequested = false;
        bool m_bCommitFailed = false;
        bool m_bWantDormant = false;
        bool m_bNeedsDiscardPrepared = false;
    };
} // namespace NorvesLib::Core::Streaming
