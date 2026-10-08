#pragma once
#include "Physics/PhysicsBroadphase.h"
namespace NorvesLib::Modules::Physics
{
    bool IsValidTerrainProxy(const PhysicsShapeProxy&);
    Math::AABB TerrainBounds(const PhysicsShapeProxy&);
    bool RaycastTerrain(const PhysicsShapeProxy&, const Math::Ray&, float maximum, float& distance,
                        Math::Vector3& normal);
    bool TerrainContact(const Math::Capsule&, const PhysicsShapeProxy&, Math::GeometryContact&);
    bool TerrainContact(const Math::Sphere&, const PhysicsShapeProxy&, Math::GeometryContact&);
    bool TerrainContact(const Math::OBB&, const PhysicsShapeProxy&, Math::GeometryContact&);
    Math::GeometrySweepHit SweepTerrain(const Math::Capsule&, const PhysicsShapeProxy&, const Math::Vector3& direction,
                                        float maximum, const Math::GeometrySweepSettings& settings);
} // namespace NorvesLib::Modules::Physics
