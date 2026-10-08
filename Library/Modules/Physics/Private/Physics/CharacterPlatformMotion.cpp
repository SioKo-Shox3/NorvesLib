#include "Physics/CharacterPlatformMotion.h"
#include <cmath>
namespace NorvesLib::Modules::Physics
{
    namespace
    {
        bool Finite(const Math::Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        bool Inverse(const Math::Quaternion& q, Math::Quaternion& inverse)
        {
            const double n = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
            if (!std::isfinite(n) || n <= 0)
                return false;
            inverse = Math::Quaternion(float(-q.x / n), float(-q.y / n), float(-q.z / n), float(q.w / n));
            return std::isfinite(inverse.x) && std::isfinite(inverse.y) && std::isfinite(inverse.z) &&
                   std::isfinite(inverse.w);
        }
        bool Yaw(const Math::Transform& owner, const Math::Transform& local, float& yaw)
        {
            const auto direction = (owner.rotation * local.rotation) * Math::Vector3::UnitZ;
            if (!Finite(direction))
                return false;
            yaw = direction.x * direction.x + direction.z * direction.z > 1e-12f ? std::atan2(direction.x, direction.z)
                                                                                 : 0;
            return std::isfinite(yaw);
        }
    } // namespace
    bool CaptureCharacterAnchor(const Math::Transform& owner, const Math::Transform& local, const Math::Vector3& world,
                                Math::Vector3& anchor, float& yaw)
    {
        Math::Quaternion qi, li;
        if (local.scale != Math::Vector3::One || !Finite(owner.scale) || owner.scale.x == 0 || owner.scale.y == 0 ||
            owner.scale.z == 0 || !Inverse(owner.rotation, qi) || !Inverse(local.rotation, li))
            return false;
        const auto rotated = qi * (world - owner.position);
        const Math::Vector3 ownerLocal(rotated.x / owner.scale.x, rotated.y / owner.scale.y, rotated.z / owner.scale.z);
        const auto value = li * (ownerLocal - local.position);
        float angle;
        if (!Finite(value) || !Yaw(owner, local, angle))
            return false;
        anchor = value;
        yaw = angle;
        return true;
    }
    bool ResolveCharacterAnchor(const Math::Transform& owner, const Math::Transform& local, const Math::Vector3& anchor,
                                Math::Vector3& world, float& yaw)
    {
        Math::Quaternion inverse;
        if (local.scale != Math::Vector3::One || !Finite(owner.scale) || owner.scale.x == 0 || owner.scale.y == 0 ||
            owner.scale.z == 0 || !Inverse(owner.rotation, inverse) || !Inverse(local.rotation, inverse))
            return false;
        const auto value = owner.TransformPoint(local.position + local.rotation * anchor);
        float angle;
        if (!Finite(value) || !Yaw(owner, local, angle))
            return false;
        world = value;
        yaw = angle;
        return true;
    }
} // namespace NorvesLib::Modules::Physics
