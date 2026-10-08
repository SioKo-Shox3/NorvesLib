#pragma once
#include "Math/Transform.h"
#include "Physics/PhysicsBroadphase.h"
namespace NorvesLib::Modules::Physics
{
    // 局所scaleは1。成功時だけ正規化・符号を揃えた姿勢を返す。
    bool PrepareColliderLocalPose(const Math::Transform& input, Math::Transform& out);
    // metadata/handleは保持し、形状だけ更新する。失敗時はoutを変更しない。
    bool BuildColliderWorldShape(EPhysicsProxyShape shape, float radius, float halfHeight,
                                 const Math::Vector3& halfExtents, const Math::Transform& owner,
                                 const Math::Transform& local, PhysicsShapeProxy& out);
} // namespace NorvesLib::Modules::Physics
