// セルのストリーミング全体を扱う。
#pragma once

#include "Streaming/CellManifest.h"
#include "Streaming/IStreamingEnvironment.h"
#include "Streaming/IStreamingPayloadHandler.h"
#include "Streaming/StreamingBudget.h"
#include "Streaming/StreamingCell.h"
#include "Streaming/StreamingPriority.h"

namespace NorvesLib::Core::Streaming
{
    struct StreamingSystemSettings
    {
        StreamingRingSettings Ring;
        StreamingBudget Budget;
        StreamingRetryPolicy Retry;
        uint32_t MaxInFlightPrepares = 4;
    };

    struct StreamingTransition
    {
        uint64_t Frame;
        StreamingCellId Cell;
        EStreamingCellState From;
        EStreamingCellState To;
    };

    struct StreamingFrameStats
    {
        uint64_t Frame = 0;
        uint32_t ResidentCells = 0; // Unloaded・Failed 以外。Unloading も数える。
        uint32_t InFlightPrepares = 0;
        uint32_t CommitSteps = 0;
        uint32_t UnloadSteps = 0;
        double StepMs = 0.0; // 注入した時計で測るコミットとアンロードの歩の時間。
        uint64_t CommitBytes = 0;
        uint64_t ResidentCpuBytes = 0;
        uint64_t ResidentGpuBytes = 0;
        uint32_t Evictions = 0;
    };

    // 公開操作はすべて GameThread で直列に呼ぶ。ハンドラと注入環境は非所有。
    // バイト上限は申告済みの最後の常駐量で読み込み前に判定する。
    // 初回は量が分からないため、読み込み後に上限超過分を追い出す。
    class StreamingSystem
    {
    public:
        StreamingSystem();
        ~StreamingSystem();
        StreamingSystem(const StreamingSystem&) = delete;
        StreamingSystem& operator=(const StreamingSystem&) = delete;

        // Prepare を取り消して完了を待ち、既存セルを保持して環境を替える。nullptr は既定に戻す。
        // 注入環境は差し替えまたは本体破棄まで生存させる。時計の時間軸は揃えておくこと。
        // 偽の実行器に溜めた仕事は、本関数と Shutdown・再読み込み・破棄の前に全部実行すること。
        void SetEnvironment(IStreamingClock* pClock, IStreamingJobExecutor* pExecutor);
        // 輪が不正なら変更しない。Retry は次のマニフェスト読み込みで作るセルに適用する。
        bool SetSettings(const StreamingSystemSettings& settings);
        const StreamingSystemSettings& GetSettings() const;
        // マニフェストを持つ前だけ登録できる。ハンドラは Shutdown の完了まで生存させる。
        bool RegisterHandler(IStreamingPayloadHandler* pHandler);
        // 解析成功時だけ旧セルを同期解放して置き換える。
        bool LoadManifestFromText(const Container::String& text, CellManifestError* pOutError = nullptr);
        bool HasManifest() const;
        void SetFocus(const StreamingFocus& focus);
        void Update();
        // Prepare の完了を待ち、予算を無視して全セルを解放する。繰り返し呼べる。
        void Shutdown();
        EStreamingCellState GetCellState(StreamingCellId id) const;
        const StreamingFrameStats& GetLastFrameStats() const;
        void SetRecordTransitions(bool bRecord);
        // ビューは遷移の追加・ClearTransitions・本体破棄まで有効。
        Container::Span<const StreamingTransition> GetTransitions() const;
        void ClearTransitions();

    private:
        struct PrepareCompletion
        {
            Thread::Atomic<bool> Done{false};
            EStreamingPrepareResult Result = EStreamingPrepareResult::Cancelled;
        };

        struct HandlerEntry
        {
            Container::String Kind;
            IStreamingPayloadHandler* Handler;
        };

        struct CellEntry
        {
            CellEntry(const StreamingCellDesc* pDesc, StreamingRetryPolicy retry);
            StreamingCell Cell;
            Container::VariableArray<IStreamingPayloadHandler*> Handlers;
            Container::TSharedPtr<PrepareCompletion> Completion;
            StreamingPayloadBytes LastKnownBytes{};
            float Priority = 0.0f;
            bool bEvicted = false;
        };

        void FinishChange(CellEntry& entry, EStreamingCellState from);
        void RequestUnload(CellEntry& entry);
        void SetDormant(CellEntry& entry, bool bDormant);
        void CollectPrepares(double nowMs);
        void CancelAndWaitPrepares();
        uint32_t CountResident(bool bLiveOnly) const;
        uint32_t CountInFlight() const;
        StreamingPayloadBytes CountCellResidentBytes(const CellEntry& entry) const;
        StreamingPayloadBytes CountResidentBytes();
        bool IsOverByteBudget(StreamingPayloadBytes additionalBytes = {});
        CellEntry* FindWorstLive(bool bWithResidentBytes = false);
        void SortCells(Container::VariableArray<CellEntry*>& cells, bool bWorstFirst) const;
        void UnloadOneStep(CellEntry& entry, double nowMs);

        SteadyStreamingClock m_DefaultClock;
        JobSystemStreamingExecutor m_DefaultExecutor;
        IStreamingClock* m_Clock;
        IStreamingJobExecutor* m_Executor;
        StreamingSystemSettings m_Settings;
        StreamingFocus m_Focus;
        CellManifest m_Manifest;
        Container::VariableArray<HandlerEntry> m_Handlers;
        Container::VariableArray<CellEntry> m_Cells;
        Container::VariableArray<StreamingTransition> m_Transitions;
        StreamingFrameStats m_LastFrameStats;
        uint64_t m_Frame = 0;
        bool m_bHasManifest = false;
        bool m_bRecordTransitions = false;
    };
} // namespace NorvesLib::Core::Streaming
