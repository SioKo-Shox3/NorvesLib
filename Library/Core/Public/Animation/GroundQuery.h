// 足の接地点を問い合わせる共通の口と物理を使う実装を定義する。
#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Scene/PhysicsQueryTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    class SceneQuery;
}

namespace NorvesLib::Core::Animation
{
    enum class EGroundQueryStatus : uint8_t
    {
        Hit,
        NoHit,
        NotReady,
        WrongThread,
        Unavailable,
        TooSteep
    };

    struct GroundHit
    {
        Math::Vector3 Point;  // 当たりのワールド座標
        Math::Vector3 Normal; // 当たりの法線。上向きの単位長
        float Distance = 0.0f; // originAbove から下へ測った距離
        EGroundQueryStatus Status = EGroundQueryStatus::NoHit;
    };

    struct GroundProbe
    {
        Math::Vector3 OriginAbove;
        float MaxDown = 0.0f;
        GroundHit Result;
    };

    /// 足の接地点を問う口。地形など別の実装も同じ口に差し込める。
    class IGroundQuery
    {
    public:
        virtual ~IGroundQuery() = default;

        /// originAbove から真下へ maxDown まで問い合わせる。戻り値は out.Status と一致する。
        virtual EGroundQueryStatus Query(const Math::Vector3& originAbove, float maxDown, GroundHit& out) const = 0;
        /// 各 probe の Result に個別の問い合わせ結果を書く。
        virtual void QueryBatch(Container::Span<GroundProbe> probes) const = 0;
    };

    struct PhysicsGroundQuerySettings
    {
        Scene::PhysicsCollisionMask LayerMask = Scene::AllPhysicsLayers; // 地面とみなすレイヤー
        float MaxSlopeDeg = 50.0f;                                       // この角度を超えた面は TooSteep
        Scene::BodyHandle IgnoreBodies[4]{};                             // 問い合わせから除く Body
        Scene::ColliderHandle IgnoreColliders[4]{};                     // 問い合わせから除く Collider
    };

    /**
     * @brief 物理スナップショットを使い足の接地点を問い合わせる。
     *
     * SceneQuery は借用し、所有しない。参照先の SceneQuery とそれを持つ Engine / World より長く生かさない。
     * GameThread 専用。作業用配列を使い回すため、同時に呼び出さない。
     * 有効な入力をほかのスレッドから問い合わせると、SceneQuery が拒否して WrongThread を返す。
     * 結果は直近の固定ステップで公開されたスナップショットで、最大 1 固定ステップ古い。
     * 始点が形状の内側などで法線を求められない当たりは Unavailable を返す。
     */
    class PhysicsGroundQuery final : public IGroundQuery
    {
    public:
        PhysicsGroundQuery(const Scene::SceneQuery& query, const PhysicsGroundQuerySettings& settings);
        EGroundQueryStatus Query(const Math::Vector3& originAbove, float maxDown, GroundHit& out) const override;
        void QueryBatch(Container::Span<GroundProbe> probes) const override;
        const PhysicsGroundQuerySettings& GetSettings() const;

    private:
        const Scene::SceneQuery& m_Query;
        PhysicsGroundQuerySettings m_Settings;
        mutable Container::VariableArray<Scene::PhysicsQueryHit> m_Hits;
        mutable Container::VariableArray<Scene::PhysicsQueryDesc> m_Queries;
        mutable Container::VariableArray<Scene::PhysicsQueryBatchResult> m_Results;
    };
} // namespace NorvesLib::Core::Animation
