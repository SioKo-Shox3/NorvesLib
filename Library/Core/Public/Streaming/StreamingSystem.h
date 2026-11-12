// セルのストリーミング全体を扱う。
#pragma once

#include "Container/VariableArray.h"
#include "Delegate/Delegate.h"
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

    enum class EStreamingSnapStatus : uint8_t
    {
        Ready,
        Cancelled,
        Failed
    };

    struct StreamingSnapResult
    {
        EStreamingSnapStatus Status = EStreamingSnapStatus::Ready;
        uint32_t Cells = 0;      // 対象のセルの数。
        double ElapsedMs = 0.0;  // 注入した時計で測る、要求から通知までの時間。
    };

    using StreamingSnapCallback = Delegate<void, const StreamingSnapResult&>;

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

    // フレームの時間を直近の窓の中央値と比べ、ratio 倍を超えた回数を数える。GameThread だけで使う。
    // 窓の長さと最少標本数は最低 1 とし、最少標本数が窓の長さを超えるときは窓の長さにする。
    class StreamingHitchCounter
    {
    public:
        explicit StreamingHitchCounter(uint32_t windowFrames = 120, uint32_t minSamples = 30, double hitchRatio = 2.0);
        // 非有限・負の値は無視する。有効な値は追加前の窓で判定してから窓に入れる。
        // 標本が最少数以上あり、中央値の hitchRatio 倍を厳密に超えた場合だけ true。
        bool AddFrame(double frameMs);
        uint32_t GetHitchCount() const;
        // 最後にヒッチと数えたフレームの時間。無ければ 0。
        double GetLastHitchFrameMs() const;
        // 最後の有効な入力で判定に使った中央値。判定しなかったら 0。
        double GetLastMedianMs() const;
        // 窓と回数と記録を空にする。
        void Reset();

    private:
        uint32_t m_WindowFrames;
        uint32_t m_MinSamples;
        double m_HitchRatio;
        Container::VariableArray<double> m_Samples;
        Container::VariableArray<double> m_SortedSamples;
        uint32_t m_NextSample = 0;
        uint32_t m_HitchCount = 0;
        double m_LastHitchFrameMs = 0.0;
        double m_LastMedianMs = 0.0;
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
        // 未登録・nullptr は false。マニフェストを持つ間は先に Shutdown() で全セルを同期解放してから外す。
        bool UnregisterHandler(IStreamingPayloadHandler* pHandler);
        // 解析成功時だけ旧セルを同期解放し、指定スコープのセルで置き換える。
        bool LoadManifestFromText(const Container::String& text, CellManifestError* pOutError = nullptr,
                                  StreamingScopeId scopeId = kDefaultStreamingScopeId);
        bool HasManifest() const;
        void SetFocus(const StreamingFocus& focus);
        void Update();
        // position から水平距離 radius 以内のセルを、輪と予算によらず最優先で読む。
        // 全対象が Active で全ペイロードが IsPayloadReady になった Update の終わりに Ready を一度通知する。
        // 対象が Failed になったら Failed を一度通知する。対象ゼロでも最初の Update で Ready を通知する。
        // 未完了の前の要求には、新しい要求を据えた後に Cancelled を一度通知する。
        // マニフェストなし・非有限の位置・負か非有限の半径・未束縛の通知先は false とし、通知しない。
        // Ready の後も焦点が目的地から radius 以内へ入る最初の Update まで対象を Active に保つ。
        // 通知中に Update・Shutdown・StopAndJoin・LoadManifestFromText を呼ばないこと。
        bool RequestSnapLoad(const Math::Vector3& position, float radius, StreamingSnapCallback onReady);
        // 未完了なら Cancelled を一度通知する。通知済みなら到着待ちの保持を外す。要求なしなら何もしない。
        void CancelSnapLoad();
        // 指定スコープの準備を取り消して待ち、予算を無視してセルの資源を同期解放し、セルを外す。
        // 戻った後は対象セルの準備処理やハンドラ呼び出しは行われない。存在しないスコープは何もしない。
        // 自動実行しない実行器に溜めた対象セルの仕事は、呼ぶ前に実行すること。
        void StopAndJoin(StreamingScopeId scopeId);
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
            CellEntry(const StreamingCellDesc* pDesc, StreamingRetryPolicy retry, StreamingScopeId scopeId);
            StreamingCell Cell;
            StreamingScopeId ScopeId;
            Container::VariableArray<IStreamingPayloadHandler*> Handlers;
            Container::TSharedPtr<PrepareCompletion> Completion;
            StreamingPayloadBytes LastKnownBytes{};
            float Priority = 0.0f;
            bool bEvicted = false;
            bool bSnapTarget = false;
        };

        // 通知先を取り出し、呼び直しに備えて先に要求状態を整える。
        StreamingSnapCallback DetachSnapCallback(EStreamingSnapStatus status, StreamingSnapResult& result);
        void FinishChange(CellEntry& entry, EStreamingCellState from);
        void RequestUnload(CellEntry& entry);
        void SetDormant(CellEntry& entry, bool bDormant);
        void CollectPrepares(double nowMs, const StreamingScopeId* pScopeId = nullptr);
        void CancelAndWaitPrepares(const StreamingScopeId* pScopeId = nullptr);
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
        Math::Vector3 m_SnapPosition;
        float m_SnapRadius = 0.0f;
        StreamingSnapCallback m_SnapCallback;
        double m_SnapRequestMs = 0.0;
        uint32_t m_SnapCells = 0;
        bool m_bSnapPending = false;
        bool m_bSnapHeld = false;
        CellManifest m_Manifest;
        Container::VariableArray<HandlerEntry> m_Handlers;
        Container::VariableArray<CellEntry> m_Cells;
        Container::VariableArray<StreamingTransition> m_Transitions;
        StreamingFrameStats m_LastFrameStats;
        StreamingHitchCounter m_HitchCounter;
        double m_LastUpdateMs = 0.0;
        uint64_t m_Frame = 0;
        bool m_bHasLastUpdateMs = false;
        bool m_bHasManifest = false;
        bool m_bRecordTransitions = false;
    };
} // namespace NorvesLib::Core::Streaming
