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

        Core::Scene::EPhysicsSceneQueryResult ExecuteQuery(const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::VariableArray<Core::Scene::PhysicsQueryHit>& outHits) const;
        // 出力領域はmin(MaxHits, proxy数)個必要（Closestは最大1個）。失敗時は全出力を初期化する。
        // proxies/query/outHits/outHitCountの格納領域は互いに重ならないこと。
        static Core::Scene::EPhysicsSceneQueryResult ExecuteQueryOverProxies(
            Core::Container::Span<const PhysicsShapeProxy> proxies, const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::Span<Core::Scene::PhysicsQueryHit> outHits, size_t& outHitCount);
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
