// ストリーミングするセルの状態遷移を実装する。
#include "Streaming/StreamingCell.h"

#include "Streaming/IStreamingPayloadHandler.h"

#include <cassert>
#include <cstdlib>

namespace NorvesLib::Core::Streaming
{
    StreamingCell::StreamingCell(const StreamingCellDesc* desc, StreamingRetryPolicy policy)
        : m_Desc(desc), m_RetryPolicy(policy)
    {
        assert(desc != nullptr && "セルの記述が必要です");
        if (desc == nullptr)
        {
            std::abort();
        }
    }

    EStreamingCellState StreamingCell::GetState() const
    {
        return m_State;
    }

    StreamingCellId StreamingCell::GetId() const
    {
        return m_Desc->Id;
    }

    const StreamingCellDesc& StreamingCell::GetDesc() const
    {
        return *m_Desc;
    }

    uint32_t StreamingCell::GetFailedAttempts() const
    {
        return m_FailedAttempts;
    }

    bool StreamingCell::CanBeginPrepare(double nowMs) const
    {
        return m_State == EStreamingCellState::Unloaded &&
            (!m_bRetryWaiting || nowMs >= m_NextPrepareTimeMs);
    }

    bool StreamingCell::BeginPrepare(double nowMs)
    {
        if (!CanBeginPrepare(nowMs))
        {
            return false;
        }

        m_CancelToken = Container::MakeShared<StreamingCancelToken>();
        m_bRetryWaiting = false;
        m_State = EStreamingCellState::Preparing;
        return true;
    }

    Container::TSharedPtr<StreamingCancelToken> StreamingCell::GetCancelToken() const
    {
        return m_CancelToken;
    }

    void StreamingCell::RequestUnload()
    {
        switch (m_State)
        {
        case EStreamingCellState::Preparing:
            m_CancelToken->Cancel();
            break;
        case EStreamingCellState::ReadyToCommit:
            m_State = EStreamingCellState::Unloaded;
            m_bNeedsDiscardPrepared = true;
            break;
        case EStreamingCellState::Committing:
            m_bUnloadRequested = true;
            break;
        case EStreamingCellState::Active:
        case EStreamingCellState::Dormant:
            BeginUnloading();
            break;
        case EStreamingCellState::Failed:
            m_FailedAttempts = 0;
            m_bRetryWaiting = false;
            m_State = EStreamingCellState::Unloaded;
            break;
        case EStreamingCellState::Unloaded:
        case EStreamingCellState::Unloading:
            break;
        }
    }

    void StreamingCell::SetWantDormant(bool bDormant)
    {
        m_bWantDormant = bDormant;
        if (m_State == EStreamingCellState::Active && bDormant)
        {
            m_State = EStreamingCellState::Dormant;
        }
        else if (m_State == EStreamingCellState::Dormant && !bDormant)
        {
            m_State = EStreamingCellState::Active;
        }
    }

    bool StreamingCell::OnPrepareFinished(EStreamingPrepareResult result, double nowMs)
    {
        if (m_State != EStreamingCellState::Preparing ||
            (result != EStreamingPrepareResult::Success &&
             result != EStreamingPrepareResult::Failed &&
             result != EStreamingPrepareResult::Cancelled))
        {
            return false;
        }

        const bool bCancelled = m_CancelToken->IsCancelled() || result == EStreamingPrepareResult::Cancelled;
        m_CancelToken.reset();
        if (bCancelled)
        {
            m_State = EStreamingCellState::Unloaded;
            m_bNeedsDiscardPrepared = true;
        }
        else if (result == EStreamingPrepareResult::Success)
        {
            m_State = EStreamingCellState::ReadyToCommit;
        }
        else
        {
            ++m_FailedAttempts;
            m_bNeedsDiscardPrepared = true;
            if (m_FailedAttempts >= m_RetryPolicy.MaxPrepareAttempts)
            {
                m_State = EStreamingCellState::Failed;
                m_bRetryWaiting = false;
            }
            else
            {
                m_NextPrepareTimeMs = nowMs + m_RetryPolicy.RetryDelayMs;
                m_bRetryWaiting = true;
                m_State = EStreamingCellState::Unloaded;
            }
        }
        return true;
    }

    bool StreamingCell::BeginCommit()
    {
        if (m_State != EStreamingCellState::ReadyToCommit ||
            m_Desc->Payloads.size() > static_cast<size_t>(kInvalidStreamingPayloadIndex))
        {
            return false;
        }

        m_Cursors.clear();
        m_Cursors.resize(m_Desc->Payloads.size());
        m_StartedPayloadCount = 0;
        m_bUnloadRequested = false;
        m_bCommitFailed = false;
        if (m_Cursors.empty())
        {
            m_CurrentPayloadIndex = kInvalidStreamingPayloadIndex;
            m_State = m_bWantDormant ? EStreamingCellState::Dormant : EStreamingCellState::Active;
        }
        else
        {
            m_CurrentPayloadIndex = 0;
            m_State = EStreamingCellState::Committing;
        }
        return true;
    }

    bool StreamingCell::OnCommitStep(EStreamingStepResult result)
    {
        if (m_State != EStreamingCellState::Committing ||
            (result != EStreamingStepResult::InProgress &&
             result != EStreamingStepResult::Done &&
             result != EStreamingStepResult::Failed))
        {
            return false;
        }

        m_StartedPayloadCount = m_CurrentPayloadIndex + 1;
        if (result == EStreamingStepResult::Failed)
        {
            ++m_FailedAttempts;
            m_bCommitFailed = true;
            BeginUnloading();
        }
        else if (m_bUnloadRequested)
        {
            BeginUnloading();
        }
        else if (result == EStreamingStepResult::Done)
        {
            ++m_CurrentPayloadIndex;
            if (m_CurrentPayloadIndex == m_Cursors.size())
            {
                m_CurrentPayloadIndex = kInvalidStreamingPayloadIndex;
                m_State = m_bWantDormant ? EStreamingCellState::Dormant : EStreamingCellState::Active;
            }
        }
        return true;
    }

    bool StreamingCell::OnUnloadStep(EStreamingStepResult result, double nowMs)
    {
        if (m_State != EStreamingCellState::Unloading ||
            (result != EStreamingStepResult::InProgress &&
             result != EStreamingStepResult::Done &&
             result != EStreamingStepResult::Failed))
        {
            return false;
        }

        if (result != EStreamingStepResult::InProgress)
        {
            if (m_CurrentPayloadIndex > 0)
            {
                --m_CurrentPayloadIndex;
                m_Cursors[m_CurrentPayloadIndex] = StreamingStepCursor{};
            }
            else
            {
                FinishUnloading(nowMs);
            }
        }
        return true;
    }

    uint32_t StreamingCell::GetCurrentPayloadIndex() const
    {
        if (m_State != EStreamingCellState::Committing && m_State != EStreamingCellState::Unloading)
        {
            return kInvalidStreamingPayloadIndex;
        }
        return m_CurrentPayloadIndex;
    }

    StreamingStepCursor& StreamingCell::GetCurrentCursor()
    {
        if ((m_State != EStreamingCellState::Committing && m_State != EStreamingCellState::Unloading) ||
            m_CurrentPayloadIndex >= m_Cursors.size())
        {
            assert(false && "操作中のペイロードがありません");
            return m_InvalidCursor;
        }
        return m_Cursors[m_CurrentPayloadIndex];
    }

    bool StreamingCell::NeedsDiscardPrepared() const
    {
        return m_bNeedsDiscardPrepared;
    }

    void StreamingCell::ClearDiscardPrepared()
    {
        m_bNeedsDiscardPrepared = false;
    }

    void StreamingCell::BeginUnloading()
    {
        m_bUnloadRequested = false;
        if (m_StartedPayloadCount == 0)
        {
            // 一歩もコミットしていないセルには戻す手続きがない。
            FinishUnloading(0.0);
            return;
        }

        m_CurrentPayloadIndex = m_StartedPayloadCount - 1;
        m_Cursors[m_CurrentPayloadIndex] = StreamingStepCursor{};
        m_State = EStreamingCellState::Unloading;
    }

    void StreamingCell::FinishUnloading(double nowMs)
    {
        m_Cursors.clear();
        m_CurrentPayloadIndex = kInvalidStreamingPayloadIndex;
        m_StartedPayloadCount = 0;
        m_bNeedsDiscardPrepared = true;
        if (m_bCommitFailed && m_FailedAttempts >= m_RetryPolicy.MaxPrepareAttempts)
        {
            m_State = EStreamingCellState::Failed;
            m_bRetryWaiting = false;
        }
        else
        {
            m_State = EStreamingCellState::Unloaded;
            if (m_bCommitFailed)
            {
                m_NextPrepareTimeMs = nowMs + m_RetryPolicy.RetryDelayMs;
                m_bRetryWaiting = true;
            }
        }
        m_bCommitFailed = false;
    }
} // namespace NorvesLib::Core::Streaming
