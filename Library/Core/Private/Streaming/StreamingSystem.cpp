// セルのストリーミング全体を扱う。
#include "Streaming/StreamingSystem.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <utility>

namespace NorvesLib::Core::Streaming
{
    namespace
    {
        bool IsResident(EStreamingCellState state)
        {
            return state != EStreamingCellState::Unloaded && state != EStreamingCellState::Failed;
        }

        bool HasResidentBytes(EStreamingCellState state)
        {
            return state == EStreamingCellState::ReadyToCommit || state == EStreamingCellState::Committing ||
                state == EStreamingCellState::Active || state == EStreamingCellState::Dormant;
        }

        StreamingPayloadContext MakeContext(const StreamingCell& cell, uint32_t index)
        {
            return {cell.GetId(), index, &cell.GetDesc().Payloads[index]};
        }
    } // namespace

    StreamingSystem::CellEntry::CellEntry(const StreamingCellDesc* pDesc, StreamingRetryPolicy retry,
                                          StreamingScopeId scopeId)
        : Cell(pDesc, retry), ScopeId(scopeId)
    {
    }

    StreamingSystem::StreamingSystem()
        : m_Clock(&m_DefaultClock), m_Executor(&m_DefaultExecutor)
    {
    }

    StreamingSystem::~StreamingSystem()
    {
        Shutdown();
    }

    void StreamingSystem::SetEnvironment(IStreamingClock* pClock, IStreamingJobExecutor* pExecutor)
    {
        CancelAndWaitPrepares();
        m_Clock = pClock != nullptr ? pClock : &m_DefaultClock;
        m_Executor = pExecutor != nullptr ? pExecutor : &m_DefaultExecutor;
    }

    bool StreamingSystem::SetSettings(const StreamingSystemSettings& settings)
    {
        if (!ValidateRingSettings(settings.Ring))
        {
            return false;
        }
        m_Settings = settings;
        return true;
    }

    const StreamingSystemSettings& StreamingSystem::GetSettings() const
    {
        return m_Settings;
    }

    bool StreamingSystem::RegisterHandler(IStreamingPayloadHandler* pHandler)
    {
        if (m_bHasManifest || pHandler == nullptr)
        {
            return false;
        }
        const char* pKind = pHandler->GetKind();
        if (pKind == nullptr || *pKind == '\0')
        {
            return false;
        }
        for (const auto& entry : m_Handlers)
        {
            if (std::strcmp(entry.Kind.c_str(), pKind) == 0)
            {
                return false;
            }
        }
        m_Handlers.push_back({Container::String(pKind), pHandler});
        return true;
    }

    bool StreamingSystem::LoadManifestFromText(const Container::String& text, CellManifestError* pOutError,
                                               StreamingScopeId scopeId)
    {
        Container::VariableArray<const char*> kinds;
        for (const auto& handler : m_Handlers)
        {
            kinds.push_back(handler.Kind.c_str());
        }
        CellManifest manifest;
        if (!CellManifest::TryParse(text, {kinds.data(), kinds.size()}, manifest, pOutError))
        {
            return false;
        }

        Shutdown();
        m_Manifest = std::move(manifest);
        m_Cells.reserve(m_Manifest.GetCells().size());
        for (const auto& desc : m_Manifest.GetCells())
        {
            m_Cells.emplace_back(&desc, m_Settings.Retry, scopeId);
            auto& entry = m_Cells.back();
            for (const auto& payload : desc.Payloads)
            {
                for (const auto& handler : m_Handlers)
                {
                    if (handler.Kind == payload.Kind)
                    {
                        entry.Handlers.push_back(handler.Handler);
                        break;
                    }
                }
            }
        }
        m_bHasManifest = true;
        return true;
    }

    bool StreamingSystem::HasManifest() const
    {
        return m_bHasManifest;
    }

    void StreamingSystem::SetFocus(const StreamingFocus& focus)
    {
        m_Focus = focus;
    }

    void StreamingSystem::FinishChange(CellEntry& entry, EStreamingCellState from)
    {
        const auto to = entry.Cell.GetState();
        if (m_bRecordTransitions && from != to)
        {
            m_Transitions.push_back({m_Frame, entry.Cell.GetId(), from, to});
        }
        if (entry.Cell.NeedsDiscardPrepared())
        {
            for (uint32_t i = 0; i < entry.Handlers.size(); ++i)
            {
                entry.Handlers[i]->DiscardPrepared(MakeContext(entry.Cell, i));
            }
            entry.Cell.ClearDiscardPrepared();
        }
    }

    void StreamingSystem::RequestUnload(CellEntry& entry)
    {
        const auto from = entry.Cell.GetState();
        if (HasResidentBytes(from))
        {
            entry.LastKnownBytes = CountCellResidentBytes(entry);
        }
        entry.Cell.RequestUnload();
        FinishChange(entry, from);
    }

    void StreamingSystem::SetDormant(CellEntry& entry, bool bDormant)
    {
        for (uint32_t i = 0; i < entry.Handlers.size(); ++i)
        {
            entry.Handlers[i]->SetDormant(MakeContext(entry.Cell, i), bDormant);
        }
    }

    void StreamingSystem::CollectPrepares(double nowMs, const StreamingScopeId* pScopeId)
    {
        for (auto& entry : m_Cells)
        {
            if ((pScopeId == nullptr || entry.ScopeId == *pScopeId) &&
                entry.Cell.GetState() == EStreamingCellState::Preparing &&
                Container::IsValid(entry.Completion) &&
                entry.Completion->Done.Load(std::memory_order_acquire))
            {
                const auto from = entry.Cell.GetState();
                entry.Cell.OnPrepareFinished(entry.Completion->Result, nowMs);
                entry.Completion.reset();
                FinishChange(entry, from);
            }
        }
    }

    void StreamingSystem::CancelAndWaitPrepares(const StreamingScopeId* pScopeId)
    {
        bool bHasPrepares = false;
        for (auto& entry : m_Cells)
        {
            if ((pScopeId == nullptr || entry.ScopeId == *pScopeId) &&
                entry.Cell.GetState() == EStreamingCellState::Preparing)
            {
                bHasPrepares = true;
                RequestUnload(entry);
            }
        }
        // 対象の全セルへ取り消しを通知してから、完了印を待つ。
        for (const auto& entry : m_Cells)
        {
            if ((pScopeId == nullptr || entry.ScopeId == *pScopeId) &&
                Container::IsValid(entry.Completion))
            {
                while (!entry.Completion->Done.Load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
            }
        }
        if (bHasPrepares)
        {
            CollectPrepares(m_Clock->NowMs(), pScopeId);
        }
    }

    uint32_t StreamingSystem::CountResident(bool bLiveOnly) const
    {
        uint32_t count = 0;
        for (const auto& entry : m_Cells)
        {
            const auto state = entry.Cell.GetState();
            if (IsResident(state) && (!bLiveOnly || state != EStreamingCellState::Unloading))
            {
                ++count;
            }
        }
        return count;
    }

    uint32_t StreamingSystem::CountInFlight() const
    {
        uint32_t count = 0;
        for (const auto& entry : m_Cells)
        {
            // 同期実行器も当フレームには回収せず、次の完了回収まで投入枠を使う。
            if (Container::IsValid(entry.Completion))
            {
                ++count;
            }
        }
        return count;
    }

    StreamingPayloadBytes StreamingSystem::CountCellResidentBytes(const CellEntry& entry) const
    {
        StreamingPayloadBytes bytes;
        for (uint32_t i = 0; i < entry.Handlers.size(); ++i)
        {
            const auto payloadBytes = entry.Handlers[i]->GetResidentBytes(MakeContext(entry.Cell, i));
            bytes.Cpu += payloadBytes.Cpu;
            bytes.Gpu += payloadBytes.Gpu;
        }
        return bytes;
    }

    StreamingPayloadBytes StreamingSystem::CountResidentBytes()
    {
        StreamingPayloadBytes bytes;
        for (auto& entry : m_Cells)
        {
            if (HasResidentBytes(entry.Cell.GetState()))
            {
                entry.LastKnownBytes = CountCellResidentBytes(entry);
                bytes.Cpu += entry.LastKnownBytes.Cpu;
                bytes.Gpu += entry.LastKnownBytes.Gpu;
            }
        }
        return bytes;
    }

    bool StreamingSystem::IsOverByteBudget(StreamingPayloadBytes additionalBytes)
    {
        const auto bytes = CountResidentBytes();
        const auto& budget = m_Settings.Budget;
        return bytes.Cpu > budget.MaxCpuBytes || bytes.Gpu > budget.MaxGpuBytes ||
            additionalBytes.Cpu > budget.MaxCpuBytes - bytes.Cpu ||
            additionalBytes.Gpu > budget.MaxGpuBytes - bytes.Gpu;
    }

    StreamingSystem::CellEntry* StreamingSystem::FindWorstLive(bool bWithResidentBytes)
    {
        CellEntry* pWorst = nullptr;
        for (auto& entry : m_Cells)
        {
            const auto state = entry.Cell.GetState();
            if (!IsResident(state) || state == EStreamingCellState::Unloading || entry.bEvicted ||
                (bWithResidentBytes && !HasResidentBytes(state)))
            {
                continue;
            }
            // Preparing は取り消し完了まで状態が変わらないため、重ねて追い出さない。
            if (state == EStreamingCellState::Preparing && entry.Cell.GetCancelToken()->IsCancelled())
            {
                continue;
            }
            if (pWorst == nullptr || entry.Priority > pWorst->Priority ||
                (entry.Priority == pWorst->Priority && entry.Cell.GetId() > pWorst->Cell.GetId()))
            {
                pWorst = &entry;
            }
        }
        return pWorst;
    }

    void StreamingSystem::SortCells(Container::VariableArray<CellEntry*>& cells, bool bWorstFirst) const
    {
        std::sort(cells.begin(), cells.end(), [bWorstFirst](const CellEntry* pLeft, const CellEntry* pRight)
        {
            if (pLeft->Priority == pRight->Priority)
            {
                return bWorstFirst ? pLeft->Cell.GetId() > pRight->Cell.GetId() :
                    pLeft->Cell.GetId() < pRight->Cell.GetId();
            }
            return bWorstFirst ? pLeft->Priority > pRight->Priority : pLeft->Priority < pRight->Priority;
        });
    }

    void StreamingSystem::UnloadOneStep(CellEntry& entry, double nowMs)
    {
        const auto from = entry.Cell.GetState();
        const uint32_t index = entry.Cell.GetCurrentPayloadIndex();
        const auto result = entry.Handlers[index]->UnloadStep(MakeContext(entry.Cell, index), entry.Cell.GetCurrentCursor());
        entry.Cell.OnUnloadStep(result, nowMs);
        FinishChange(entry, from);
    }

    void StreamingSystem::Update()
    {
        ++m_Frame;
        m_LastFrameStats = {};
        m_LastFrameStats.Frame = m_Frame;
        const double now = m_Clock->NowMs();
        CollectPrepares(now);

        Container::VariableArray<CellEntry*> candidates;
        for (auto& entry : m_Cells)
        {
            entry.bEvicted = false;
            const auto& bounds = entry.Cell.GetDesc().Bounds;
            const float distance = ComputeHorizontalDistance(bounds, m_Focus.Position);
            entry.Priority = ComputePriority(bounds, m_Focus, m_Settings.Ring);
            const float loadRadius = ComputeLoadRadius(m_Manifest.GetCellSize(), m_Focus, m_Settings.Ring);
            const auto decision = DecideRing(entry.Cell.GetState(), distance, loadRadius, m_Settings.Ring);
            switch (decision)
            {
            case EStreamingRingDecision::Unload:
                RequestUnload(entry);
                break;
            case EStreamingRingDecision::MakeDormant:
            case EStreamingRingDecision::MakeActive:
            {
                const bool bDormant = decision == EStreamingRingDecision::MakeDormant;
                const auto from = entry.Cell.GetState();
                entry.Cell.SetWantDormant(bDormant);
                FinishChange(entry, from);
                SetDormant(entry, bDormant);
                break;
            }
            case EStreamingRingDecision::Load:
                if (entry.Cell.CanBeginPrepare(now))
                {
                    candidates.push_back(&entry);
                }
                break;
            case EStreamingRingDecision::None:
                break;
            }
            // ReadyToCommit にも渡し、同フレームにコミットが終わる場合の距離を反映する。
            if (entry.Cell.GetState() == EStreamingCellState::Committing ||
                entry.Cell.GetState() == EStreamingCellState::ReadyToCommit)
            {
                const auto from = entry.Cell.GetState();
                entry.Cell.SetWantDormant(ShouldEnterDormant(distance, m_Settings.Ring));
                FinishChange(entry, from);
            }
        }

        uint32_t liveCount = CountResident(true);
        while (liveCount > m_Settings.Budget.MaxResidentCells)
        {
            auto* pWorst = FindWorstLive();
            if (pWorst == nullptr)
            {
                break;
            }
            RequestUnload(*pWorst);
            pWorst->bEvicted = true;
            ++m_LastFrameStats.Evictions;
            // Preparing の取り消しは非同期なので、解放を依頼した分をここでは差し引く。
            --liveCount;
        }
        if (IsOverByteBudget())
        {
            auto* pWorst = FindWorstLive(true);
            if (pWorst != nullptr)
            {
                RequestUnload(*pWorst);
                pWorst->bEvicted = true;
                ++m_LastFrameStats.Evictions;
            }
        }

        SortCells(candidates, false);
        for (auto* pEntry : candidates)
        {
            if (CountInFlight() >= m_Settings.MaxInFlightPrepares)
            {
                break;
            }
            if (pEntry->bEvicted || IsOverByteBudget())
            {
                continue;
            }
            if (IsOverByteBudget(pEntry->LastKnownBytes))
            {
                auto* pWorst = FindWorstLive(true);
                if (pWorst != nullptr && pEntry->Priority < pWorst->Priority)
                {
                    RequestUnload(*pWorst);
                    pWorst->bEvicted = true;
                    ++m_LastFrameStats.Evictions;
                }
                continue;
            }
            if (CountResident(false) >= m_Settings.Budget.MaxResidentCells)
            {
                auto* pWorst = FindWorstLive();
                if (CountResident(true) >= m_Settings.Budget.MaxResidentCells && pWorst != nullptr &&
                    pEntry->Priority < pWorst->Priority)
                {
                    RequestUnload(*pWorst);
                    pWorst->bEvicted = true;
                    ++m_LastFrameStats.Evictions;
                }
                continue;
            }

            const auto from = pEntry->Cell.GetState();
            const bool bStarted = pEntry->Cell.BeginPrepare(now);
            FinishChange(*pEntry, from);
            if (!bStarted)
            {
                continue;
            }
            auto completion = Container::MakeShared<PrepareCompletion>();
            pEntry->Completion = completion;
            const auto token = pEntry->Cell.GetCancelToken();
            const auto* pDesc = &pEntry->Cell.GetDesc();
            auto* const pHandlers = pEntry->Handlers.data();
            const bool bSubmitted = m_Executor->Submit([completion, token, pDesc, pHandlers]()
            {
                EStreamingPrepareResult result = EStreamingPrepareResult::Success;
                try
                {
                    for (uint32_t i = 0; i < pDesc->Payloads.size(); ++i)
                    {
                        if (token->IsCancelled())
                        {
                            result = EStreamingPrepareResult::Cancelled;
                            break;
                        }
                        result = pHandlers[i]->Prepare({pDesc->Id, i, &pDesc->Payloads[i]}, *token);
                        if (result != EStreamingPrepareResult::Success)
                        {
                            break;
                        }
                    }
                    if (token->IsCancelled())
                    {
                        result = EStreamingPrepareResult::Cancelled;
                    }
                }
                catch (...)
                {
                    // 例外でも完了を通知し、Shutdown が永久に待つことを防ぐ。
                    result = EStreamingPrepareResult::Failed;
                }
                completion->Result = result;
                completion->Done.Store(true, std::memory_order_release);
            });
            if (!bSubmitted)
            {
                RequestUnload(*pEntry);
                completion->Result = EStreamingPrepareResult::Cancelled;
                completion->Done.Store(true, std::memory_order_release);
                const auto cancelledFrom = pEntry->Cell.GetState();
                pEntry->Cell.OnPrepareFinished(completion->Result, now);
                pEntry->Completion.reset();
                FinishChange(*pEntry, cancelledFrom);
                break;
            }
        }

        const double stepStart = m_Clock->NowMs();
        const auto canStep = [this, stepStart]()
        {
            return (m_LastFrameStats.CommitSteps == 0 && m_LastFrameStats.UnloadSteps == 0) ||
                (m_Clock->NowMs() - stepStart < m_Settings.Budget.PerFrameCommitMs &&
                 m_LastFrameStats.CommitBytes < m_Settings.Budget.PerFrameCommitBytes);
        };
        Container::VariableArray<CellEntry*> unloading;
        Container::VariableArray<CellEntry*> committing;
        for (auto& entry : m_Cells)
        {
            if (entry.Cell.GetState() == EStreamingCellState::Unloading)
            {
                unloading.push_back(&entry);
            }
            else if (entry.Cell.GetState() == EStreamingCellState::ReadyToCommit ||
                     entry.Cell.GetState() == EStreamingCellState::Committing)
            {
                committing.push_back(&entry);
            }
        }
        SortCells(unloading, true);
        SortCells(committing, false);
        for (auto* pEntry : unloading)
        {
            if (!canStep())
            {
                break;
            }
            UnloadOneStep(*pEntry, now);
            ++m_LastFrameStats.UnloadSteps;
        }
        for (auto* pEntry : committing)
        {
            if (!canStep())
            {
                break;
            }
            if (pEntry->Cell.GetState() == EStreamingCellState::ReadyToCommit)
            {
                if (IsOverByteBudget())
                {
                    continue;
                }
                const auto from = pEntry->Cell.GetState();
                pEntry->Cell.BeginCommit();
                FinishChange(*pEntry, from);
                if (pEntry->Cell.GetState() == EStreamingCellState::Dormant)
                {
                    SetDormant(*pEntry, true);
                }
            }
            while (pEntry->Cell.GetState() == EStreamingCellState::Committing && canStep())
            {
                const uint32_t index = pEntry->Cell.GetCurrentPayloadIndex();
                uint64_t bytes = 0;
                const auto result = pEntry->Handlers[index]->CommitStep(
                    MakeContext(pEntry->Cell, index), pEntry->Cell.GetCurrentCursor(), bytes);
                m_LastFrameStats.CommitBytes += bytes;
                ++m_LastFrameStats.CommitSteps;
                const auto from = pEntry->Cell.GetState();
                pEntry->Cell.OnCommitStep(result);
                FinishChange(*pEntry, from);
                if (pEntry->Cell.GetState() == EStreamingCellState::Dormant)
                {
                    SetDormant(*pEntry, true);
                }
            }
        }
        m_LastFrameStats.StepMs = m_Clock->NowMs() - stepStart;

        for (auto& entry : m_Cells)
        {
            FinishChange(entry, entry.Cell.GetState());
        }
        m_LastFrameStats.ResidentCells = CountResident(false);
        m_LastFrameStats.InFlightPrepares = CountInFlight();
        const auto bytes = CountResidentBytes();
        m_LastFrameStats.ResidentCpuBytes = bytes.Cpu;
        m_LastFrameStats.ResidentGpuBytes = bytes.Gpu;
    }

    void StreamingSystem::StopAndJoin(StreamingScopeId scopeId)
    {
        const auto belongsToScope = [scopeId](const CellEntry& entry)
        {
            return entry.ScopeId == scopeId;
        };
        if (!m_bHasManifest || std::find_if(m_Cells.begin(), m_Cells.end(), belongsToScope) == m_Cells.end())
        {
            return;
        }

        CancelAndWaitPrepares(&scopeId);
        for (auto& entry : m_Cells)
        {
            if (belongsToScope(entry))
            {
                RequestUnload(entry);
                while (entry.Cell.GetState() == EStreamingCellState::Unloading)
                {
                    UnloadOneStep(entry, m_Clock->NowMs());
                }
            }
        }
        m_Cells.erase(std::remove_if(m_Cells.begin(), m_Cells.end(), belongsToScope), m_Cells.end());
        if (m_Cells.empty())
        {
            m_Manifest = CellManifest{};
            m_bHasManifest = false;
        }
    }

    void StreamingSystem::Shutdown()
    {
        CancelAndWaitPrepares();
        for (auto& entry : m_Cells)
        {
            RequestUnload(entry);
            while (entry.Cell.GetState() == EStreamingCellState::Unloading)
            {
                UnloadOneStep(entry, m_Clock->NowMs());
            }
        }
        m_Cells.clear();
        m_Manifest = CellManifest{};
        m_bHasManifest = false;
    }

    EStreamingCellState StreamingSystem::GetCellState(StreamingCellId id) const
    {
        for (const auto& entry : m_Cells)
        {
            if (entry.Cell.GetId() == id)
            {
                return entry.Cell.GetState();
            }
        }
        return EStreamingCellState::Unloaded;
    }

    const StreamingFrameStats& StreamingSystem::GetLastFrameStats() const
    {
        return m_LastFrameStats;
    }

    void StreamingSystem::SetRecordTransitions(bool bRecord)
    {
        m_bRecordTransitions = bRecord;
    }

    Container::Span<const StreamingTransition> StreamingSystem::GetTransitions() const
    {
        return {m_Transitions.data(), m_Transitions.size()};
    }

    void StreamingSystem::ClearTransitions()
    {
        m_Transitions.clear();
    }
} // namespace NorvesLib::Core::Streaming
