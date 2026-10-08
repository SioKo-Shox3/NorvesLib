#pragma once

#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Math/GeometryTypes.h"
#include "Scene/PhysicsQueryTypes.h"

namespace NorvesLib::Modules::Physics
{
    enum class EPhysicsProxyShape : uint8_t
    {
        Sphere,
        Box,
        Capsule,
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
        Math::AABB Bounds;
        Core::Scene::PhysicsCollisionMask Layer = Core::Scene::DefaultPhysicsLayer;
        Core::Scene::PhysicsCollisionMask Mask = Core::Scene::AllPhysicsLayers;
        uint64_t UserData = 0;
        bool bTrigger = false;
    };

    struct PhysicsCandidatePair
    {
        Core::Scene::ColliderHandle First;
        Core::Scene::ColliderHandle Second;
    };

    class PhysicsBroadphase
    {
    public:
        void SetProxies(Core::Container::VariableArray<PhysicsShapeProxy> proxies);

        const Core::Container::VariableArray<PhysicsShapeProxy>& GetProxies() const;
        const Core::Container::VariableArray<PhysicsCandidatePair>& GetCandidatePairs() const;

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
    };
} // namespace NorvesLib::Modules::Physics
