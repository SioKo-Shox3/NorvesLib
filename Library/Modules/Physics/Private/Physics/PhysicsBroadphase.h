#pragma once

#include "Container/Containers.h"
#include "Math/GeometryTypes.h"
#include "Scene/SceneQuery.h"

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
