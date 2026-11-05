#pragma once

#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Container/PointerTypes.h"
#include "Math/GeometryTypes.h"
#include "Scene/PhysicsQueryTypes.h"
#include "Physics/StaticAabbTree.h"

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;
}

namespace NorvesLib::Modules::Physics
{
    struct PhysicsStaticTreeSnapshot;

    enum class EPhysicsProxyShape : uint8_t
    {
        Sphere,
        Box,
        Capsule,
        HeightField,
    };

    // 組の生成方針。Unspecified は従来どおりすべての組を対象にする。
    enum class EPhysicsProxyMobility : uint8_t
    {
        Unspecified,
        Static,
        Kinematic,
        Dynamic,
        Trigger,
    };

    struct PhysicsShapeProxy
    {
        Core::Scene::ColliderHandle Collider;
        Core::Scene::BodyHandle Body;
        Core::EntityHandle Entity;
        bool bHasEntity = false;
        EPhysicsProxyShape Shape = EPhysicsProxyShape::Sphere;
        Math::Sphere Sphere;
        Math::OBB Box;
        Math::Capsule Capsule;
        // 読み取り専用の格子高さ場。
        Core::Container::TSharedPtr<const Core::Rendering::Terrain::TerrainHeightField> HeightField;
        // 高さ場の原点の世界座標。世界座標への変換はこの値を足す平行移動だけ。
        Math::Vector3 HeightFieldOrigin;
        Math::AABB Bounds;
        Core::Scene::PhysicsCollisionMask Layer = Core::Scene::DefaultPhysicsLayer;
        Core::Scene::PhysicsCollisionMask Mask = Core::Scene::AllPhysicsLayers;
        uint64_t UserData = 0;
        bool bTrigger = false;
        // 球・箱・カプセルの表面番号。高さ場は当たり位置で層を照会する。
        uint8_t SurfaceId = Core::Scene::kInvalidPhysicsSurfaceId;
        // PhysicsModule だけが設定する。既定値では従来どおりすべての組を対象にする。
        EPhysicsProxyMobility Mobility = EPhysicsProxyMobility::Unspecified;
        // PhysicsModule だけが設定する。有効な Dynamic / Kinematic の Body を持たないコライダー。
        // 既定では木に入れない。
        bool bStatic = false;
    };

    struct PhysicsCandidatePair
    {
        Core::Scene::ColliderHandle First;
        Core::Scene::ColliderHandle Second;
        // GetProxies() の添字。ハンドルと併せて検証して使う。
        uint32_t FirstProxyIndex = UINT32_MAX;
        uint32_t SecondProxyIndex = UINT32_MAX;
    };

    struct PhysicsBroadphaseStats
    {
        // 最後の SetProxies で生成した候補の組数。
        uint64_t CandidatePairCount = 0;
        // 所有者が記録したナローフェーズの接触判定回数。
        uint64_t NarrowPhaseTestCount = 0;
        // 添字による検索回数と FindProxy の二分探索でハンドルを比較した回数。
        uint64_t ProxyLookupCount = 0;
        // 最後の SetProxies で静的木を再構築した回数。最大 1。
        uint64_t TreeRebuildCount = 0;
        // インスタンスのクエリが走査対象にしたプロキシ数の累計。
        uint64_t VisitedNodeCount = 0;
    };

    class PhysicsBroadphase
    {
    public:
        void SetProxies(Core::Container::VariableArray<PhysicsShapeProxy> proxies);
        // 次の SetProxies で署名が同じなら、共有した木を作り直さずに使う。
        void ShareStaticTreeFrom(const PhysicsBroadphase& source);
        const StaticAabbTree* GetStaticTree() const;

        const Core::Container::VariableArray<PhysicsShapeProxy>& GetProxies() const;
        const Core::Container::VariableArray<PhysicsCandidatePair>& GetCandidatePairs() const;
        const PhysicsBroadphaseStats& GetStats() const;
        const PhysicsShapeProxy* FindProxy(Core::Scene::ColliderHandle handle) const;
        const PhysicsShapeProxy* GetPairProxy(uint32_t proxyIndex, Core::Scene::ColliderHandle expected) const;
        void RecordNarrowPhaseTest() const;

        bool Raycast(const Math::Ray& ray, float maxDistance, Core::Scene::PhysicsRaycastHit& outHit) const;
        void OverlapSphere(
            const Math::Sphere& sphere,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const;
        void OverlapBox(
            const Math::OBB& box,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const;
        void OverlapCapsule(
            const Math::Capsule& capsule,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const;

        Core::Scene::EPhysicsSceneQueryResult RaycastQuery(const Math::Ray& ray, float maxDistance,
            Core::Scene::PhysicsRaycastHit& outHit) const;
        // 旧直接Overlapと同じく成功時だけ追記する。Module側は呼出し前にclearする。
        Core::Scene::EPhysicsSceneQueryResult AppendOverlapQuery(const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const;
        // 旧無filter検索を保つ。Layer=0も対象。新APIのLayerMask/trigger/ignore判定とは区別する。
        static Core::Scene::EPhysicsSceneQueryResult RaycastOverProxies(Core::Container::Span<const PhysicsShapeProxy> proxies,
            const Math::Ray& ray, float maxDistance, Core::Scene::PhysicsRaycastHit& outHit);
        // scratch/outHitsはmin(MaxHits,proxy数)以上。全領域は非alias。失敗時outHits/countは初期化。
        static Core::Scene::EPhysicsSceneQueryResult OverlapOverProxies(Core::Container::Span<const PhysicsShapeProxy> proxies,
            const Core::Scene::PhysicsQueryDesc& query, Core::Container::Span<Core::Scene::PhysicsQueryHit> scratch,
            Core::Container::Span<Core::Scene::PhysicsOverlapHit> outHits, size_t& outHitCount);

        Core::Scene::EPhysicsSceneQueryResult ExecuteQuery(const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::VariableArray<Core::Scene::PhysicsQueryHit>& outHits) const;
        // 出力領域はmin(MaxHits, proxy数)個必要（Closestは最大1個）。失敗時は全出力を初期化する。
        // proxies/query/outHits/outHitCountの格納領域は互いに重ならないこと。
        static Core::Scene::EPhysicsSceneQueryResult ExecuteQueryOverProxies(
            Core::Container::Span<const PhysicsShapeProxy> proxies, const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::Span<Core::Scene::PhysicsQueryHit> outHits, size_t& outHitCount);
        // 同期借用。callbackはSuccess/NoHitで継続、他の結果で中断する。
        // precheckはbounds除外前に呼び、NoHitなら除外する。input/context/proxyを保持しないこと。
        using ProxyVisitCallback = Core::Scene::EPhysicsSceneQueryResult (*)(const PhysicsShapeProxy&, void*);
        static Core::Scene::EPhysicsSceneQueryResult VisitProxiesInAabb(
            Core::Container::Span<const PhysicsShapeProxy> proxies, const Math::AABB& bounds,
            ProxyVisitCallback visitor, void* context, ProxyVisitCallback precheck = nullptr);
        static Core::Scene::EPhysicsSceneQueryResult VisitProxiesAlongRay(
            Core::Container::Span<const PhysicsShapeProxy> proxies, const Math::Ray& ray, float maxDistance,
            ProxyVisitCallback visitor, void* context, ProxyVisitCallback precheck = nullptr);
        static bool IsValidQuery(const Core::Scene::PhysicsQueryDesc& query);
        // 値型の1個のproxyを検査する。Success以外ではoutHitを初期状態へ戻す。
        // 相対尺度の6乗がFLT_MAX/4096を超える入力は旧幾何のoverflow防止のためInvalidArgument。
        static Core::Scene::EPhysicsSceneQueryResult QueryProxy(const PhysicsShapeProxy& proxy,
            const Core::Scene::PhysicsQueryDesc& query, Core::Scene::PhysicsQueryHit& outHit);

        static Math::AABB CalculateBounds(const PhysicsShapeProxy& proxy);
        static bool ComputeContact(
            const PhysicsShapeProxy& first,
            const PhysicsShapeProxy& second,
            Math::GeometryContact& outContact);

    private:
        void BuildCandidatePairs();

        Core::Container::VariableArray<PhysicsShapeProxy> m_Proxies;
        Core::Container::VariableArray<PhysicsCandidatePair> m_CandidatePairs;
        Core::Container::TSharedPtr<const PhysicsStaticTreeSnapshot> m_StaticTree;
        // 木の項目から、今回の m_Proxies の添字への対応。
        Core::Container::VariableArray<uint32_t> m_TreeProxyIndices;
        // 木に入れなかった全プロキシの添字。ハンドル順を保つ。
        Core::Container::VariableArray<uint32_t> m_NonTreeProxyIndices;
        // 所有者のスレッドだけが検索・接触判定を呼び出すため、通常の整数で保持する。
        mutable PhysicsBroadphaseStats m_Stats;
    };
} // namespace NorvesLib::Modules::Physics
