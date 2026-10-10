#pragma once

/**
 * @file EcsTransformSystem.h
 * @brief 階層の変換のシステム: 局所の変換・親・Active の変更から、ワールドの変換（EcsWorldTransform）と EffectiveActive を伝播で求める
 *
 * 設計 1.9 の伝播。伝播のきっかけは、追跡する列 EcsLocalTransform・EcsHierarchy（Parent）・EcsEntityFlags（Active・PendingDestroy）の
 * 変更ログ（作成された行は全列が「変わった」になる）。変わった行を Depth ごとのバケツに積み、Depth 0 から順に処理する。
 *   - 行のワールドの変換 = 親のワールドの変換 × 局所の変換（旧の再帰 Entity.cpp の ComposeTransform と同じ式）。根の親は単位。
 *   - EffectiveActive = 親の EffectiveActive かつ 自分が Active かつ PendingDestroy でない。根の親は true。
 *   - ワールドの変換か EffectiveActive が変わった行（初めて求めた行を含む）は、その子を次の Depth のバケツへ積む。
 * 処理した行の「訪れた番号」（EcsPropagatedTransform::Visited）で、同じ伝播での二重の処理を除く。
 *
 * ワールドの変換は EcsWorldTransform の列へ書く（値が変わった行だけ。WriteColumnValue を通すので、チャンクの版が進む）。
 * EffectiveActive・訪れた番号・求めたかどうかは EcsPropagatedTransform（追跡しない列）に置く。
 * 検査 ECS_TRANSFORM は、この結果を、木を根から全部たどって求め直した値と全行照合する。
 *
 * 並列のしかた: 1 つの Depth の行は互いに独立なので、行ごとの計算（列の読み取りと式の評価だけ）を ParallelFor で回す。
 * 計算の結果は行ごとの置き場に出し、列への書き込みと次の Depth への積み込みは GameThread が行の順に行う。
 * だから直列でも並列でも、列の値と処理した順は同じになる（決定的）。重複の除去も、この直列の段階で行う。
 *
 * GameThread だけが Run を呼ぶ（並列の外）。EcsWorld は構造変更をしない間だけ読む。
 */

#include "Container/Containers.h"
#include "Ecs/EcsTypes.h"
#include "Ecs/EcsWorld.h"
#include "Math/Transform.h"
#include "Thread/JobSystem.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Ecs
{
    /// 親のワールドの変換と局所の変換から、子のワールドの変換を求める（旧の再帰と同じ式。浮動小数の結果が bit まで一致する）
    inline Math::Transform EcsComposeTransform(const Math::Transform &parent, const Math::Transform &local)
    {
        const Math::Vector3 scaledPosition(parent.scale.x * local.position.x, parent.scale.y * local.position.y,
                                           parent.scale.z * local.position.z);
        const Math::Vector3 scale(parent.scale.x * local.scale.x, parent.scale.y * local.scale.y,
                                  parent.scale.z * local.scale.z);
        return Math::Transform(parent.position + parent.rotation * scaledPosition, parent.rotation * local.rotation, scale);
    }

    /// 浮動小数 2 つが同じ値か。数値として等しければ同じ（+0 と -0 も同じ。旧の再帰は数値の == で等しければキャッシュを
    /// 更新しないので、同じ関係で並べる）。NaN は同じ bit のときだけ同じ（== だと NaN の行が毎回「変わった」になる）
    inline bool EcsSameBits(float lhs, float rhs)
    {
        return lhs == rhs || std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
    }

    /// 列に置く変換が同じ値か（EcsSameBits の関係で全成分を並べる。旧の再帰が同じ式で求めた値と並べるので、同じ入力なら一致する）
    inline bool EcsEqualWorldTransform(const EcsWorldTransform &lhs, const EcsWorldTransform &rhs)
    {
        return EcsSameBits(lhs.position.x, rhs.position.x) && EcsSameBits(lhs.position.y, rhs.position.y) &&
               EcsSameBits(lhs.position.z, rhs.position.z) && EcsSameBits(lhs.rotation.x, rhs.rotation.x) &&
               EcsSameBits(lhs.rotation.y, rhs.rotation.y) && EcsSameBits(lhs.rotation.z, rhs.rotation.z) &&
               EcsSameBits(lhs.rotation.w, rhs.rotation.w) && EcsSameBits(lhs.scale.x, rhs.scale.x) &&
               EcsSameBits(lhs.scale.y, rhs.scale.y) && EcsSameBits(lhs.scale.z, rhs.scale.z);
    }

    class EcsTransformSystem
    {
    public:
        /// 1 回の Run の結果
        struct RunResult
        {
            /// 重複を除いた、最初のバケツに積んだ行の数
            uint32_t Seeds = 0;
            /// 処理した行の数（きっかけの行と、伝播で積まれた子孫）
            uint32_t Processed = 0;
            /// 処理した行のうち、ワールドの変換か EffectiveActive が変わった行の数
            uint32_t Changed = 0;
            /// 処理した Depth の数（空でないバケツ）
            uint32_t Levels = 0;
            /// 求められなかった行の数（親が一度も求められていない）。0 でなければ不整合
            uint32_t Unresolved = 0;
        };

        /// 想定する木の深さの上限。これ以上の Depth の行は処理しない（不整合の防御）
        static constexpr uint32_t kMaxDepth = 1u << 16;

        /**
         * @brief world の追跡する列の変更の消費者を登録する
         *
         * EcsLocalTransform・EcsHierarchy・EcsEntityFlags が追跡する列でないとき（最初のアーキタイプの前に SetColumnTracked
         * していない）は消費者を持てないので、Run のたびに全行をきっかけにする（結果は同じで、費用が行数に比例する）。
         * 最初の Run は、登録より前にあった行を取りこぼさないよう、必ず全行をきっかけにする。
         */
        explicit EcsTransformSystem(EcsWorld &world)
            : m_World(world)
        {
            m_Local = EcsColumnInfoOf<EcsLocalTransform>().Type;
            m_Hierarchy = EcsColumnInfoOf<EcsHierarchy>().Type;
            m_Flags = EcsColumnInfoOf<EcsEntityFlags>().Type;
            m_Propagated = EcsColumnInfoOf<EcsPropagatedTransform>().Type;
            m_WorldType = EcsColumnInfoOf<EcsWorldTransform>().Type;
            m_Consumers[0] = world.RegisterChangeConsumer(m_Local);
            m_Consumers[1] = world.RegisterChangeConsumer(m_Hierarchy);
            m_Consumers[2] = world.RegisterChangeConsumer(m_Flags);
            m_bIncremental = true;
            for (const EcsConsumerId consumer : m_Consumers)
            {
                if (consumer == kInvalidEcsConsumer)
                {
                    m_bIncremental = false;
                }
            }
        }

        ~EcsTransformSystem()
        {
            for (const EcsConsumerId consumer : m_Consumers)
            {
                if (consumer != kInvalidEcsConsumer)
                {
                    m_World.UnregisterChangeConsumer(consumer);
                }
            }
        }

        EcsTransformSystem(const EcsTransformSystem &) = delete;
        EcsTransformSystem &operator=(const EcsTransformSystem &) = delete;

        /// 行ごとの計算を ParallelFor で回すか。minCount 以上の行があるバケツだけ並列にする（0 なら常に並列）。grain は 1 回の取り出しの行数
        void SetParallel(bool bEnabled, size_t minCount = 512, size_t grain = 128)
        {
            m_bParallel = bEnabled;
            m_ParallelMinCount = minCount;
            m_ParallelGrain = grain == 0 ? 1 : grain;
        }

        /**
         * @brief 前回の Run より後に EcsEntityFlags（Active・PendingDestroy）の変更があったか
         *
         * true の間、行の EffectiveActive は古いかもしれない。消費者を持てないときは常に true。
         */
        [[nodiscard]] bool HasUnpropagatedFlagChanges() const
        {
            return !m_bIncremental || m_World.GetChangeLogLength(m_Flags) != 0;
        }

        [[nodiscard]] bool IsParallel() const
        {
            return m_bParallel;
        }

        /// 次の Run で全行をきっかけにする（変更の取りこぼしを疑うときの再同期）
        void RequestFullSeed()
        {
            m_bFullSeed = true;
        }

        /// 処理した行を順に記録するか（テスト用。Run のたびに先頭へ戻す）。記録は処理した順の EcsEntity
        void SetRecordOrder(bool bEnabled)
        {
            m_bRecordOrder = bEnabled;
            m_Order.clear();
        }

        [[nodiscard]] const Container::VariableArray<EcsEntity> &GetProcessedOrder() const
        {
            return m_Order;
        }

        /// 直近の Run で処理して値を求めた行（処理した順）と、そのうちワールドの変換の値が変わった行の印
        struct AppliedRow
        {
            EcsEntity Entity;
            bool bWorldChanged = false;
        };

        [[nodiscard]] const Container::VariableArray<AppliedRow> &GetApplied() const
        {
            return m_Applied;
        }

        /// 伝播を 1 回行う。変更が無ければ（消費者のログが空なら）何もしない
        RunResult Run()
        {
            RunResult result;
            m_Applied.clear();
            if (m_bRecordOrder)
            {
                m_Order.clear();
            }
            const uint64_t serial = ++m_Serial;
            m_MaxDepth = 0;
            for (Container::VariableArray<EcsEntity> &bucket : m_Buckets)
            {
                bucket.clear();
            }

            // ---- きっかけ: 変更ログ（初回・消費者を持てないときは全行） ----
            if (m_bFullSeed || !m_bIncremental)
            {
                // 消費者の既読は進めておく（全行を処理するので、ログの内容は使わない）
                for (const EcsConsumerId consumer : m_Consumers)
                {
                    if (consumer != kInvalidEcsConsumer)
                    {
                        m_Scratch.clear();
                        m_World.ReadChanges(consumer, m_Scratch);
                    }
                }
                SeedAllRows(serial, result);
                m_bFullSeed = false;
            }
            else
            {
                for (const EcsConsumerId consumer : m_Consumers)
                {
                    m_Scratch.clear();
                    m_World.ReadChanges(consumer, m_Scratch);
                    for (const EcsChangeEntry &entry : m_Scratch)
                    {
                        AddSeed(entry.Entity, serial, result);
                    }
                }
            }

            // ---- Depth 0 から順に処理する ----
            for (uint32_t depth = 0; depth <= m_MaxDepth && depth < m_Buckets.size(); ++depth)
            {
                if (m_Buckets[depth].empty())
                {
                    continue;
                }
                // 次の Depth のバケツを先に確保する（処理中に m_Buckets を広げて参照を無効にしない）
                if (m_Buckets.size() < static_cast<size_t>(depth) + 2)
                {
                    m_Buckets.resize(static_cast<size_t>(depth) + 2);
                }
                ProcessLevel(depth, serial, result);
            }
            return result;
        }

    private:
        /// 1 行の計算結果（置き場は行ごと。並列の中ではここだけに書く）
        struct RowResult
        {
            EcsWorldTransform World;
            bool bEffectiveActive = false;
            /// ワールドの変換の値が列と違う（書き込みが要る）
            bool bWorldChanged = false;
            /// 子へ伝える（ワールドの変換か EffectiveActive が変わった、または初めて求めた）
            bool bChanged = false;
            bool bUnresolved = false;
        };

        /// 行をきっかけとして最初のバケツに積む（行が無い・Entity の行でない・孤児・積み済みなら何もしない）
        void AddSeed(EcsEntity entity, uint64_t serial, RunResult &result)
        {
            EcsPropagatedTransform *propagated = static_cast<EcsPropagatedTransform *>(m_World.GetColumnValue(entity, m_Propagated));
            if (propagated == nullptr || propagated->Visited == serial || m_World.IsOrphan(entity))
            {
                return;
            }
            const EcsHierarchy *hierarchy = static_cast<const EcsHierarchy *>(m_World.GetColumnValue(entity, m_Hierarchy));
            if (hierarchy == nullptr || hierarchy->Depth >= kMaxDepth)
            {
                return;
            }
            propagated->Visited = serial;
            PushToBucket(hierarchy->Depth, entity);
            ++result.Seeds;
        }

        void SeedAllRows(uint64_t serial, RunResult &result)
        {
            EcsQuery query;
            query.All.push_back(m_Propagated);
            query.All.push_back(m_Local);
            query.All.push_back(m_Hierarchy);
            query.All.push_back(m_Flags);
            m_Archetypes.clear();
            m_World.MatchArchetypes(query, m_Archetypes);
            for (const EcsArchetypeId archetype : m_Archetypes)
            {
                for (const EcsChunk *chunk : m_World.GetArchetypeChunks(archetype))
                {
                    const EcsEntity *entities = chunk->GetEntities();
                    for (uint32_t row = 0; row < chunk->Count; ++row)
                    {
                        AddSeed(entities[row], serial, result);
                    }
                }
            }
        }

        void PushToBucket(uint32_t depth, EcsEntity entity)
        {
            if (m_Buckets.size() <= depth)
            {
                m_Buckets.resize(static_cast<size_t>(depth) + 1);
            }
            m_Buckets[depth].push_back(entity);
            if (depth > m_MaxDepth)
            {
                m_MaxDepth = depth;
            }
        }

        /// 1 行の計算: 親の（すでに求めた）値と自分の局所の変換・flags から結果を作る。列は読むだけ（並列の中で呼ぶ）
        void ComputeRow(EcsEntity entity, RowResult &out) const
        {
            const EcsWorld &world = m_World;
            const EcsHierarchy *hierarchy = static_cast<const EcsHierarchy *>(world.GetColumnValue(entity, m_Hierarchy));
            const EcsLocalTransform *local = static_cast<const EcsLocalTransform *>(world.GetColumnValue(entity, m_Local));
            const EcsEntityFlags *flags = static_cast<const EcsEntityFlags *>(world.GetColumnValue(entity, m_Flags));
            const EcsPropagatedTransform *self =
                static_cast<const EcsPropagatedTransform *>(world.GetColumnValue(entity, m_Propagated));
            const EcsWorldTransform *current = static_cast<const EcsWorldTransform *>(world.GetColumnValue(entity, m_WorldType));
            if (hierarchy == nullptr || local == nullptr || flags == nullptr || self == nullptr || current == nullptr)
            {
                out.bUnresolved = true;
                return;
            }

            Math::Transform parentWorld = Math::Transform::Identity;
            bool bParentActive = true;
            if (!hierarchy->Parent.IsNull())
            {
                const EcsPropagatedTransform *parent =
                    static_cast<const EcsPropagatedTransform *>(world.GetColumnValue(hierarchy->Parent, m_Propagated));
                const EcsWorldTransform *parentValue =
                    static_cast<const EcsWorldTransform *>(world.GetColumnValue(hierarchy->Parent, m_WorldType));
                if (parent == nullptr || parent->bValid == 0 || parentValue == nullptr)
                {
                    out.bUnresolved = true;
                    return;
                }
                parentWorld = Math::Transform(parentValue->position, parentValue->rotation, parentValue->scale);
                bParentActive = parent->bEffectiveActive != 0;
            }

            const Math::Transform composed =
                EcsComposeTransform(parentWorld, Math::Transform(local->position, local->rotation, local->scale));
            out.World = EcsWorldTransform{composed.position, composed.rotation, composed.scale};
            out.bEffectiveActive = bParentActive && flags->bActive && !flags->bPendingDestroy;
            out.bWorldChanged = !EcsEqualWorldTransform(*current, out.World);
            out.bChanged = self->bValid == 0 || out.bWorldChanged || (self->bEffectiveActive != 0) != out.bEffectiveActive;
        }

        /// 1 つの Depth のバケツを処理する: 計算（並列でもよい）→ 列への書き込みと子の積み込み（GameThread、行の順）
        void ProcessLevel(uint32_t depth, uint64_t serial, RunResult &result)
        {
            // 次の Depth へ積む処理で m_Buckets の要素を足すだけで、広げはしない（Run が先に確保した）
            const Container::VariableArray<EcsEntity> &bucket = m_Buckets[depth];
            const size_t count = bucket.size();
            m_Results.resize(count);
            ++result.Levels;

            if (m_bParallel && count >= m_ParallelMinCount && count > 0)
            {
                // 並列の中は列を読むだけ。未登録の列の型を引かないよう、型の番号は構築時に固定してある
                EcsParallelScope parallel;
                NorvesLib::Thread::JobSystem::Get().ParallelFor(count, m_ParallelGrain, [this, &bucket](size_t index) {
                    m_Results[index] = RowResult{};
                    ComputeRow(bucket[index], m_Results[index]);
                });
            }
            else
            {
                for (size_t index = 0; index < count; ++index)
                {
                    m_Results[index] = RowResult{};
                    ComputeRow(bucket[index], m_Results[index]);
                }
            }

            for (size_t index = 0; index < count; ++index)
            {
                const EcsEntity entity = bucket[index];
                const RowResult &row = m_Results[index];
                ++result.Processed;
                if (row.bUnresolved)
                {
                    ++result.Unresolved;
                    continue;
                }
                if (m_bRecordOrder)
                {
                    m_Order.push_back(entity);
                }
                if (row.bWorldChanged)
                {
                    m_World.WriteColumnValue(entity, m_WorldType, &row.World);
                }
                EcsPropagatedTransform *propagated =
                    static_cast<EcsPropagatedTransform *>(m_World.GetColumnValue(entity, m_Propagated));
                propagated->bEffectiveActive = row.bEffectiveActive ? 1u : 0u;
                propagated->bValid = 1;
                m_Applied.push_back(AppliedRow{entity, row.bWorldChanged});
                if (!row.bChanged)
                {
                    continue;
                }
                ++result.Changed;

                // 変わった行の子を次の Depth へ積む（Entity の子だけ。Component の行は連結が別）
                const EcsHierarchy *hierarchy = static_cast<const EcsHierarchy *>(m_World.GetColumnValue(entity, m_Hierarchy));
                for (EcsEntity child = hierarchy->FirstChild; !child.IsNull();)
                {
                    const EcsHierarchy *childHierarchy = static_cast<const EcsHierarchy *>(m_World.GetColumnValue(child, m_Hierarchy));
                    if (childHierarchy == nullptr)
                    {
                        break;
                    }
                    EcsPropagatedTransform *childPropagated =
                        static_cast<EcsPropagatedTransform *>(m_World.GetColumnValue(child, m_Propagated));
                    if (childPropagated != nullptr && childPropagated->Visited != serial && depth + 1 < kMaxDepth)
                    {
                        childPropagated->Visited = serial;
                        PushToBucket(depth + 1, child);
                    }
                    child = childHierarchy->NextSibling;
                }
            }
        }

        EcsWorld &m_World;
        TypeId m_Local = 0;
        TypeId m_Hierarchy = 0;
        TypeId m_Flags = 0;
        TypeId m_Propagated = 0;
        TypeId m_WorldType = 0;
        /// EcsLocalTransform・EcsHierarchy・EcsEntityFlags の消費者
        EcsConsumerId m_Consumers[3] = {kInvalidEcsConsumer, kInvalidEcsConsumer, kInvalidEcsConsumer};
        bool m_bIncremental = false;
        /// 最初の Run は全行をきっかけにする
        bool m_bFullSeed = true;
        bool m_bParallel = false;
        size_t m_ParallelMinCount = 512;
        size_t m_ParallelGrain = 128;
        bool m_bRecordOrder = false;
        uint64_t m_Serial = 0;
        uint32_t m_MaxDepth = 0;
        /// Depth ごとのバケツ（容量は Run をまたいで使い回す）
        Container::VariableArray<Container::VariableArray<EcsEntity>> m_Buckets;
        Container::VariableArray<RowResult> m_Results;
        Container::VariableArray<EcsChangeEntry> m_Scratch;
        Container::VariableArray<EcsArchetypeId> m_Archetypes;
        Container::VariableArray<EcsEntity> m_Order;
        Container::VariableArray<AppliedRow> m_Applied;
    };

}
