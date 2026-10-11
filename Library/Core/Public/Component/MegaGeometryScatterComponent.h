#pragma once

/**
 * @file MegaGeometryScatterComponent.h
 * @brief MegaGeometry の散布の Object と軽い実体の行（設計 1.11）
 *
 * - 散布は 1 つの Component（Object）で、個々の実体は Object を持たない軽い行（EcsMegaInstanceColumns）にする。
 *   Entity の数は本数によらない。エディタと保存に見えるのは、メッシュ・種・本数などの引数だけで、個々の値は保存しない。
 * - 入力の番号 i を安定した識別子とする。入力の番号 → 行の表を持ち、UpdateInstances は入力の番号を指す（格納の順とは独立）。
 * - 行は入力の位置の Morton 順に作り、席は連続した範囲の組（塊）から先頭の順に取る。範囲が空間的にまとまる。
 * - 作成・所有者の変換とメッシュの反映・解放は、EcsWorld の仕事の列（EcsMassWork.h）が 1 フレームの行数と時間の予算で
 *   範囲（またはチャンク）ごとに分けて進める。列は World の LateTick の段の系（LateTick.MassWork）が毎フレーム回す。
 *   - 作成: SetInstances は席の塊とチャンクの組を予約するだけで、行は範囲ごとに作る。範囲の全行を作って書いてから、その範囲を公開する。
 *   - 反映: 所有者のワールドの変換・メッシュ・影の変化は版を進め、範囲ごとに全行を書き直してから公開し直す。
 *     書き直すまでの範囲は前の値で描く（公開中の範囲の proxy は、いつも範囲の全行の同じ版の値）。
 *   - 解放: 破棄（World の木から外れる）と入力の与え直しでは、公開中の proxy をすぐ全部取り下げ（持ち主がこの直後にメッシュを
 *     返しても描画が古いメッシュを読まない）、行の削除はチャンクごとに予算で分ける。全行を消したら塊とチャンクの組を返す。
 *     行の削除の後始末は列が持つので、Component が先に消えても続く。
 * - Active と可視は範囲の印で切り替える（印を変えた範囲だけを公開・取り下げる）。
 * - 行は散布ごとのチャンクの組（EcsWorld::AcquireChunkGroup）のアーキタイプに入れ、ほかの散布や行とチャンクを共有しない。
 * - メッシュは表で持ち、入力ごとに表の番号（MeshIndex）を選ぶ。1 つの散布に種類の違うメッシュを混ぜられる。
 * - 検査 MASS_RANGES: 公開中の範囲は全行がそろっている（行が生きていて、全行の proxy が行の値と一致する）、
 *   公開していない範囲・解放した範囲の proxy は無い、を毎フレーム（仕事の無いフレームは 60 回に 1 回）突き合わせる。
 *
 * GameThread の並列の段の外で触る。行の作成は World の木に付いている間だけ行い、付く前に与えた入力は付いたときに作る。
 * SceneView の付け替えは、まだ自動では反映しない（入力を与え直すと反映される）。
 */

#include "Component/Component.h"
#include "Container/Containers.h"
#include "Ecs/EcsMassWork.h"
#include "Ecs/EcsTypes.h"
#include "Math/Quaternion.h"
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/SceneProxy.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Core::Component
{
    /// 散布の 1 個の入力（所有者の Entity からの局所の変換と、メッシュの表の番号）
    struct MegaScatterInstance
    {
        Math::Vector3 Position;
        Math::Quaternion Rotation;
        Math::Vector3 Scale;
        uint32_t MeshIndex = 0;
    };

    class MegaGeometryScatterComponent : public Component
    {
        REFLECTION_CLASS(MegaGeometryScatterComponent, Component)

    public:
        /// 1 つの範囲の行（席）の数
        static constexpr uint32_t kRangeRows = 64;
        static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;
        /// 散布の仕事の列を回す系の名前（World の LateTick の段）
        static constexpr const char *kMassWorkSystemName = "LateTick.MassWork";

        MegaGeometryScatterComponent();
        explicit MegaGeometryScatterComponent(const FieldInitializer *initializer);
        explicit MegaGeometryScatterComponent(const IUnknown *sourceObject);
        virtual ~MegaGeometryScatterComponent();

        void Finalize() override;
        void Enable() override;
        void Disable() override;

        /// メッシュを 1 つにする（全入力が表の 0 番を使う）
        void SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle);
        /// メッシュの表を与え直す。入力の MeshIndex が表の外・無効なメッシュの行は描かない
        void SetMegaMeshHandles(Container::Span<const Rendering::MegaGeometry::MegaMeshHandle> handles);
        Rendering::MegaGeometry::MegaMeshHandle GetMegaMeshHandle() const { return MegaMeshHandleValue.Get(); }
        void SetCastShadow(bool bCastShadow);
        void SetVisible(bool bVisible);
        bool IsVisible() const { return bVisible.Get(); }
        /// 配置の種（保存する引数。値は使う側が決める）
        void SetSeed(uint32_t seed) { Seed = seed; }
        uint32_t GetSeed() const { return Seed.Get(); }

        /**
         * @brief 入力を与え直す。前の行と範囲は解放し（行の削除は予算で分ける）、入力の Morton 順に席の塊を予約する。
         *        行は仕事の列が範囲ごとに作り、範囲の全行を作ってから公開する
         * @return 予約できた（または World の木に付く前で、付いたときに作る）なら true
         */
        bool SetInstances(Container::Span<const MegaScatterInstance> instances);

        /**
         * @brief 入力の番号 first から values の数だけ値を書き換え、書いた行を含む範囲を範囲ごとに公開し直す
         *        （書き直しを待っている範囲は、範囲の全行を今の版で書き直してから公開する）。まだ作っていない行は作るときに使う
         * @return 範囲が入力の外なら false で、何もしない
         */
        bool UpdateInstances(uint32_t first, Container::Span<const MegaScatterInstance> values);

        /// 範囲の Active の印。外した範囲は描かない
        bool SetRangeActive(uint32_t range, bool bActive);
        /// 範囲の可視の印。外した範囲は描かない
        bool SetRangeVisible(uint32_t range, bool bVisible);

        /// 入力の数
        uint32_t GetInstanceCount() const { return static_cast<uint32_t>(m_Instances.size()); }
        /// 作った行の数（作成の途中は入力の数より少ない）
        uint32_t GetRowCount() const { return m_CreatedRows; }
        /// 範囲の数（席の塊を予約した時点で決まる）
        uint32_t GetRangeCount() const { return static_cast<uint32_t>(m_Ranges.size()); }
        /// 公開している範囲の数
        uint32_t GetPublishedRangeCount() const;
        /// 作成か書き直しが残っているか（false なら、全範囲が今の入力・所有者・メッシュの値で公開の状態にある）
        bool HasPendingWork() const;
        /// 入力の番号の行（行が無ければ null）
        Ecs::EcsEntity GetInstanceRow(uint32_t inputIndex) const;
        /// 入力の番号の行が属する範囲（行が無ければ kInvalidIndex）
        uint32_t GetInstanceRange(uint32_t inputIndex) const;
        /// proxy の鍵に使う散布の番号（席を予約したときに決まる）
        uint32_t GetBatchId() const { return m_BatchId; }
        /// 行を入れたチャンクの組（行が無ければ 0）
        uint32_t GetChunkGroup() const { return m_ChunkGroup; }

    protected:
        void OnEcsRowAttached() override;
        void OnEcsRowDetaching() override;

        PROPERTY(Rendering::MegaGeometry::MegaMeshHandle, MegaMeshHandleValue)
        PROPERTY(bool, bCastShadow)
        PROPERTY(bool, bVisible)
        PROPERTY(uint32_t, Seed)
        /// 本数（保存する引数）
        PROPERTY(uint32_t, InstanceCount)

    private:
        /// 散布の範囲（席の表の範囲 1 つと、格納の順で連続した行）
        struct ScatterRange
        {
            uint32_t TableRange = 0;
            uint32_t FirstRow = 0;
            uint32_t RowCount = 0;
            /// 行に書いた値の版（m_ContentVersion と違えば書き直しを待っている）
            uint32_t WrittenVersion = 0;
            bool bCreated = false;
            bool bActive = true;
            bool bVisible = true;
            bool bPublished = false;
        };

        /// 仕事の列の項目（Component の寿命の間だけ列に載る）
        class MassWork final : public Ecs::IEcsMassWork
        {
        public:
            explicit MassWork(MegaGeometryScatterComponent &owner) : m_Owner(owner) {}
            void PollMassWork() override;
            bool HasMassWork() const override;
            void StepMassWork(Ecs::EcsMassBudgetCursor &cursor) override;
            void AuditMassWork(Ecs::EcsMassAudit &audit) const override;

        private:
            MegaGeometryScatterComponent &m_Owner;
        };

        World *ResolveWorld() const;
        bool BeginRows(World &world);
        void ReleaseRows();
        void CreateNextRange(Ecs::EcsMassBudgetCursor &cursor);
        void RewriteNextStaleRange(Ecs::EcsMassBudgetCursor &cursor);
        /// 範囲の全行を今の版で書き直す（公開はしない）
        void RewriteRange(uint32_t range);
        void BumpContentVersion();
        void WriteRow(Ecs::EcsWorld &ecs, uint32_t row);
        bool BuildProxy(const Ecs::EcsWorld &ecs, uint32_t row, Rendering::MegaGeometryProxy &outProxy) const;
        Rendering::MegaGeometry::MegaMeshHandle ResolveMesh(uint32_t meshIndex) const;
        bool ShouldPublish(const ScatterRange &range) const;
        /// 印と状態に合わせて範囲を公開・取り下げる。bRepublish なら公開中の範囲も公開し直す。作っていない範囲は何もしない
        void ReflectRange(uint32_t range, bool bRepublish);
        void ReflectAllRanges(bool bRepublish);
        void Audit(Ecs::EcsMassAudit &audit) const;

        /// 入力（入力の番号の順）
        Container::VariableArray<MegaScatterInstance> m_Instances;
        /// メッシュの表（入力の MeshIndex が指す）
        Container::VariableArray<Rendering::MegaGeometry::MegaMeshHandle> m_MeshHandles;
        /// 行（格納の順 = Morton 順 = 席の順）。作っていない行は null
        Container::VariableArray<Ecs::EcsEntity> m_Rows;
        /// 格納の順 → 入力の番号
        Container::VariableArray<uint32_t> m_RowToInput;
        /// 入力の番号 → 格納の順
        Container::VariableArray<uint32_t> m_InputToRow;
        Container::VariableArray<ScatterRange> m_Ranges;
        /// 行を作る World（行を消すまで持つ）
        World *m_pRowWorld = nullptr;
        /// 席の表の塊
        uint32_t m_Block = 0xFFFFFFFFu;
        uint32_t m_BatchId = 0;
        /// 行のチャンクの組とアーキタイプ（この散布の行だけが入る）
        uint32_t m_ChunkGroup = 0;
        Ecs::EcsArchetypeId m_Archetype = Ecs::kInvalidEcsArchetype;
        /// 作った範囲・行の数（範囲は先頭から順に作る）
        uint32_t m_CreatedRanges = 0;
        uint32_t m_CreatedRows = 0;
        /// 作成に失敗した（残りの範囲は作らない）
        bool m_bCreateFailed = false;
        /// 予約した範囲の外の席を持つ行の数（作成の後にまとめて警告する）
        uint32_t m_RangeMismatches = 0;
        /// 席を予約したときの列の Run の回数（作成にかかったフレームの数をログに出す）
        uint64_t m_CreateStartRun = 0;
        /// 行に書く値の版と、書き直しを待っている作った範囲の数、次に探す範囲
        uint32_t m_ContentVersion = 1;
        uint32_t m_StaleRanges = 0;
        uint32_t m_RewriteCursor = 0;
        /// 行に書いている所有者のワールドの変換（Poll が変化を見つけたら替えて版を進める）
        Math::Transform m_OwnerWorld;
        MassWork m_MassWork{*this};
        /// 仕事の列に載っているか
        bool m_bMassWorkRegistered = false;
    };

} // namespace NorvesLib::Core::Component
