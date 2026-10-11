#pragma once

/**
 * @file MegaGeometryScatterComponent.h
 * @brief MegaGeometry の散布の Object と軽い実体の行（設計 1.11）
 *
 * - 散布は 1 つの Component（Object）で、個々の実体は Object を持たない軽い行（EcsMegaInstanceColumns）にする。
 *   Entity の数は本数によらない。エディタと保存に見えるのは、メッシュ・種・本数などの引数だけで、個々の値は保存しない。
 * - 入力の番号 i を安定した識別子とする。入力の番号 → 行の表を持ち、UpdateInstances は入力の番号を指す（格納の順とは独立）。
 * - 行は入力の位置の Morton 順に作り、席は連続した範囲の組（塊）から先頭の順に取る。範囲が空間的にまとまる。
 * - 公開は範囲ごと。範囲の全行を書き終えてから、その範囲の proxy をまとめて公開し直す。
 * - Active と可視は範囲の印で切り替える（印を変えた範囲だけを公開・取り下げる）。
 * - 破棄（World の木から外れる）で、全行を削除し（席は理由が Destroy の削除で解放される）、proxy を取り下げ、塊を返す。
 *
 * GameThread の並列の段の外で触る。行の作成は World の木に付いている間だけ行い、付く前に与えた入力は付いたときに作る。
 * 所有者の変換の変化と SceneView の付け替えは、まだ自動では反映しない（入力を与え直すと反映される）。
 */

#include "Component/Component.h"
#include "Container/Containers.h"
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
    /// 散布の 1 個の入力（所有者の Entity からの局所の変換）
    struct MegaScatterInstance
    {
        Math::Vector3 Position;
        Math::Quaternion Rotation;
        Math::Vector3 Scale;
    };

    class MegaGeometryScatterComponent : public Component
    {
        REFLECTION_CLASS(MegaGeometryScatterComponent, Component)

    public:
        /// 1 つの範囲の行（席）の数
        static constexpr uint32_t kRangeRows = 64;
        static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

        MegaGeometryScatterComponent();
        explicit MegaGeometryScatterComponent(const FieldInitializer *initializer);
        explicit MegaGeometryScatterComponent(const IUnknown *sourceObject);
        virtual ~MegaGeometryScatterComponent();

        void Finalize() override;
        void Enable() override;
        void Disable() override;

        void SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle);
        Rendering::MegaGeometry::MegaMeshHandle GetMegaMeshHandle() const { return MegaMeshHandleValue.Get(); }
        void SetCastShadow(bool bCastShadow);
        void SetVisible(bool bVisible);
        bool IsVisible() const { return bVisible.Get(); }
        /// 配置の種（保存する引数。値は使う側が決める）
        void SetSeed(uint32_t seed) { Seed = seed; }
        uint32_t GetSeed() const { return Seed.Get(); }

        /**
         * @brief 入力を与え直す。前の行と範囲は全部解放し、入力の Morton 順に行を作り直して範囲ごとに公開する
         * @return 行を作れた（または World の木に付く前で、付いたときに作る）なら true
         */
        bool SetInstances(Container::Span<const MegaScatterInstance> instances);

        /**
         * @brief 入力の番号 first から values の数だけ値を書き換え、書いた行を含む範囲を範囲ごとに公開し直す
         * @return 範囲が入力の外なら false で、何もしない
         */
        bool UpdateInstances(uint32_t first, Container::Span<const MegaScatterInstance> values);

        /// 範囲の Active の印。外した範囲は描かない
        bool SetRangeActive(uint32_t range, bool bActive);
        /// 範囲の可視の印。外した範囲は描かない
        bool SetRangeVisible(uint32_t range, bool bVisible);

        /// 入力の数
        uint32_t GetInstanceCount() const { return static_cast<uint32_t>(m_Instances.size()); }
        /// 作った行の数
        uint32_t GetRowCount() const { return static_cast<uint32_t>(m_Rows.size()); }
        uint32_t GetRangeCount() const { return static_cast<uint32_t>(m_Ranges.size()); }
        /// 公開している範囲の数
        uint32_t GetPublishedRangeCount() const;
        /// 入力の番号の行（行が無ければ null）
        Ecs::EcsEntity GetInstanceRow(uint32_t inputIndex) const;
        /// 入力の番号の行が属する範囲（行が無ければ kInvalidIndex）
        uint32_t GetInstanceRange(uint32_t inputIndex) const;
        /// proxy の鍵に使う散布の番号（行を作ったときに決まる）
        uint32_t GetBatchId() const { return m_BatchId; }

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
            bool bActive = true;
            bool bVisible = true;
            bool bPublished = false;
        };

        World *ResolveWorld() const;
        bool BuildRows(World &world);
        void ReleaseRows();
        void WriteRow(Ecs::EcsWorld &ecs, uint32_t row, const Math::Transform &ownerWorld);
        bool BuildProxy(const Ecs::EcsWorld &ecs, uint32_t row, Rendering::MegaGeometryProxy &outProxy) const;
        bool ShouldPublish(const ScatterRange &range) const;
        /// 印と状態に合わせて範囲を公開・取り下げる。bRepublish なら公開中の範囲も公開し直す
        void ReflectRange(uint32_t range, bool bRepublish);
        void ReflectAllRanges(bool bRepublish);

        /// 入力（入力の番号の順）
        Container::VariableArray<MegaScatterInstance> m_Instances;
        /// 行（格納の順 = Morton 順 = 席の順）
        Container::VariableArray<Ecs::EcsEntity> m_Rows;
        /// 格納の順 → 入力の番号
        Container::VariableArray<uint32_t> m_RowToInput;
        /// 入力の番号 → 格納の順
        Container::VariableArray<uint32_t> m_InputToRow;
        Container::VariableArray<ScatterRange> m_Ranges;
        /// 行を作った World（行を消すまで持つ）
        World *m_pRowWorld = nullptr;
        /// 席の表の塊
        uint32_t m_Block = 0xFFFFFFFFu;
        uint32_t m_BatchId = 0;
    };

} // namespace NorvesLib::Core::Component
