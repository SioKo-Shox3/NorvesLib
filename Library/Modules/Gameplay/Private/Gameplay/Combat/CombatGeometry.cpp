#include "Gameplay/Combat/CombatGeometry.h"

#include "Math/GeometryIntersection.h"
#include "Math/QuaternionUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::Gameplay::Combat
{
    namespace
    {
        using Math::Vector3;

        bool IsFinite(const Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValidRotation(const Math::Quaternion& rotation)
        {
            if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
                !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
            {
                return false;
            }

            // 有限な float の二乗が float の範囲を超えても正しく判定する。
            const double lengthSquared = double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                         double(rotation.z) * rotation.z + double(rotation.w) * rotation.w;
            return lengthSquared > double(Math::Constants::EPSILON);
        }

        Math::Quaternion NormalizeRotation(const Math::Quaternion& rotation)
        {
            const float scale = std::max({std::fabs(rotation.x), std::fabs(rotation.y),
                                          std::fabs(rotation.z), std::fabs(rotation.w)});
            if (!(scale > 0.0f) || !std::isfinite(scale))
            {
                return Math::Quaternion();
            }

            // 最大成分で縮めてから正規化し、長さの計算のオーバーフローを避ける。
            return Math::QuaternionUtils::Normalize(Math::Quaternion(rotation.x / scale, rotation.y / scale,
                                                                    rotation.z / scale, rotation.w / scale));
        }

        double Length(const Vector3& value)
        {
            return std::hypot(double(value.x), double(value.y), double(value.z));
        }

        double Reach(const CombatShape& shape)
        {
            switch (shape.Type)
            {
            case ECombatShapeType::Sphere:
                return Length(shape.Center) + double(shape.Radius);
            case ECombatShapeType::Capsule:
                return std::max(Length(shape.PointA), Length(shape.PointB)) + double(shape.Radius);
            case ECombatShapeType::Box:
                return Length(shape.Center) + Length(shape.HalfExtents);
            }
            return 0.0;
        }

        float ShapeRadius(const CombatShape& shape)
        {
            if (shape.Type == ECombatShapeType::Box)
            {
                return std::min({shape.HalfExtents.x, shape.HalfExtents.y, shape.HalfExtents.z});
            }
            return shape.Radius;
        }

        double PointMotion(const CombatSweepVolume& volume, const Vector3& point,
                           const Math::Quaternion& previousRotation, const Math::Quaternion& currentRotation)
        {
            const Vector3 previousOffset = previousRotation * point;
            const Vector3 currentOffset = currentRotation * point;
            return std::hypot((double(volume.Current.Position.x) - volume.Previous.Position.x) +
                                  (double(currentOffset.x) - previousOffset.x),
                              (double(volume.Current.Position.y) - volume.Previous.Position.y) +
                                  (double(currentOffset.y) - previousOffset.y),
                              (double(volume.Current.Position.z) - volume.Previous.Position.z) +
                                  (double(currentOffset.z) - previousOffset.z));
        }

        double VolumeMotion(const CombatSweepVolume& volume)
        {
            const Math::Quaternion previousRotation = NormalizeRotation(volume.Previous.Rotation);
            const Math::Quaternion currentRotation = NormalizeRotation(volume.Current.Rotation);
            double pointMotion;
            if (volume.Shape.Type == ECombatShapeType::Capsule)
            {
                pointMotion = std::max(PointMotion(volume, volume.Shape.PointA, previousRotation, currentRotation),
                                       PointMotion(volume, volume.Shape.PointB, previousRotation, currentRotation));
            }
            else
            {
                pointMotion = PointMotion(volume, volume.Shape.Center, previousRotation, currentRotation);
            }

            // 同じ回転は内積の丸めで微小な回転と判定されないようにする。
            const bool bSameRotation = previousRotation == currentRotation ||
                (previousRotation.x == -currentRotation.x && previousRotation.y == -currentRotation.y &&
                 previousRotation.z == -currentRotation.z && previousRotation.w == -currentRotation.w);
            // Angle はラジアンを返すので、到達距離を掛けると弧長になる。
            const double angle = bSameRotation ? 0.0 : Math::QuaternionUtils::Angle(previousRotation, currentRotation);
            return std::max(pointMotion, angle * Reach(volume.Shape));
        }

        float OutwardBound(double value, bool bLower)
        {
            const float largest = std::numeric_limits<float>::max();
            const float infinity = std::numeric_limits<float>::infinity();
            if (value > double(largest))
            {
                return bLower ? largest : infinity;
            }
            if (value < -double(largest))
            {
                return bLower ? -infinity : -largest;
            }
            return std::nextafter(static_cast<float>(value), bLower ? -infinity : infinity);
        }

        Math::AABB SweptBounds(const CombatSweepVolume& volume)
        {
            // 回転と座標の丸めを含むよう到達距離と各端をわずかに外へ広げる。
            const double reach = Reach(volume.Shape) * (1.0 + 16.0 * std::numeric_limits<float>::epsilon());
            const Vector3& previous = volume.Previous.Position;
            const Vector3& current = volume.Current.Position;
            return Math::AABB(
                Vector3(OutwardBound(std::min(double(previous.x), double(current.x)) - reach, true),
                        OutwardBound(std::min(double(previous.y), double(current.y)) - reach, true),
                        OutwardBound(std::min(double(previous.z), double(current.z)) - reach, true)),
                Vector3(OutwardBound(std::max(double(previous.x), double(current.x)) + reach, false),
                        OutwardBound(std::max(double(previous.y), double(current.y)) + reach, false),
                        OutwardBound(std::max(double(previous.z), double(current.z)) + reach, false)));
        }

        struct WorldCombatShape
        {
            Math::Sphere Sphere;
            Math::Capsule Capsule;
            Math::OBB Box;
        };

        WorldCombatShape MakeWorldShape(const CombatSweepVolume& volume, float s)
        {
            const CombatPose pose = InterpolateCombatPose(volume.Previous, volume.Current, s);
            const CombatShape& shape = volume.Shape;
            WorldCombatShape world;
            switch (shape.Type)
            {
            case ECombatShapeType::Sphere:
                world.Sphere = Math::Sphere(pose.Position + pose.Rotation * shape.Center, shape.Radius);
                break;
            case ECombatShapeType::Capsule:
                world.Capsule = Math::Capsule(pose.Position + pose.Rotation * shape.PointA,
                                              pose.Position + pose.Rotation * shape.PointB, shape.Radius);
                break;
            case ECombatShapeType::Box:
                world.Box = Math::OBB(pose.Position + pose.Rotation * shape.Center, shape.HalfExtents,
                                      (pose.Rotation * Vector3::UnitX).Normalized(),
                                      (pose.Rotation * Vector3::UnitY).Normalized(),
                                      (pose.Rotation * Vector3::UnitZ).Normalized());
                break;
            }
            return world;
        }
    } // namespace

    CombatPose InterpolateCombatPose(const CombatPose& t0, const CombatPose& t1, float s)
    {
        CombatPose result;
        result.Position = Vector3(std::lerp(t0.Position.x, t1.Position.x, s),
                                  std::lerp(t0.Position.y, t1.Position.y, s),
                                  std::lerp(t0.Position.z, t1.Position.z, s));
        result.Rotation = NormalizeRotation(Math::QuaternionUtils::Slerp(
            NormalizeRotation(t0.Rotation), NormalizeRotation(t1.Rotation), s));
        return result;
    }

    bool IsValidCombatVolume(const CombatSweepVolume& volume)
    {
        const CombatShape& shape = volume.Shape;
        if (!IsFinite(volume.Previous.Position) || !IsFinite(volume.Current.Position) ||
            !IsValidRotation(volume.Previous.Rotation) || !IsValidRotation(volume.Current.Rotation) ||
            !IsFinite(shape.Center) || !IsFinite(shape.PointA) || !IsFinite(shape.PointB) ||
            !std::isfinite(shape.Radius) || !IsFinite(shape.HalfExtents))
        {
            return false;
        }

        switch (shape.Type)
        {
        case ECombatShapeType::Sphere:
        case ECombatShapeType::Capsule:
            return shape.Radius > 0.0f;
        case ECombatShapeType::Box:
            return shape.HalfExtents.x > 0.0f && shape.HalfExtents.y > 0.0f && shape.HalfExtents.z > 0.0f;
        }
        return false;
    }

    bool IsValidCombatSettings(const CombatSweepSettings& settings)
    {
        return settings.MaxSubsteps != 0 && std::isfinite(settings.MinRadius) && settings.MinRadius > 0.0f;
    }

    uint32_t ComputeCombatSubstepCount(const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox,
                                       const CombatSweepSettings& settings)
    {
        if (!IsValidCombatVolume(hitbox) || !IsValidCombatVolume(hurtbox) || !IsValidCombatSettings(settings))
        {
            return 0;
        }

        const double motion = VolumeMotion(hitbox) + VolumeMotion(hurtbox);
        const double radius = std::max(double(std::min(ShapeRadius(hitbox.Shape), ShapeRadius(hurtbox.Shape))),
                                       double(settings.MinRadius));
        const double required = std::ceil(motion / (0.5 * radius));
        if (!std::isfinite(required) || required >= double(settings.MaxSubsteps))
        {
            return settings.MaxSubsteps;
        }
        if (required <= 1.0)
        {
            return 1;
        }
        return static_cast<uint32_t>(required);
    }

    bool ComputeCombatContactAt(const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox, float s,
                                CombatSweepContact& outContact)
    {
        if (!IsValidCombatVolume(hitbox) || !IsValidCombatVolume(hurtbox) ||
            !std::isfinite(s) || s < 0.0f || s > 1.0f)
        {
            return false;
        }

        const WorldCombatShape a = MakeWorldShape(hitbox, s);
        const WorldCombatShape b = MakeWorldShape(hurtbox, s);
        Math::GeometryContact geometryContact;
        bool bHit = false;
        bool bSwapped = false;

        switch (hitbox.Shape.Type)
        {
        case ECombatShapeType::Sphere:
            switch (hurtbox.Shape.Type)
            {
            case ECombatShapeType::Sphere:
                bHit = Math::ComputeContact(a.Sphere, b.Sphere, geometryContact);
                break;
            case ECombatShapeType::Capsule:
                bHit = Math::ComputeContact(b.Capsule, a.Sphere, geometryContact);
                bSwapped = true;
                break;
            case ECombatShapeType::Box:
                bHit = Math::ComputeContact(a.Sphere, b.Box, geometryContact);
                break;
            }
            break;
        case ECombatShapeType::Capsule:
            switch (hurtbox.Shape.Type)
            {
            case ECombatShapeType::Sphere:
                bHit = Math::ComputeContact(a.Capsule, b.Sphere, geometryContact);
                break;
            case ECombatShapeType::Capsule:
                bHit = Math::ComputeContact(a.Capsule, b.Capsule, geometryContact);
                break;
            case ECombatShapeType::Box:
                bHit = Math::ComputeContact(a.Capsule, b.Box, geometryContact);
                break;
            }
            break;
        case ECombatShapeType::Box:
            switch (hurtbox.Shape.Type)
            {
            case ECombatShapeType::Sphere:
                bHit = Math::ComputeContact(b.Sphere, a.Box, geometryContact);
                bSwapped = true;
                break;
            case ECombatShapeType::Capsule:
                bHit = Math::ComputeContact(b.Capsule, a.Box, geometryContact);
                bSwapped = true;
                break;
            case ECombatShapeType::Box:
                bHit = Math::ComputeContact(a.Box, b.Box, geometryContact);
                break;
            }
            break;
        }

        if (!bHit)
        {
            return false;
        }

        CombatSweepContact result;
        result.Point = geometryContact.Point;
        result.Normal = geometryContact.Normal * (bSwapped ? 1.0f : -1.0f);
        result.Depth = geometryContact.Depth;
        result.S = s;
        outContact = result;
        return true;
    }

    ECombatSweepResult SweepCombatVolumes(const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox,
                                         const CombatSweepSettings& settings, CombatSweepContact& outContact)
    {
        if (!IsValidCombatVolume(hitbox) || !IsValidCombatVolume(hurtbox) || !IsValidCombatSettings(settings))
        {
            return ECombatSweepResult::InvalidInput;
        }

        if (!Math::AABBIntersectsAABB(SweptBounds(hitbox), SweptBounds(hurtbox)))
        {
            return ECombatSweepResult::Miss;
        }

        const uint32_t substeps = ComputeCombatSubstepCount(hitbox, hurtbox, settings);
        for (uint64_t i = 0; i <= uint64_t(substeps); ++i)
        {
            const float s = static_cast<float>(double(i) / double(substeps));
            CombatSweepContact contact;
            if (!ComputeCombatContactAt(hitbox, hurtbox, s, contact))
            {
                continue;
            }

            if (i > 0)
            {
                float low = static_cast<float>(double(i - 1) / double(substeps));
                float high = s;
                for (uint32_t iteration = 0; iteration < settings.RefineIterations; ++iteration)
                {
                    const float mid = (low + high) * 0.5f;
                    if (mid <= low || mid >= high)
                    {
                        break;
                    }
                    CombatSweepContact refined;
                    if (ComputeCombatContactAt(hitbox, hurtbox, mid, refined))
                    {
                        high = mid;
                        contact = refined;
                    }
                    else
                    {
                        low = mid;
                    }
                }
            }

            contact.SubstepCount = substeps;
            outContact = contact;
            return ECombatSweepResult::Hit;
        }
        return ECombatSweepResult::Miss;
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
