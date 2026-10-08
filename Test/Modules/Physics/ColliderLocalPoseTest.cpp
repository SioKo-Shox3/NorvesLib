// 局所姿勢の幾何を別のdouble四元数演算によるsupportと比較する。
#include "Physics/ColliderShapeTransform.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace
{
    using namespace NorvesLib::Modules::Physics;
    namespace M = NorvesLib::Math;
#define POSE_CHECK(expr)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "ColliderPose %d: %s\n", __LINE__, #expr);                                            \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
    struct D
    {
        double x, y, z;
    };
    D Add(D a, D b)
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    D Mul(D a, double b)
    {
        return {a.x * b, a.y * b, a.z * b};
    }
    double Dot(D a, D b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    D ToD(M::Vector3 v)
    {
        return {v.x, v.y, v.z};
    }
    D Rotate(M::Quaternion q, D v, bool normalize = false)
    {
        double x = q.x, y = q.y, z = q.z, w = q.w;
        if (normalize)
        {
            const double n = std::sqrt(x * x + y * y + z * z + w * w);
            x /= n;
            y /= n;
            z /= n;
            w /= n;
        }
        const D axis{x, y, z};
        const D cross{y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
        return Add(Add(Mul(v, w * w - x * x - y * y - z * z), Mul(axis, 2 * Dot(axis, v))), Mul(cross, 2 * w));
    }
    D Linear(const M::Transform& owner, D v)
    {
        return Rotate(owner.rotation, {owner.scale.x * v.x, owner.scale.y * v.y, owner.scale.z * v.z});
    }
    void RotatedContainment()
    {
        for (const auto scale : {M::Vector3(2, 1, .5f), M::Vector3(-2, 3, 1), M::Vector3(2, 2, 2)})
            for (const auto rotation : {M::Quaternion::Identity, M::Quaternion(0, 0, 0, 2)})
                for (float angle : {.3f, .785398163f, 1.570796327f})
                {
                    M::Transform owner({10, 2, -3}, rotation, scale);
                    M::Transform local({1, .4f, -.5f}, M::Quaternion(M::Vector3::UnitZ, angle));
                    const M::Vector3 half(1, 2, .5f);
                    PhysicsShapeProxy box;
                    box.UserData = 123;
                    POSE_CHECK(BuildColliderWorldShape(EPhysicsProxyShape::Box, .4f, 1, half, owner, local, box));
                    POSE_CHECK(box.UserData == 123);
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        D p{(corner & 1) ? half.x : -half.x, (corner & 2) ? half.y : -half.y,
                            (corner & 4) ? half.z : -half.z};
                        const D edge = Linear(owner, Rotate(local.rotation, p, true));
                        const float extents[] = {box.Box.HalfExtents.x, box.Box.HalfExtents.y, box.Box.HalfExtents.z};
                        for (unsigned axis = 0; axis < 3; ++axis)
                            POSE_CHECK(std::fabs(Dot(ToD(box.Box.Axes[axis]), edge)) <=
                                       extents[axis] + 1e-4 * (1 + extents[axis]));
                    }
                    PhysicsShapeProxy capsule;
                    POSE_CHECK(
                        BuildColliderWorldShape(EPhysicsProxyShape::Capsule, .4f, 1, half, owner, local, capsule));
                    const D center = Mul(Add(ToD(capsule.Capsule.PointA), ToD(capsule.Capsule.PointB)), .5);
                    const D a = Add(ToD(capsule.Capsule.PointA), Mul(center, -1));
                    const D b = Add(ToD(capsule.Capsule.PointB), Mul(center, -1));
                    const D segment = Linear(owner, Rotate(local.rotation, {0, 1, 0}, true));
                    const D columns[] = {Linear(owner, {1, 0, 0}), Linear(owner, {0, 1, 0}), Linear(owner, {0, 0, 1})};
                    for (unsigned i = 0; i < 64; ++i)
                    {
                        const D direction{std::cos(i * .7), std::sin(i * .7), std::sin(i * .31)};
                        const double x = Dot(direction, columns[0]), y = Dot(direction, columns[1]),
                                     z = Dot(direction, columns[2]);
                        const double exact = std::fabs(Dot(direction, segment)) + .4 * std::sqrt(x * x + y * y + z * z);
                        const double bound = std::max(Dot(direction, a), Dot(direction, b)) +
                                             capsule.Capsule.Radius * std::sqrt(Dot(direction, direction));
                        POSE_CHECK(bound + 1e-4 * (1 + bound) >= exact);
                    }
                }
    }
    void NormalizationAndFiniteOutput()
    {
        M::Transform a, b, pa, pb;
        a.rotation = M::Quaternion(1, 0, 0, 0);
        b.rotation = M::Quaternion(-1, 0, 0, 0);
        POSE_CHECK(PrepareColliderLocalPose(a, pa) && PrepareColliderLocalPose(b, pb));
        POSE_CHECK(pa.rotation == pb.rotation && pa.rotation.x == 1);
        M::Transform local;
        local.rotation =
            M::Quaternion(-.72082430124282837f, -.538532555103302f, .1980423629283905f, .3888113796710968f);
        POSE_CHECK(PrepareColliderLocalPose(local, pa));
        PhysicsShapeProxy proxy;
        POSE_CHECK(BuildColliderWorldShape(EPhysicsProxyShape::Capsule, 1000, 0, {1, 1, 1}, {}, local, proxy));
        const auto radial = pa.rotation * M::Vector3(1000, 0, 0);
        const double length =
            std::sqrt(double(radial.x) * radial.x + double(radial.y) * radial.y + double(radial.z) * radial.z);
        POSE_CHECK(proxy.Capsule.Radius >= length);
        proxy.UserData = 987;
        const auto previous = proxy;
        M::Transform owner;
        owner.scale = {4, 1, 1};
        local.position = {1e38f, 0, 0};
        POSE_CHECK(!BuildColliderWorldShape(EPhysicsProxyShape::Sphere, 1, 1, {1, 1, 1}, owner, local, proxy));
        POSE_CHECK(proxy.UserData == 987 && proxy.Capsule.Radius == previous.Capsule.Radius);
        local.position = {};
        owner.rotation = M::Quaternion(1e30f, 0, 0, 1);
        POSE_CHECK(!BuildColliderWorldShape(EPhysicsProxyShape::Box, 1, 1, {1, 1, 1}, owner, local, proxy));
        POSE_CHECK(proxy.UserData == 987 && proxy.Capsule.Radius == previous.Capsule.Radius);
    }
    void LegacyAndKnownValues()
    {
        M::Transform owner({10, 2, 3}, M::Quaternion::Identity, {2, 3, 1});
        PhysicsShapeProxy capsule;
        POSE_CHECK(BuildColliderWorldShape(EPhysicsProxyShape::Capsule, .4f, 1, {1, 2, .5f}, owner, {}, capsule));
        POSE_CHECK(capsule.Capsule.Radius == .8f);
        POSE_CHECK(capsule.Capsule.PointA == M::Vector3(10, -1, 3));
        POSE_CHECK(capsule.Capsule.PointB == M::Vector3(10, 5, 3));
        M::Transform local({1, 0, 0});
        POSE_CHECK(BuildColliderWorldShape(EPhysicsProxyShape::Capsule, .4f, 1, {1, 2, .5f}, owner, local, capsule));
        POSE_CHECK(capsule.Capsule.Radius == .8f && capsule.Capsule.PointA == M::Vector3(12, -1, 3));
        owner.scale = {2, 1, .5f};
        local.rotation = M::Quaternion(M::Vector3::UnitZ, M::Constants::PI / 4);
        PhysicsShapeProxy box;
        POSE_CHECK(BuildColliderWorldShape(EPhysicsProxyShape::Box, .4f, 1, {1, 2, .5f}, owner, local, box));
        POSE_CHECK(std::fabs(box.Box.HalfExtents.x - 2.5f) < 1e-5f);
        POSE_CHECK(std::fabs(box.Box.HalfExtents.y - 3.5f) < 1e-5f);
        POSE_CHECK(std::fabs(box.Box.HalfExtents.z - .25f) < 1e-5f);
        owner.position = {100000000, 100000000, 100000000};
        PhysicsShapeProxy farBox;
        POSE_CHECK(BuildColliderWorldShape(EPhysicsProxyShape::Box, .4f, 1, {1, 2, .5f}, owner, local, farBox));
        POSE_CHECK(farBox.Box.HalfExtents == box.Box.HalfExtents);
        M::Transform previous = local, normalized;
        local.scale = {2, 1, 1};
        POSE_CHECK(!PrepareColliderLocalPose(local, normalized));
        local = previous;
        local.rotation = M::Quaternion(0, 0, 0, -1);
        POSE_CHECK(PrepareColliderLocalPose(local, normalized) && normalized.rotation.w == 1);
    }
} // namespace
int main()
{
    RotatedContainment();
    LegacyAndKnownValues();
    NormalizationAndFiniteOutput();
    std::puts("ColliderLocalPoseTest PASS");
    return 0;
}
