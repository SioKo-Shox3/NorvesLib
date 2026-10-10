#pragma once

/**
 * @file EcsCommandBuffer.h
 * @brief 並列の仕事・段の中から積む構造変更のコマンドバッファと、決定的な再生
 *
 * - 仕事（チャンク 1 つの処理など）ごとに 1 つのバッファを持ち、自分のバッファにだけ積む（バッファ同士は触れ合わない）。
 * - 再生（EcsCommandBuffer::Replay）は、渡されたバッファの順（システムの順 → チャンクの番号）に、各バッファの中は積んだ順に
 *   EcsWorld へ当てる。どのスレッドがどの仕事を走らせたかに依らず、結果は同じになる。
 * - コマンドが運ぶのは EcsEntity と POD だけ。再生のときに Generation を照合し、死んだ実体へのコマンドは捨てる。
 * - 作成は予約札（EcsCreateTicket）を返す。再生の前なら CancelCreate で取り消せて、取り消した作成は再生されない。
 *   同じバッファの後続のコマンドは、実体の代わりに札を対象にできる（作成が取り消し・失敗なら捨てる）。
 * - 再生は、コマンドがあれば World の版を 1 つ進めてから当てる。作成した実体は ResolveCreate で引く。
 *   バッファは Clear() するまで再生済みのまま残り、二重には再生しない。
 */

#include "Container/Containers.h"
#include "Container/Span.h"
#include "Ecs/EcsWorld.h"

#include <cstdint>

namespace NorvesLib::Core::Ecs
{
    /// 作成の予約札。積んだバッファに対してだけ意味を持つ。
    struct EcsCreateTicket
    {
        uint32_t Buffer = 0xFFFFFFFFu;
        uint32_t Index = 0xFFFFFFFFu;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Index != 0xFFFFFFFFu;
        }
    };

    /// コマンドの対象。実体、または同じバッファで予約した作成の札。
    struct EcsCommandTarget
    {
        EcsEntity Entity;
        EcsCreateTicket Ticket;

        constexpr EcsCommandTarget(EcsEntity entity)
            : Entity(entity)
        {
        }

        constexpr EcsCommandTarget(EcsCreateTicket ticket)
            : Ticket(ticket)
        {
        }
    };

    /// 再生の結果の集計
    struct EcsReplayResult
    {
        /// 当てたコマンドの数（作成を含む）
        uint32_t Applied = 0;
        /// 作った実体の数
        uint32_t Created = 0;
        /// 取り消されていて作らなかった数
        uint32_t CancelledCreates = 0;
        /// 対象が死んでいた（Generation が合わない・札の作成が取り消し／失敗）ので捨てた数
        uint32_t DiscardedStale = 0;
        /// 対象は生きていたが World が拒んだ数（すでにある列の追加・無い列の除去・不正な値など）
        uint32_t Rejected = 0;
    };

    /// 1 つの仕事が積むコマンドの束。GameThread 以外から使ってよいのは、自分の仕事のバッファへの積み込みだけ。
    class EcsCommandBuffer
    {
    public:
        /// bufferId は札の持ち主を見分ける番号（再生の順とは無関係。同じ再生で重ならない番号を渡す）
        explicit EcsCommandBuffer(uint32_t bufferId = 0);

        /// 作成を積む。initialValues の値は積むときにコピーする。
        EcsCreateTicket Create(EcsArchetypeId archetype, Container::Span<const EcsColumnValue> initialValues = {});

        void Destroy(EcsCommandTarget target);
        void AddColumn(EcsCommandTarget target, const EcsColumnInfo &column, const void *value = nullptr);
        void RemoveColumn(EcsCommandTarget target, TypeId type);
        /// 列の値の書き込み（size は列の大きさと一致しなければ、再生で拒まれる）
        void Write(EcsCommandTarget target, TypeId type, const void *value, uint32_t size);

        template <typename T>
        void AddColumnOf(EcsCommandTarget target, const T &value = T{})
        {
            AddColumn(target, EcsColumnInfoOf<T>(), &value);
        }

        template <typename T>
        void RemoveColumnOf(EcsCommandTarget target)
        {
            RemoveColumn(target, EcsColumnInfoOf<T>().Type);
        }

        template <typename T>
        void WriteOf(EcsCommandTarget target, const T &value)
        {
            Write(target, EcsColumnInfoOf<T>().Type, &value, static_cast<uint32_t>(sizeof(T)));
        }

        /**
         * @brief 再生の前の作成を取り消す（その作成は再生されず、札を対象にしたコマンドは捨てられる）
         * @return 取り消せたら true。別のバッファの札・無効な札・取り消し済み・再生済みは false
         *         （再生済みなら、できた実体を ResolveCreate で引いて消す）
         */
        bool CancelCreate(EcsCreateTicket ticket);

        /// 再生で作られた実体。再生の前・取り消し・失敗・別のバッファの札は null。
        [[nodiscard]] EcsEntity ResolveCreate(EcsCreateTicket ticket) const;

        /// 積んだコマンドを全部捨てる（札も無効になる）。再生済みの印も戻る。
        void Clear();

        [[nodiscard]] size_t Size() const
        {
            return m_Commands.size();
        }

        [[nodiscard]] uint32_t GetBufferId() const
        {
            return m_BufferId;
        }

        [[nodiscard]] bool IsReplayed() const
        {
            return m_bReplayed;
        }

        /**
         * @brief バッファを渡された順に World へ当てる（GameThread、並列の外）
         *
         * 再生済みのバッファ・null は飛ばす。当てるコマンドが 1 つでもあれば、先に World の版を進める。
         * 当て終えたバッファは再生済みになる（Clear するまで二重に当てない）。
         */
        static EcsReplayResult Replay(EcsWorld &world, Container::Span<EcsCommandBuffer *const> buffersInJobOrder);

    private:
        enum class Kind : uint8_t
        {
            Create,
            Destroy,
            AddColumn,
            RemoveColumn,
            Write,
        };

        static constexpr uint32_t kNone = 0xFFFFFFFFu;

        struct Command
        {
            Kind CommandKind = Kind::Destroy;
            /// Create 以外の対象（TargetCreate が kNone でなければ札を優先する）
            EcsEntity Entity;
            uint32_t TargetCreate = kNone;
            /// Create: 自分の作成の番号（m_Creates の添字）
            uint32_t CreateIndex = kNone;
            EcsArchetypeId Archetype = kInvalidEcsArchetype;
            TypeId Type = 0;
            uint32_t Size = 0;
            uint32_t Align = 1;
            /// 値のコピーの位置（m_Payload）。Create は ValueCount 個の（型・大きさ・値）の並び
            uint32_t PayloadOffset = 0;
            uint32_t ValueCount = 0;
            bool bHasValue = false;
        };

        struct CreateRecord
        {
            bool bCancelled = false;
            EcsEntity Entity;
        };

        Command MakeCommand(Kind kind, const EcsCommandTarget &target) const;
        uint32_t AppendPayload(const void *data, uint32_t size);
        /// 当てた結果の集計に加える。対象が死んでいたら DiscardedStale
        void Apply(EcsWorld &world, const Command &command, EcsReplayResult &result);

        uint32_t m_BufferId = 0;
        bool m_bReplayed = false;
        Container::VariableArray<Command> m_Commands;
        Container::VariableArray<CreateRecord> m_Creates;
        Container::VariableArray<uint8_t> m_Payload;
    };

}
