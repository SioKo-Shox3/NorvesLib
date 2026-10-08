#include "Physics/ColliderShapeTransform.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Modules::Physics
{
    namespace
    {
        using Math::Vector3;
        bool Finite(const Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
        double NormSquared(const Math::Quaternion& q)
        {
            return double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
        }
        float RoundUp(double value)
        {
            float result = static_cast<float>(value);
            if (double(result) < value)
                result = std::nextafter(result, std::numeric_limits<float>::infinity());
            return result;
        }
    } // namespace
    bool PrepareColliderLocalPose(const Math::Transform& input, Math::Transform& out)
    {
        const double norm = NormSquared(input.rotation);
        if (!Finite(input.position) || input.scale != Vector3::One || !std::isfinite(norm) ||
            std::fabs(norm - 1) > 1e-4)
            return false;
        Math::Transform result = input;
        const float inverse = static_cast<float>(1 / std::sqrt(norm));
        result.rotation = Math::Quaternion(input.rotation.x * inverse, input.rotation.y * inverse,
                                           input.rotation.z * inverse, input.rotation.w * inverse);
        if (result.rotation.w < 0 ||
            (result.rotation.w == 0 &&
             (result.rotation.x < 0 || (result.rotation.x == 0 &&
                                        (result.rotation.y < 0 || (result.rotation.y == 0 && result.rotation.z < 0))))))
            result.rotation =
                Math::Quaternion(-result.rotation.x, -result.rotation.y, -result.rotation.z, -result.rotation.w);
        out = result;
        return true;
    }
    bool BuildColliderWorldShape(EPhysicsProxyShape shape, float radius, float halfHeight, const Vector3& halfExtents,
                                 const Math::Transform& owner, const Math::Transform& local, PhysicsShapeProxy& out)
    {
        Math::Transform prepared;
        if (!PrepareColliderLocalPose(local, prepared))
            return false;
        const double ownerNorm = NormSquared(owner.rotation);
        if (!Finite(owner.position) || !Finite(owner.scale) || !std::isfinite(ownerNorm) || ownerNorm <= 0)
            return false;
        const auto& q = prepared.rotation;
        const bool unrotated = q.x == 0 && q.y == 0 && q.z == 0;
        const bool unshifted = prepared.position == Vector3::Zero;
        const Vector3 center = owner.TransformPoint(prepared.position);
        const float sx = std::fabs(owner.scale.x), sy = std::fabs(owner.scale.y), sz = std::fabs(owner.scale.z);
        PhysicsShapeProxy result = out;
        result.Shape = shape;
        if (shape == EPhysicsProxyShape::Sphere)
        {
            // 球の局所回転は無関係。既存のscale規則を変えず、中心だけ移す。
            result.Sphere = Math::Sphere(center, radius * std::fmaxf(sx, std::fmaxf(sy, sz)));
        }
        else if (shape == EPhysicsProxyShape::Capsule)
        {
            if (unrotated)
            {
                // 旧形状と平行移動だけの場合は半径のXZ規則を保つ。
                const Vector3 a(0, -halfHeight, 0), b(0, halfHeight, 0);
                result.Capsule = Math::Capsule(owner.TransformPoint(unshifted ? a : prepared.position + a),
                                               owner.TransformPoint(unshifted ? b : prepared.position + b),
                                               radius * std::fmaxf(sx, sz));
            }
            else
            {
                const Vector3 halfAxis = owner.TransformVector(q * Vector3(0, halfHeight, 0));
                // 保存済みfloat四元数の単位長からのずれと演算丸めを半径へ含める。
                const double rotationMargin =
                    std::max(1.0, NormSquared(q)) * (1.0 + 8.0 * std::numeric_limits<float>::epsilon());
                const float boundRadius = RoundUp(double(radius) * std::max({sx, sy, sz}) * ownerNorm * rotationMargin);
                result.Capsule = Math::Capsule(center - halfAxis, center + halfAxis, boundRadius);
            }
        }
        else if (shape == EPhysicsProxyShape::Box)
        {
            if (unrotated)
            {
                result.Box = Math::OBB(center, {halfExtents.x * sx, halfExtents.y * sy, halfExtents.z * sz},
                                       (owner.rotation * Vector3::UnitX).Normalized(),
                                       (owner.rotation * Vector3::UnitY).Normalized(),
                                       (owner.rotation * Vector3::UnitZ).Normalized());
            }
            else
            {
                // 非一様scaleのshearを捨てず、直交basisへのsupport幅で箱を包む。
                // half-edgeは並進を介さず求め、遠方の中心との差分による桁落ちを避ける。
                const double ownerLength = std::sqrt(ownerNorm);
                const Math::Quaternion ownerUnit(static_cast<float>(owner.rotation.x / ownerLength),
                                                 static_cast<float>(owner.rotation.y / ownerLength),
                                                 static_cast<float>(owner.rotation.z / ownerLength),
                                                 static_cast<float>(owner.rotation.w / ownerLength));
                const auto basisRotation = ownerUnit * q;
                const Vector3 basis[] = {(basisRotation * Vector3::UnitX).Normalized(),
                                         (basisRotation * Vector3::UnitY).Normalized(),
                                         (basisRotation * Vector3::UnitZ).Normalized()};
                const Vector3 edges[] = {owner.TransformVector(q * Vector3(halfExtents.x, 0, 0)),
                                         owner.TransformVector(q * Vector3(0, halfExtents.y, 0)),
                                         owner.TransformVector(q * Vector3(0, 0, halfExtents.z))};
                float extents[3]{};
                for (unsigned i = 0; i < 3; ++i)
                {
                    double support = 0;
                    for (const auto& edge : edges)
                        support += std::fabs(double(basis[i].x) * edge.x + double(basis[i].y) * edge.y +
                                             double(basis[i].z) * edge.z);
                    extents[i] = RoundUp(support);
                }
                result.Box = Math::OBB(center, {extents[0], extents[1], extents[2]}, basis[0], basis[1], basis[2]);
            }
        }
        else
            return false;
        // 有限な入力でも合成でfloat範囲を越えうる。無効な形状を公開しない。
        bool valid = false;
        if (shape == EPhysicsProxyShape::Sphere)
            valid = Finite(result.Sphere.Center) && std::isfinite(result.Sphere.Radius) && result.Sphere.Radius > 0;
        else if (shape == EPhysicsProxyShape::Capsule)
            valid = Finite(result.Capsule.PointA) && Finite(result.Capsule.PointB) &&
                    std::isfinite(result.Capsule.Radius) && result.Capsule.Radius > 0;
        else if (shape == EPhysicsProxyShape::Box)
        {
            valid = Finite(result.Box.Center) && Finite(result.Box.HalfExtents) && result.Box.HalfExtents.x > 0 &&
                    result.Box.HalfExtents.y > 0 && result.Box.HalfExtents.z > 0;
            for (unsigned i = 0; i < 3; ++i)
            {
                valid = valid && Finite(result.Box.Axes[i]) &&
                        std::fabs(Vector3::Dot(result.Box.Axes[i], result.Box.Axes[i]) - 1) <= 1e-4f;
                for (unsigned j = 0; j < i; ++j)
                    valid = valid && std::fabs(Vector3::Dot(result.Box.Axes[i], result.Box.Axes[j])) <= 1e-4f;
            }
        }
        if (!valid)
            return false;
        out = result;
        return true;
    }
} // namespace NorvesLib::Modules::Physics
