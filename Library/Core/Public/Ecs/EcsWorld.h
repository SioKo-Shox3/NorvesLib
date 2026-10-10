#pragma once

/**
 * @file EcsWorld.h
 * @brief ECS の実体の表・アーキタイプ・16 KB のチャンクと、行の作成と削除
 *
 * - アーキタイプは「列の型の番号の昇順の配列」と「タグのビット」で決まる。同じ組は同じ番号を返す。
 * - チャンクは 16 KB 固定。先頭に見出し（EcsChunk）、続けて行ごとの EcsEntity の配列、その後に列ごとの
 *   連続した配列（SoA）を置く。1 アーキタイプのチャンクは先頭から詰めて埋め、最後の 1 つだけが空きを持つ。
 * - 行の削除は、アーキタイプの末尾の行との入れ替えで行う。末尾のチャンクが空になったら解放し、
 *   次の作成で同じブロックを再利用する。入れ替わった実体の表は更新される。
 * - 列の追加・削除は、行を別のアーキタイプ（列の組が違うだけで、タグは同じ）へ移す。値と EcsEntity は保たれ、
 *   移動元は削除と同じ入れ替えで詰める。
 * - 作った行の値は全部 0。
 * - 更新への参加（存在と分ける）: 行ごとに参加の番号を持つ。作った行は SetCreateJoinSerial で決めた番号（既定 0 = 最初から参加）を
 *   受け取り、列の追加・削除・入れ替えで動いても保つ。システムは収集の番号以下の行だけを処理する（EcsChunk::IsRowJoined）。
 *   行の存在（クエリ・IsAlive・GetColumnValue）は参加に依らず、作った直後から見える。
 *
 * 版と変更ログ（設計 1.2）:
 * - World の版は uint64。AdvanceVersion()（システム 1 回ぶん・再生ごと）で進む。
 * - 「チャンク × 列」ごとに最後に書いた版を持つ（全列）。SetColumnTracked() した列（追跡する列）は、行ごとの版と
 *   変更ログも持つ。ログは「その版で書いた行」の並びで、同じ版の同じ行は 1 回しか積まない。
 * - 並列の仕事は MarkWritten で EcsChangeBundle へ積み、CommitChanges が仕事の順に連結してログへ移す。
 * - 段の外の書き込み（WriteColumnValue）と構造変更（作成・移動・入れ替え・孤児）は、消費者が読んだ後の最初の書き込みで
 *   版を +1 してから書くので、読み落としが起きない。作成・移動・入れ替えで動いた行は、全追跡列で「変わった」になる。
 * - 消費者（RegisterChangeConsumer）は、既読のログ位置と既読の版を持つ。消費者がいない列にはログを積まない。
 * クエリ（EcsQuery）は All/Any/None（列・タグ）とチャンクの版による絞り込みを持ち、孤児の行は既定で出ない。
 *
 * 作成・削除のフック（SetColumnHooks）は、列が生まれる・消えるときだけ呼ぶ（列の組が変わるだけの Move では呼ばない）。
 *
 * GameThread だけが触る（スレッドセーフではない）。並列の仕事が呼んでよいのは、MarkWritten 系と走査用の参照だけ
 * （その間は構造変更・SetColumnTracked・CommitChanges をしない）。並列の中の構造変更は EcsCommandBuffer に積む。
 */

#include "Container/Containers.h"
#include "Container/Span.h"
#include "Ecs/EcsTypes.h"
#include "Memory/PoolAllocator.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Ecs
{
    inline constexpr uint32_t kEcsChunkSize = 16 * 1024;
    inline constexpr uint32_t kEcsChunkHeaderSize = 64;

    /// 孤児の印（予約済みのタグのビット）。付いた行は、既定のクエリのどれにも出ない。
    inline constexpr uint64_t kEcsTagOrphan = 1ull << 63;

    /// 作成・削除のフックが呼ばれた理由。行がアーキタイプ間を移るだけ（Move）の列には、どのフックも呼ばれない。
    enum class EcsHookReason : uint8_t
    {
        /// 行の作成で、その列が生まれた
        Create,
        /// AddColumn で、その列が足された
        ColumnAdded,
        /// 行の削除で、その列が消える
        Destroy,
        /// RemoveColumn で、その列が外される
        ColumnRemoved,
    };

    /// フックの関数。value は列の値（作成・追加の直後、または削除・除去の直前）。フックの中では構造変更をしない。
    using EcsHookFn = void (*)(void *user, EcsEntity entity, const void *value, EcsHookReason reason);

    /// 列の型ごとの作成・削除のフック。どちらも null でよい。
    struct EcsColumnHooks
    {
        EcsHookFn OnCreate = nullptr;
        EcsHookFn OnDestroy = nullptr;
        void *User = nullptr;
    };

    /// 行の作成時に与える列の初期値。Size は列の大きさと一致しなければならない（食い違いは作成を拒む）。
    struct EcsColumnValue
    {
        TypeId Type = 0;
        const void *Data = nullptr;
        uint32_t Size = 0;
    };

    /// 変更ログの 1 項目。その版で書かれた（または構造変更で動いた）行の実体。
    struct EcsChangeEntry
    {
        EcsEntity Entity;
        uint64_t Version = 0;
    };

    /// 並列の仕事（チャンク 1 つの処理）ごとの変更の束。MarkWritten が積み、EcsWorld::CommitChanges が空にする。
    class EcsChangeBundle
    {
    public:
        void Clear()
        {
            m_Records.clear();
        }

        [[nodiscard]] size_t Size() const
        {
            return m_Records.size();
        }

    private:
        friend class EcsWorld;

        struct Record
        {
            uint32_t Log = 0;
            EcsChangeEntry Entry;
        };

        Container::VariableArray<Record> m_Records;
    };

    /// 変更の消費者の番号
    using EcsConsumerId = uint32_t;
    inline constexpr EcsConsumerId kInvalidEcsConsumer = 0xFFFFFFFFu;

    /// 走査の条件。列は型の番号、タグはビット。
    struct EcsQuery
    {
        /// すべて持つ列
        Container::VariableArray<TypeId> All;
        /// 空でなければ、どれか 1 つ以上持つ列
        Container::VariableArray<TypeId> Any;
        /// どれも持たない列
        Container::VariableArray<TypeId> None;
        uint64_t AllTags = 0;
        /// 0 でなければ、どれか 1 つ以上持つタグ
        uint64_t AnyTags = 0;
        uint64_t NoneTags = 0;
        /// 孤児（kEcsTagOrphan）の行も含める。既定は除く。
        bool bIncludeOrphans = false;
        /// 空でなければ、これらのどれかの列が ChangedSince より後の版で書かれたチャンクだけを通す（CollectChunks）
        Container::VariableArray<TypeId> Changed;
        uint64_t ChangedSince = 0;
    };

    /// 16 KB のブロックの先頭に置く見出し。実体の配列と列は、このあとにアーキタイプの配置どおりに続く。
    struct EcsChunk
    {
        EcsArchetypeId Archetype = kInvalidEcsArchetype;
        uint32_t Count = 0;
        uint32_t Capacity = 0;
        uint32_t IndexInArchetype = 0;
        /// このチャンクの行がこれまでに持った参加の番号の最大（行が抜けても下がらない。次にチャンクを確保するまで有効）
        uint64_t MaxJoinSerial = 0;

        /// 行 → 実体の配列（Count 個が有効）
        [[nodiscard]] EcsEntity *GetEntities()
        {
            return reinterpret_cast<EcsEntity *>(reinterpret_cast<std::byte *>(this) + kEcsChunkHeaderSize);
        }

        [[nodiscard]] const EcsEntity *GetEntities() const
        {
            return reinterpret_cast<const EcsEntity *>(reinterpret_cast<const std::byte *>(this) + kEcsChunkHeaderSize);
        }

        /// 行 → 更新への参加の番号の配列（実体の配列の直後。Count 個が有効）
        [[nodiscard]] uint64_t *GetJoinSerials()
        {
            return reinterpret_cast<uint64_t *>(reinterpret_cast<std::byte *>(this) + kEcsChunkHeaderSize +
                                                sizeof(EcsEntity) * static_cast<size_t>(Capacity));
        }

        [[nodiscard]] const uint64_t *GetJoinSerials() const
        {
            return reinterpret_cast<const uint64_t *>(reinterpret_cast<const std::byte *>(this) + kEcsChunkHeaderSize +
                                                      sizeof(EcsEntity) * static_cast<size_t>(Capacity));
        }

        /// 全行が collectionSerial の収集に参加するか（false なら行ごとに IsRowJoined で飛ばす）
        [[nodiscard]] bool AllRowsJoined(uint64_t collectionSerial) const
        {
            return MaxJoinSerial <= collectionSerial;
        }

        /// 行 row が collectionSerial の収集に参加するか。参加の番号が収集の番号以下の行だけが参加する。
        [[nodiscard]] bool IsRowJoined(uint32_t row, uint64_t collectionSerial) const
        {
            return GetJoinSerials()[row] <= collectionSerial;
        }
    };
    static_assert(sizeof(EcsChunk) <= kEcsChunkHeaderSize, "チャンクの見出しは 64 byte に収める");
    static_assert(sizeof(EcsEntity) == 8, "参加の番号の配列は実体の配列の直後に 8 byte 境界で置く");

    class EcsWorld
    {
    public:
        EcsWorld();
        ~EcsWorld();

        EcsWorld(const EcsWorld &) = delete;
        EcsWorld &operator=(const EcsWorld &) = delete;

        /**
         * @brief 列の組とタグから、アーキタイプを取得する（無ければ作る）
         * @param columns 列の記述。順序は問わない（型の番号の昇順に並べて照合する）
         * @param tags タグのビット（列を持たない印。アーキタイプの同一性に含む）
         * @return アーキタイプの番号。同じ型が重複する・行が 16 KB に収まらない・
         *         型が TypeRegistry に無い、または大きさ・配置が登録と食い違うときは kInvalidEcsArchetype（タグや列の組が違っても毎回照合する）
         */
        EcsArchetypeId GetOrCreateArchetype(Container::Span<const EcsColumnInfo> columns, uint64_t tags = 0);

        /**
         * @brief 1 行を作る（値は 0。initialValues があればその列だけ上書きする）
         * @return 作った実体。アーキタイプが不正・初期値の列がアーキタイプに無い・フックの中からの呼び出しなら null
         *         （OnCreate は初期値を入れた後に呼ぶ）
         */
        EcsEntity CreateEntity(EcsArchetypeId archetype, Container::Span<const EcsColumnValue> initialValues = {});

        /**
         * @brief まとめて count 行を作る（値は 0）
         * @param outEntities 非 null なら、作った実体を作成順に末尾へ足す
         * @return 作った数（アーキタイプが不正なら 0）
         */
        uint32_t CreateEntities(EcsArchetypeId archetype, uint32_t count, Container::VariableArray<EcsEntity> *outEntities = nullptr);

        /// 1 行を削除する（末尾の行との入れ替え）。古い識別子・null は false で何もしない。
        bool DestroyEntity(EcsEntity entity);

        /**
         * @brief 実体に列を足す（行は列を 1 つ増やしたアーキタイプへ移る）
         * @param initialValue 非 null なら column.Size byte をコピーする。null なら 0
         * @return 成功なら true。生きていない・すでにその列を持つ・列の記述が不正（登録と食い違う）ときは false で何もしない
         */
        bool AddColumn(EcsEntity entity, const EcsColumnInfo &column, const void *initialValue = nullptr);

        /// 実体から列 type を外す（行は列を 1 つ減らしたアーキタイプへ移る）。生きていない・その列を持たないときは false。
        bool RemoveColumn(EcsEntity entity, TypeId type);

        template <typename T>
        bool AddColumnOf(EcsEntity entity, const T &value = T{})
        {
            return AddColumn(entity, EcsColumnInfoOf<T>(), &value);
        }

        template <typename T>
        bool RemoveColumnOf(EcsEntity entity)
        {
            return RemoveColumn(entity, EcsColumnInfoOf<T>().Type);
        }

        /**
         * @brief 列の型 type の作成・削除のフックを設定する（置き換え。両方 null なら外す）
         *
         * - OnCreate: 行の作成（reason Create）と AddColumn（ColumnAdded）で、その列が生まれたとき。
         * - OnDestroy: 行の削除（Destroy）と RemoveColumn（ColumnRemoved）で、その列が消えるとき。
         * - 列の組が変わるだけの移動（Move）で残る列には、OnDestroy も OnCreate も呼ばない。
         *   孤児の印（MarkOrphan）も移動なので呼ばない。
         * - フックの中の構造変更（作成・削除・列の追加と除去・孤児）は false / null で何もしない。
         * - EcsWorld の破棄では呼ばない。
         * @return type が無効なら false
         */
        bool SetColumnHooks(TypeId type, const EcsColumnHooks &hooks);

        [[nodiscard]] bool IsAlive(EcsEntity entity) const;

        /// これから作る行が受け取る、更新への参加の番号を決める（スケジューラが「次の収集」の番号を入れる）
        void SetCreateJoinSerial(uint64_t serial)
        {
            m_CreateJoinSerial = serial;
        }

        [[nodiscard]] uint64_t GetCreateJoinSerial() const
        {
            return m_CreateJoinSerial;
        }

        /// 実体の更新への参加の番号。生きていなければ 0。
        [[nodiscard]] uint64_t GetJoinSerial(EcsEntity entity) const;

        /// 実体の行が属するアーキタイプ。生きていなければ kInvalidEcsArchetype。
        [[nodiscard]] EcsArchetypeId GetArchetypeOf(EcsEntity entity) const;

        // ---- 版と変更ログ ----

        /**
         * @brief 列 type を追跡する列にする（行ごとの版と変更ログを持つ）
         * @return 成功（すでに追跡していれば true）。type の列を持つアーキタイプがすでにあるときは false
         *         （チャンクの配置が変わるので、最初のアーキタイプを作る前に呼ぶ）
         */
        bool SetColumnTracked(TypeId type);

        template <typename T>
        bool SetColumnTrackedOf()
        {
            return SetColumnTracked(EcsColumnInfoOf<T>().Type);
        }

        [[nodiscard]] bool IsColumnTracked(TypeId type) const;

        /// いまの版（1 から始まる。0 は「一度も書かれていない」）
        [[nodiscard]] uint64_t GetVersion() const
        {
            return m_Version;
        }

        /// 版を 1 進めて返す（システム 1 回ぶん、コマンドの再生ごと）。以後の書き込みはこの版になる。
        uint64_t AdvanceVersion();

        /// 追跡する列 type の変更の消費者を登録する。既読はいまのログの末尾・いまの版から始まる。登録できなければ kInvalidEcsConsumer。
        EcsConsumerId RegisterChangeConsumer(TypeId trackedType);
        bool UnregisterChangeConsumer(EcsConsumerId consumer);

        /**
         * @brief 前回の読みより後の変更を out の末尾へ足し、既読を進める
         * @return 足した数（すでに死んだ実体の項目は除く）。同じ行でも版が違えば別の項目になる。
         *         読んだ時点で、いまの版を既読の版にする（以後の段の外の書き込みは新しい版になる）。
         */
        uint32_t ReadChanges(EcsConsumerId consumer, Container::VariableArray<EcsChangeEntry> &out);

        /// 消費者が最後に読んだ時点の版。EcsQuery::ChangedSince に渡す。無効な消費者は 0。
        [[nodiscard]] uint64_t GetConsumerReadVersion(EcsConsumerId consumer) const;

        /// 追跡する列 type のログに残っている項目の数（全員が読み終えた分は捨てられる）
        [[nodiscard]] size_t GetChangeLogLength(TypeId trackedType) const;

        /**
         * @brief 並列の仕事から、チャンクの行 [firstRow, firstRow + count) の列 columnIndex を書いたと記録する
         *
         * チャンク × 列の版を version にし、追跡する列なら行の版を比べて、まだその版でない行だけを bundle へ積む。
         * 1 つのチャンクを処理する仕事は 1 つだけ、同じ列を書く 2 つのシステムは同じ波に入らない、が前提。
         */
        void MarkWritten(EcsChunk *chunk, uint32_t columnIndex, uint32_t row, uint64_t version, EcsChangeBundle &bundle) const;
        void MarkWrittenRange(EcsChunk *chunk, uint32_t columnIndex, uint32_t firstRow, uint32_t count, uint64_t version,
                              EcsChangeBundle &bundle) const;

        /// 束を先頭から順に連結してログへ移し、束を空にする（GameThread、並列の外）
        void CommitChanges(Container::Span<EcsChangeBundle *const> bundlesInJobOrder);

        /// 段の外の書き込み: 値を列の大きさだけコピーし、変更として記録する。生きていない・その列を持たないときは false。
        bool WriteColumnValue(EcsEntity entity, TypeId type, const void *value);

        template <typename T>
        bool WriteColumnValueOf(EcsEntity entity, const T &value)
        {
            return WriteColumnValue(entity, EcsColumnInfoOf<T>().Type, &value);
        }

        /// 段の外の書き込み（列の一部）: 列 type の [offset, offset + size) だけ書き、WriteColumnValue と同じく変更を記録する。
        /// 生きていない・その列を持たない・範囲が列の外・value が null なら false。
        bool WriteColumnPart(EcsEntity entity, TypeId type, uint32_t offset, uint32_t size, const void *value);

        /// 実体の追跡する列 type の行の版。追跡していない・生きていない・その列を持たないときは 0。
        [[nodiscard]] uint64_t GetRowVersion(EcsEntity entity, TypeId type) const;

        /// チャンクの列 columnIndex を最後に書いた版（追跡しない列も持つ）
        [[nodiscard]] uint64_t GetChunkColumnVersion(const EcsChunk *chunk, uint32_t columnIndex) const;

        /// チャンクの追跡する列 columnIndex の、行ごとの版の配列（Count 個が有効）。追跡しない列は nullptr。
        [[nodiscard]] const uint64_t *GetChunkRowVersions(const EcsChunk *chunk, uint32_t columnIndex) const;

        // ---- クエリ ----

        /// 条件に合うアーキタイプを out の末尾へ足す（ChangedSince はチャンク単位なので見ない）。Changed の列を 1 つも持たないものは除く。
        void MatchArchetypes(const EcsQuery &query, Container::VariableArray<EcsArchetypeId> &out) const;

        /// 条件に合うチャンクを out の末尾へ足し、足した数を返す。Changed が空でなければ、その列のどれかが ChangedSince より後に書かれたチャンクだけ。
        uint32_t CollectChunks(const EcsQuery &query, Container::VariableArray<EcsChunk *> &out) const;

        /// 行に孤児の印を付ける（列の値は保ち、タグに kEcsTagOrphan を足したアーキタイプへ移る）。生きていない・すでに孤児なら false。
        bool MarkOrphan(EcsEntity entity);
        [[nodiscard]] bool IsOrphan(EcsEntity entity) const;

        /// 実体の列 type の値へのポインタ。生きていない・その列を持たないときは nullptr。
        /// 行の入れ替えや削除で動くので、構造変更をまたいで持たない。
        [[nodiscard]] void *GetColumnValue(EcsEntity entity, TypeId type);
        [[nodiscard]] const void *GetColumnValue(EcsEntity entity, TypeId type) const;

        template <typename T>
        [[nodiscard]] T *GetColumnValue(EcsEntity entity)
        {
            return static_cast<T *>(GetColumnValue(entity, EcsColumnInfoOf<T>().Type));
        }

        template <typename T>
        [[nodiscard]] const T *GetColumnValue(EcsEntity entity) const
        {
            return static_cast<const T *>(GetColumnValue(entity, EcsColumnInfoOf<T>().Type));
        }

        // ---- チャンク単位の参照（走査用） ----

        [[nodiscard]] uint32_t GetArchetypeCount() const;
        [[nodiscard]] Container::Span<const EcsColumnInfo> GetArchetypeColumns(EcsArchetypeId archetype) const;
        [[nodiscard]] uint64_t GetArchetypeTags(EcsArchetypeId archetype) const;
        /// 1 チャンクに入る行数
        [[nodiscard]] uint32_t GetArchetypeChunkCapacity(EcsArchetypeId archetype) const;
        [[nodiscard]] uint32_t GetArchetypeRowCount(EcsArchetypeId archetype) const;
        [[nodiscard]] Container::Span<EcsChunk *const> GetArchetypeChunks(EcsArchetypeId archetype) const;
        /// 列の型 type の、アーキタイプ内の列番号。無ければ -1。
        [[nodiscard]] int32_t FindColumnIndex(EcsArchetypeId archetype, TypeId type) const;
        /// チャンクの列 columnIndex の先頭（Count 個が有効）
        [[nodiscard]] void *GetChunkColumn(EcsChunk *chunk, uint32_t columnIndex) const;
        [[nodiscard]] const void *GetChunkColumn(const EcsChunk *chunk, uint32_t columnIndex) const;

        // ---- 数の確認 ----

        /// 生きている実体（行）の数
        [[nodiscard]] uint32_t GetEntityCount() const
        {
            return m_EntityCount;
        }

        /// 使用中のチャンクの数
        [[nodiscard]] uint32_t GetChunkCount() const
        {
            return m_ChunkCount;
        }

        /// プールが確保済みのチャンク用メモリの大きさ（解放したブロックを含む。再利用で増えないことの確認用）
        [[nodiscard]] size_t GetChunkPoolBytes() const
        {
            return m_ChunkPool.GetTotalSize();
        }

    private:
        struct Archetype
        {
            Container::VariableArray<EcsColumnInfo> Columns;
            Container::VariableArray<uint32_t> Offsets;
            /// 列ごとの追跡番号（m_Logs の添字）。追跡しない列は -1。
            Container::VariableArray<int32_t> TrackedIndex;
            /// 追跡する列の、行ごとの版の配列のチャンク内の位置。追跡しない列は 0。
            Container::VariableArray<uint32_t> RowVersionOffsets;
            /// 列ごとのチャンクの版（uint64 × 列数）のチャンク内の位置
            uint32_t ChunkVersionOffset = 0;
            uint64_t Tags = 0;
            uint32_t Capacity = 0;
            uint32_t RowCount = 0;
            Container::VariableArray<EcsChunk *> Chunks;
        };

        /// 実体の表の 1 項目。Chunk が null なら空き（Row は空き一覧の次の番号）。
        struct Slot
        {
            EcsChunk *Chunk = nullptr;
            uint32_t Row = 0;
            uint32_t Generation = 1;
        };

        /// 追跡する列 1 本の変更ログ。Entries[0] のログ位置（通し番号）は BaseSeq。
        struct ChangeLog
        {
            TypeId Type = 0;
            Container::VariableArray<EcsChangeEntry> Entries;
            uint64_t BaseSeq = 0;
            uint32_t ConsumerCount = 0;
            /// 同じ実体・版の項目が重なりうる間 true（読むときに除く）。全員が読み切ると戻る
            bool bMayDuplicate = false;
        };

        struct Consumer
        {
            uint32_t Log = 0;
            uint64_t Cursor = 0;
            uint64_t ReadVersion = 0;
            bool bActive = false;
        };

        static constexpr uint32_t kNoSlot = 0xFFFFFFFFu;

        struct HookEntry
        {
            TypeId Type = 0;
            EcsColumnHooks Hooks;
        };

        [[nodiscard]] const Slot *FindSlot(EcsEntity entity) const;
        /// 行を作る（フックは呼ばない）。作った実体は outEntities の末尾へ足す。
        uint32_t CreateRows(EcsArchetypeId archetype, uint32_t count, Container::VariableArray<EcsEntity> *outEntities);
        [[nodiscard]] const EcsColumnHooks *FindHooks(TypeId type) const;
        /// 実体の全列について、フックを呼ぶ（OnCreate は create が true）
        void FireRowHooks(EcsEntity entity, bool create, EcsHookReason reason);
        /// 実体の列 column のフックを 1 つ呼ぶ
        void FireColumnHook(EcsEntity entity, TypeId type, bool create, EcsHookReason reason);
        EcsChunk *AcquireChunk(EcsArchetypeId archetypeId);
        void ReleaseLastChunk(Archetype &archetype);
        EcsEntity AcquireSlot();
        void ZeroRows(const Archetype &archetype, EcsChunk *chunk, uint32_t firstRow, uint32_t rowCount) const;
        /// 末尾の行を row へ移して詰める。末尾のチャンクが空になれば解放する（実体の表のこの行と m_EntityCount は呼び出し側）。
        void RemoveRow(Archetype &archetype, EcsChunk *chunk, uint32_t row);
        /// 実体の行を target の末尾へ移す。共通の列の値をコピーし、added があればその列に addedValue（null なら 0）を入れる。
        void MoveRow(EcsEntity entity, EcsArchetypeId target, const EcsColumnInfo *added, const void *addedValue);
        /// 構造変更・段の外の書き込みに使う版。消費者が読んだ後の最初の書き込みなら +1 する。
        uint64_t AcquireWriteVersion();
        /// 行の全列を version で「変わった」にする（追跡する列はログにも積む）
        void TouchRow(const Archetype &archetype, EcsChunk *chunk, uint32_t row, uint64_t version);
        void AppendLog(int32_t logIndex, const EcsChangeEntry &entry);
        void TrimLog(ChangeLog &log);
        /// 追跡する列 type のログの番号。追跡しなければ -1。
        [[nodiscard]] int32_t FindLogIndex(TypeId type) const;

        Container::VariableArray<Archetype> m_Archetypes;
        Container::VariableArray<Slot> m_Slots;
        uint32_t m_FreeSlotHead = kNoSlot;
        uint32_t m_EntityCount = 0;
        uint32_t m_ChunkCount = 0;
        uint64_t m_Version = 1;
        /// いまの版を消費者が読んだ（以後の段の外の書き込みは版を進めてから書く）
        bool m_bVersionSealed = false;
        Container::VariableArray<ChangeLog> m_Logs;
        Container::VariableArray<Consumer> m_Consumers;
        Container::VariableArray<HookEntry> m_Hooks;
        /// フックを呼んでいる間の深さ（0 でなければ構造変更を拒む）
        uint32_t m_HookDepth = 0;
        /// 作る行が受け取る参加の番号
        uint64_t m_CreateJoinSerial = 0;
        Memory::PoolAllocator<false> m_ChunkPool;
    };

}
