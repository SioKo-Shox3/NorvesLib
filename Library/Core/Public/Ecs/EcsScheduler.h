#pragma once

/**
 * @file EcsScheduler.h
 * @brief ECS のシステムの宣言と、段・波・同期点のスケジューラ
 *
 * - 段（Stage）は AddStage した順に走る。段の中のシステムは SystemDesc の Reads / Writes / After で順序と衝突を決め、
 *   衝突しないシステムを「波」にまとめる。波は前の波がすべて終わってから始まる。
 *   - 衝突: 同じ列に対する W-W・R-W。登録順に並べるが、直接の After があればそれに従う。
 *     GameThreadOnly のシステムはすべてと衝突し、単独の波になる。
 *   - RenderThreadOnly は登録を拒む。After が循環するときも登録（AddAfter）を拒む。
 * - チャンクのシステム（WorkerThread / AnyThread）は、波の中の全システムの対象チャンクを 1 つの
 *   JobSystem::ParallelFor の仕事の列にまとめて走る（システムの中で ParallelFor は呼ばない）。
 *   仕事は（システムの登録順 → チャンクの順）で番号が付き、変更の束とコマンドバッファも仕事ごとに持つ。
 * - GameThreadOnly のシステムは呼び手のスレッドで、他のシステムと重ならずに走る。Object に触ってよい。
 * - 段の中の構造変更は EcsCommandBuffer に積み、段の終わりの同期点（Ecs.Sync.<段>）で
 *   （システムの登録順 → チャンクの順）に再生する。再生の後に SetSyncHook の関数を呼ぶ。
 * - システム 1 回ぶんごとに World の版を 1 つ進め（AdvanceVersion）、仕事はその版で MarkWritten する。
 *   波が終わるごとに変更の束を仕事の順に CommitChanges する。
 * - 順序付きの GameThread 系（RegisterOrderedSystem）は、Tick を持っていた型の呼び先を
 *   （群, 優先度, 順番）の順 = TickOrderLess（Component/TickGroup.h）の比較で呼ぶ。順の一覧を作り直すのは、
 *   追加・削除・付け替えがあったときの次の収集の先頭だけ。
 *   bCallerDriven の系は段の中では呼ばず、同じ段の GameThread のシステム（呼び手）が GetOrderedRun で
 *   順の一覧を読んで呼ぶ（群の切れ目で変換を公開するなど、呼び手が間に処理を挟むため）。
 * - 収集（BeginCollection / EndCollection）は通常の Tick や単独の固定ステップの単位。入れ子では番号を共有する。
 *   参加の番号 = 「次の収集」の番号（GetNextCollectionSerial）。収集の中で足した呼び先は、次の収集まで呼ばれない。
 * - 統計が有効な構成では、段・系・同期点を Ecs.Stage.<段>・Ecs.Sys.<系>・Ecs.Sync.<段> の区間としてトレースへ記録する。
 *   チャンクの仕事は、走ったスレッドの番号で系の名前の区間に入る。名前は登録時に 1 度だけ作る。
 *
 * GameThread だけが触る。チャンクの仕事の中から呼んでよいのは、EcsChunkJob に渡された口だけ。
 * RunStage の最中の登録・削除（システム・段の追加）は拒む。呼び先（順序付きの項目）の追加・削除は許す。
 */

#include "Component/TickGroup.h"
#include "Container/Containers.h"
#include "Container/Span.h"
#include "Ecs/EcsCommandBuffer.h"
#include "Ecs/EcsWorld.h"
#include "Object/RuntimeSchema.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Ecs
{
    using EcsStageId = uint32_t;
    inline constexpr EcsStageId kInvalidEcsStage = 0xFFFFFFFFu;

    using EcsSystemId = uint32_t;
    inline constexpr EcsSystemId kInvalidEcsSystem = 0xFFFFFFFFu;

    /// 単独で走るシステム（GameThreadOnly・順序付きの呼び先）へ渡す文脈
    struct EcsSystemContext
    {
        EcsWorld *World = nullptr;
        /// 構造変更はここへ積む。段の同期点まで World には見えない
        EcsCommandBuffer *Commands = nullptr;
        /// このシステム 1 回ぶんの版（AdvanceVersion の結果）
        uint64_t Version = 0;
        /// 型の channel の delta（順序付きの呼び先は項目の channel、それ以外はシステムの channel）
        float DeltaTime = 0.0f;
        uint64_t CollectionSerial = 0;
    };

    /// チャンク 1 つの仕事へ渡す文脈。別のスレッドで同時に走るので、ここに書かれた口以外には触れない。
    struct EcsChunkJob
    {
        EcsWorld *World = nullptr;
        EcsChunk *Chunk = nullptr;
        EcsArchetypeId Archetype = kInvalidEcsArchetype;
        /// システムの対象チャンクの並びの中の番号
        uint32_t ChunkIndex = 0;
        uint64_t Version = 0;
        float DeltaTime = 0.0f;
        /// MarkWritten に渡す束（この仕事だけが使う）
        EcsChangeBundle *Changes = nullptr;
        /// 構造変更の積み先（この仕事だけが使う）
        EcsCommandBuffer *Commands = nullptr;
        /// いまの収集の番号。参加の番号がこれより大きい行（同じ収集の中で作られた行）は処理しない
        uint64_t CollectionSerial = 0;

        /// チャンクの全行が参加するか（true なら IsRowJoined を呼ばずに全行を処理してよい）
        [[nodiscard]] bool AllRowsJoined() const
        {
            return Chunk->AllRowsJoined(CollectionSerial);
        }

        /// 行 row が今回の収集に参加するか。false の行は飛ばす。
        [[nodiscard]] bool IsRowJoined(uint32_t row) const
        {
            return Chunk->IsRowJoined(row, CollectionSerial);
        }
    };

    using EcsSystemFn = void (*)(void *user, const EcsSystemContext &context);
    using EcsChunkFn = void (*)(void *user, const EcsChunkJob &job);
    /// 段の同期点で、コマンドの再生の後に呼ぶ関数（GameThread）
    using EcsSyncFn = void (*)(void *user, EcsWorld &world);

    /// システムの宣言
    struct SystemDesc
    {
        /// 登録のときに複製する。空・重複は拒む
        const char *Name = nullptr;
        EcsStageId Stage = kInvalidEcsStage;
        /// 読む列・書く列（型の番号）。衝突の判定に使う
        Container::VariableArray<TypeId> Reads;
        Container::VariableArray<TypeId> Writes;
        /// GameThreadOnly は Run、WorkerThread / AnyThread は Query と RunChunk を使う。RenderThreadOnly は拒む
        ThreadPolicy Thread = ThreadPolicy::GameThreadOnly;
        /// このシステムより前に走るシステム（登録済みで、同じ段か前の段）
        Container::VariableArray<EcsSystemId> After;
        /// 型の channel の番号。RunStage に渡す delta の並びの添字
        uint8_t TimeChannel = 0;
        /// チャンクのシステムの対象
        EcsQuery Query;
        EcsSystemFn Run = nullptr;
        EcsChunkFn RunChunk = nullptr;
        void *User = nullptr;
    };

    /// 順序付きの GameThread 系の宣言（Reads / Writes は持たず、すべてと衝突する単独の波になる）
    struct OrderedSystemDesc
    {
        const char *Name = nullptr;
        EcsStageId Stage = kInvalidEcsStage;
        Container::VariableArray<EcsSystemId> After;
        /// この系が呼ぶ群。群が含まれない項目は呼ばない
        Component::TickGroupMask Groups = Component::AllTickGroups;
        /// true: 段の中では呼ばない。呼び手が GetOrderedRunCount / GetOrderedRun で順に読んで呼ぶ
        bool bCallerDriven = false;
    };

    /// 順序付きの系の呼び先 1 つ
    struct OrderedEntryDesc
    {
        Component::ETickGroup Group = Component::ETickGroup::Default;
        int16_t Priority = 0;
        /// 同じ群・優先度の中の順（Entity の深さ優先の番号など）
        size_t Ordinal = 0;
        EcsSystemFn Run = nullptr;
        void *User = nullptr;
        uint8_t TimeChannel = 0;
    };

    /// 順の一覧の 1 項目（呼び手が合流させて呼ぶときの読み口）
    struct OrderedRunView
    {
        Component::ETickGroup Group = Component::ETickGroup::Default;
        int16_t Priority = 0;
        size_t Ordinal = 0;
        EcsSystemFn Run = nullptr;
        void *User = nullptr;
        uint8_t TimeChannel = 0;
    };

    struct EcsOrderedHandle
    {
        EcsSystemId System = kInvalidEcsSystem;
        uint32_t Index = 0;
        uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return System != kInvalidEcsSystem;
        }
    };

    /// 実行の集計（テストと計測の確認用）
    struct SchedulerStats
    {
        /// ParallelFor を呼んだ回数（チャンクの波ごとに 1 回）
        uint32_t ParallelForCalls = 0;
        /// ParallelFor に渡した仕事の数の合計
        uint32_t ChunkJobs = 0;
        /// 直近の波の仕事の数
        uint32_t LastWaveJobs = 0;
        /// 順序付きの系の順の一覧を作り直した回数
        uint32_t OrderRebuilds = 0;
    };

    class SystemScheduler
    {
    public:
        explicit SystemScheduler(EcsWorld &world);
        ~SystemScheduler();

        SystemScheduler(const SystemScheduler &) = delete;
        SystemScheduler &operator=(const SystemScheduler &) = delete;

        // ---- 段 ----

        /// 段を足す（走る順 = 足した順）。名前が空・重複なら kInvalidEcsStage
        EcsStageId AddStage(const char *name);
        [[nodiscard]] uint32_t GetStageCount() const;
        [[nodiscard]] EcsStageId FindStage(const char *name) const;

        /// 段の同期点で、コマンドの再生の後に呼ぶ関数を設定する（置き換え。null で外す）
        bool SetSyncHook(EcsStageId stage, EcsSyncFn fn, void *user);

        /// RunStage に delta が渡されなかったとき、この段の全 channel に使う delta（0 で外す）。
        /// 呼び手が delta を渡さない段（固定ステップの段）のシステムへ、1 歩の時間を届けるのに使う
        bool SetStageDefaultDelta(EcsStageId stage, float delta);

        // ---- システム ----

        /**
         * @brief システムを登録する
         * @return システムの番号。RenderThreadOnly・名前が空か重複・段が無効・関数が足りない・
         *         After が未登録か後ろの段・RunStage の最中は kInvalidEcsSystem
         */
        EcsSystemId RegisterSystem(const SystemDesc &desc);

        /// 順序付きの GameThread 系を登録する（失敗の条件は RegisterSystem と同じ）
        EcsSystemId RegisterOrderedSystem(const OrderedSystemDesc &desc);

        /**
         * @brief system が after より後に走るようにする
         * @return 循環する（自分自身を含む）・after が後ろの段・番号が無効・RunStage の最中は false で何も変えない
         */
        bool AddAfter(EcsSystemId system, EcsSystemId after);

        [[nodiscard]] uint32_t GetSystemCount() const;
        [[nodiscard]] EcsSystemId FindSystem(const char *name) const;

        /// 段の波の数（システムが無ければ 0）
        [[nodiscard]] uint32_t GetWaveCount(EcsStageId stage);
        /// システムが入る波の番号（段の中で 0 から）。無効なら -1
        [[nodiscard]] int32_t GetSystemWave(EcsSystemId system);

        /// 区間の名前（登録のとき 1 度だけ作った文字列。フレームごとに変わらない）
        [[nodiscard]] const char *GetStageScopeName(EcsStageId stage) const;
        [[nodiscard]] const char *GetSyncScopeName(EcsStageId stage) const;
        [[nodiscard]] const char *GetSystemScopeName(EcsSystemId system) const;

        // ---- 順序付きの系の呼び先 ----

        /// 呼び先を足す。参加の番号は「次の収集」の番号で、それまでは呼ばれない。無効なら IsValid() が false の値
        EcsOrderedHandle AddOrderedEntry(EcsSystemId system, const OrderedEntryDesc &entry);

        /// 呼び先の参加の番号（足した時点の「次の収集」の番号。この収集から呼ばれる）。無効な持ち手は 0
        [[nodiscard]] uint64_t GetOrderedEntryJoinSerial(EcsOrderedHandle handle);

        /// 呼び先を外す。すぐに呼ばれなくなる（走っている収集の中でも）
        bool RemoveOrderedEntry(EcsOrderedHandle handle);

        /// 群・優先度を付け替える。順への反映は次の収集から
        bool SetOrderedEntryKey(EcsOrderedHandle handle, Component::ETickGroup group, int16_t priority);
        /// 群・優先度・同じ群と優先度の中の順を付け替える。順への反映は次の収集から
        bool SetOrderedEntryKey(EcsOrderedHandle handle, Component::ETickGroup group, int16_t priority, size_t ordinal);

        /// この収集の順の一覧の長さ（bCallerDriven の系の呼び手が読む。収集の先頭でだけ変わる）
        [[nodiscard]] uint32_t GetOrderedRunCount(EcsSystemId system) const;
        /// 順の一覧の index 番目を読む。範囲外・外された呼び先は false（呼ぶ直前に毎回読み直す）
        [[nodiscard]] bool GetOrderedRun(EcsSystemId system, uint32_t index, OrderedRunView &out) const;

        // ---- 収集 ----

        /**
         * @brief 収集（通常の Tick・単独の固定ステップなど）を始める。最も外側の呼び出しで番号が進み、入れ子は番号を共有する
         * @return 収集の番号（1 から）
         */
        uint64_t BeginCollection();
        void EndCollection();
        [[nodiscard]] bool IsCollecting() const
        {
            return m_CollectionDepth > 0;
        }

        /// 走っている収集の番号（収集の外では最後の収集の番号。まだ無ければ 0）
        [[nodiscard]] uint64_t GetCollectionSerial() const
        {
            return m_CollectionSerial;
        }

        /// 参加の番号 = 「次の収集」の番号。いま足した呼び先はこの収集から呼ばれる
        [[nodiscard]] uint64_t GetNextCollectionSerial() const
        {
            return m_CollectionSerial + 1;
        }

        // ---- 実行 ----

        /**
         * @brief 段を 1 つ走らせる（波 → 同期点）。収集の外から呼ぶと、1 つの収集として走る
         * @param channelDeltas 型の channel ごとの delta（範囲外の channel は 0）
         * @return 段が無効・走っている最中の再入は false。システムが例外を投げたら、積んだコマンドを捨てて投げ直す
         */
        bool RunStage(EcsStageId stage, Container::Span<const float> channelDeltas = {});

        [[nodiscard]] bool IsRunning() const
        {
            return m_bRunning;
        }

        /// 直近の同期点でのコマンドの再生の結果
        [[nodiscard]] const EcsReplayResult &GetLastReplayResult() const
        {
            return m_LastReplay;
        }

        [[nodiscard]] const SchedulerStats &GetStats() const
        {
            return m_Stats;
        }

        void ResetStats();

    private:
        /// 仕事ごとの変更の束とコマンドバッファ（プールして再利用する）
        struct Slot
        {
            EcsChangeBundle Changes;
            EcsCommandBuffer Commands;

            explicit Slot(uint32_t id)
                : Commands(id)
            {
            }
        };

        struct OrderedEntry
        {
            EcsSystemFn Run = nullptr;
            void *User = nullptr;
            uint8_t TimeChannel = 0;
            Component::ETickGroup Group = Component::ETickGroup::Default;
            int16_t Priority = 0;
            size_t Ordinal = 0;
            Component::ETickGroup NextGroup = Component::ETickGroup::Default;
            int16_t NextPriority = 0;
            size_t NextOrdinal = 0;
            bool bHasNextKey = false;
            bool bAlive = false;
            /// 空き一覧に入れた
            bool bFree = false;
            /// この収集の番号以上で呼ばれる
            uint64_t JoinSerial = 0;
            /// 足した順（同じ比較の最後の決め手）
            uint64_t Sequence = 0;
            uint32_t Generation = 1;
        };

        struct System
        {
            Container::String Name;
            Container::String ScopeName;
            EcsStageId Stage = kInvalidEcsStage;
            Container::VariableArray<TypeId> Reads;
            Container::VariableArray<TypeId> Writes;
            ThreadPolicy Thread = ThreadPolicy::GameThreadOnly;
            Container::VariableArray<EcsSystemId> After;
            uint8_t TimeChannel = 0;
            EcsQuery Query;
            EcsSystemFn Run = nullptr;
            EcsChunkFn RunChunk = nullptr;
            void *User = nullptr;
            int32_t Wave = -1;

            bool bOrdered = false;
            bool bCallerDriven = false;
            Component::TickGroupMask Groups = Component::AllTickGroups;
            Container::VariableArray<OrderedEntry> Entries;
            Container::VariableArray<uint32_t> FreeEntries;
            /// この収集で呼ぶ項目の添字（収集の先頭で作り直す）
            Container::VariableArray<uint32_t> Order;
            bool bOrderDirty = false;
            uint64_t NextSequence = 0;

            /// 今回の実行
            uint32_t FirstSlot = 0;
            uint32_t SlotCount = 0;
            uint32_t FirstChunk = 0;
            uint32_t ChunkCount = 0;
            uint64_t RunVersion = 0;
            float RunDelta = 0.0f;
        };

        struct Stage
        {
            Container::String Name;
            Container::String StageScopeName;
            Container::String SyncScopeName;
            /// 登録順
            Container::VariableArray<EcsSystemId> Systems;
            Container::VariableArray<Container::VariableArray<EcsSystemId>> Waves;
            bool bPlanDirty = false;
            EcsSyncFn SyncFn = nullptr;
            void *SyncUser = nullptr;
            float DefaultDelta = 0.0f;
        };

        /// 段の波を求める。循環するなら false（outWaves は未定義）
        bool ComputeWaves(const Stage &stage, Container::VariableArray<int32_t> &outWaves, int32_t &outWaveCount) const;
        bool ConflictsWith(const System &a, const System &b) const;
        void EnsurePlan(EcsStageId stage);
        void RebuildDirtyOrders();
        void EnsureSlots(uint32_t count);
        void RunGameThreadSystem(System &system, Container::Span<const float> channelDeltas);
        void RunOrderedEntries(System &system, EcsSystemContext &context, Container::Span<const float> channelDeltas);
        void RunChunkWave(const Container::VariableArray<EcsSystemId> &wave, Container::Span<const float> channelDeltas);
        void RunChunkJob(uint32_t jobIndex);
        void SyncPoint(Stage &stage);
        void DiscardStageBuffers(const Stage &stage);
        [[nodiscard]] OrderedEntry *ResolveEntry(EcsOrderedHandle handle);

        struct ChunkJobRecord
        {
            EcsSystemId System = kInvalidEcsSystem;
            EcsChunk *Chunk = nullptr;
            uint32_t ChunkIndex = 0;
            uint32_t Slot = 0;
        };

        EcsWorld &m_World;
        Container::VariableArray<Stage> m_Stages;
        Container::VariableArray<System> m_Systems;
        Container::VariableArray<Container::TUniquePtr<Slot>> m_Slots;
        uint32_t m_SlotCursor = 0;
        /// 波ごとの作業用（再利用する）
        Container::VariableArray<EcsChunk *> m_WaveChunks;
        Container::VariableArray<ChunkJobRecord> m_Jobs;
        Container::VariableArray<EcsChangeBundle *> m_WaveBundles;
        Container::VariableArray<EcsCommandBuffer *> m_ReplayList;

        uint64_t m_CollectionSerial = 0;
        uint32_t m_CollectionDepth = 0;
        bool m_bRunning = false;
        /// 走っている段の、channel の delta が無いときの値（RunStage の間だけ有効）
        float m_RunDefaultDelta = 0.0f;
        EcsReplayResult m_LastReplay;
        SchedulerStats m_Stats;
    };

    /// 収集の RAII。入れ子では番号を共有する
    class EcsCollectionScope
    {
    public:
        explicit EcsCollectionScope(SystemScheduler &scheduler)
            : m_Scheduler(scheduler)
            , m_Serial(scheduler.BeginCollection())
        {
        }

        ~EcsCollectionScope()
        {
            m_Scheduler.EndCollection();
        }

        EcsCollectionScope(const EcsCollectionScope &) = delete;
        EcsCollectionScope &operator=(const EcsCollectionScope &) = delete;

        [[nodiscard]] uint64_t GetSerial() const
        {
            return m_Serial;
        }

    private:
        SystemScheduler &m_Scheduler;
        uint64_t m_Serial;
    };

}
