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
 * - 作った行の値は全部 0。
 *
 * GameThread だけが触る（スレッドセーフではない）。列の追加・削除、版、クエリ、コマンドバッファは別の層で足す。
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

    /// 16 KB のブロックの先頭に置く見出し。実体の配列と列は、このあとにアーキタイプの配置どおりに続く。
    struct EcsChunk
    {
        EcsArchetypeId Archetype = kInvalidEcsArchetype;
        uint32_t Count = 0;
        uint32_t Capacity = 0;
        uint32_t IndexInArchetype = 0;

        /// 行 → 実体の配列（Count 個が有効）
        [[nodiscard]] EcsEntity *GetEntities()
        {
            return reinterpret_cast<EcsEntity *>(reinterpret_cast<std::byte *>(this) + kEcsChunkHeaderSize);
        }

        [[nodiscard]] const EcsEntity *GetEntities() const
        {
            return reinterpret_cast<const EcsEntity *>(reinterpret_cast<const std::byte *>(this) + kEcsChunkHeaderSize);
        }
    };
    static_assert(sizeof(EcsChunk) <= kEcsChunkHeaderSize, "チャンクの見出しは 64 byte に収める");

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

        /// 1 行を作る（値は 0）。アーキタイプが不正なら null。
        EcsEntity CreateEntity(EcsArchetypeId archetype);

        /**
         * @brief まとめて count 行を作る（値は 0）
         * @param outEntities 非 null なら、作った実体を作成順に末尾へ足す
         * @return 作った数（アーキタイプが不正なら 0）
         */
        uint32_t CreateEntities(EcsArchetypeId archetype, uint32_t count, Container::VariableArray<EcsEntity> *outEntities = nullptr);

        /// 1 行を削除する（末尾の行との入れ替え）。古い識別子・null は false で何もしない。
        bool DestroyEntity(EcsEntity entity);

        [[nodiscard]] bool IsAlive(EcsEntity entity) const;

        /// 実体の行が属するアーキタイプ。生きていなければ kInvalidEcsArchetype。
        [[nodiscard]] EcsArchetypeId GetArchetypeOf(EcsEntity entity) const;

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

        static constexpr uint32_t kNoSlot = 0xFFFFFFFFu;

        [[nodiscard]] const Slot *FindSlot(EcsEntity entity) const;
        EcsChunk *AcquireChunk(EcsArchetypeId archetypeId);
        void ReleaseLastChunk(Archetype &archetype);
        EcsEntity AcquireSlot();
        void ZeroRows(const Archetype &archetype, EcsChunk *chunk, uint32_t firstRow, uint32_t rowCount) const;

        Container::VariableArray<Archetype> m_Archetypes;
        Container::VariableArray<Slot> m_Slots;
        uint32_t m_FreeSlotHead = kNoSlot;
        uint32_t m_EntityCount = 0;
        uint32_t m_ChunkCount = 0;
        Memory::PoolAllocator<false> m_ChunkPool;
    };

}
