// 攻撃スイープの接触時刻・接触点・法線と、不正入力時の出力保持を解析的な期待値で検証する。
// 回転は等角速度の弧と端点の直線補間を対比し、刀身のすり抜けと補間の取り違えを反証する。
#include "Gameplay/Combat/CombatGeometry.h"
#include "Math/GeometryIntersection.h"
#include "Math/QuaternionUtils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Combat;
    namespace M = NorvesLib::Math;

#define SWEEP_CHECK(expr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "CombatSweep %d: %s\n", __LINE__, #expr);                                               \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

    float Dot(const M::Vector3& a, const M::Vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float Distance(const M::Vector3& a, const M::Vector3& b)
    {
        const auto difference = a - b;
        return std::sqrt(Dot(difference, difference));
    }

    CombatSweepVolume Sphere(float radius, const M::Vector3& position)
    {
        CombatSweepVolume volume;
        volume.Shape.Radius = radius;
        volume.Previous.Position = position;
        volume.Current = volume.Previous;
        return volume;
    }

    CombatSweepVolume Blade(const M::Vector3& a, const M::Vector3& b)
    {
        CombatSweepVolume volume;
        volume.Shape.Type = ECombatShapeType::Capsule;
        volume.Shape.PointA = a;
        volume.Shape.PointB = b;
        volume.Shape.Radius = 0.02f;
        return volume;
    }

    void TranslationDoesNotTunnel()
    {
        auto hitbox = Blade({0, 0, 0}, {0, 1, 0});
        const float travel = 0.5f;
        hitbox.Current.Position.x = travel;
        const CombatSweepSettings settings;
        const float hurtRadius = 0.05f;
        const float timeTolerance = 0.5f * hurtRadius / travel;
        const float pointTolerance = 0.5f * hurtRadius;
        for (unsigned i = 0; i < 13; ++i)
        {
            // 始点と終点のどちらからも半径和以上離し、端点以外の線分上で接触させる。
            const float radiusSum = hitbox.Shape.Radius + hurtRadius;
            const float fraction = float(i + 1) / 14.0f;
            const auto hurtbox = Sphere(hurtRadius, {radiusSum + (travel - 2 * radiusSum) * fraction,
                                                     fraction, 0});
            CombatSweepContact contact;
            SWEEP_CHECK(!ComputeCombatContactAt(hitbox, hurtbox, 1.0f, contact));
            SWEEP_CHECK(SweepCombatVolumes(hitbox, hurtbox, settings, contact) == ECombatSweepResult::Hit);
            const float firstS = (hurtbox.Current.Position.x - radiusSum) / travel;
            const auto point = hurtbox.Current.Position - M::Vector3(hurtRadius, 0, 0);
            SWEEP_CHECK(std::fabs(contact.S - firstS) <= timeTolerance);
            SWEEP_CHECK(Distance(contact.Point, point) <= pointTolerance);
            SWEEP_CHECK(std::fabs(Dot(contact.Normal, contact.Normal) - 1.0f) < 1e-4f);
            SWEEP_CHECK(Dot(contact.Normal, {-1, 0, 0}) > 0.9f);
        }
    }

    void RotationFollowsArc()
    {
        auto hitbox = Blade({0, 0, 0.5f}, {0, 0, 1});
        // FromAxisAngle はラジアンを受け取る。
        const float halfSwing = M::Constants::PI / 3.0f;
        hitbox.Previous.Rotation = M::QuaternionUtils::FromAxisAngle(M::Vector3::UnitY, -halfSwing);
        hitbox.Current.Rotation = M::QuaternionUtils::FromAxisAngle(M::Vector3::UnitY, halfSwing);
        const auto hurtbox = Sphere(0.05f, {0, 0, 0.9f});
        const CombatSweepSettings settings;
        const auto substeps = ComputeCombatSubstepCount(hitbox, hurtbox, settings);
        SWEEP_CHECK(substeps > 0);
        CombatSweepContact contact;
        SWEEP_CHECK(SweepCombatVolumes(hitbox, hurtbox, settings, contact) == ECombatSweepResult::Hit);

        // 球中心から刀身の直線までの垂直距離は z*sin(|角度|)。最初の接触角を逆算する。
        const float radiusSum = hitbox.Shape.Radius + hurtbox.Shape.Radius;
        const float contactAngle = std::asin(radiusSum / hurtbox.Current.Position.z);
        const float projection = hurtbox.Current.Position.z * std::cos(contactAngle);
        SWEEP_CHECK(projection > hitbox.Shape.PointA.z && projection < hitbox.Shape.PointB.z);
        const float firstS = (halfSwing - contactAngle) / (2 * halfSwing);
        // 二分探索の刻みより広く、等角速度でない補間との差より十分狭い数値許容差。
        SWEEP_CHECK(std::fabs(contact.S - firstS) < 1e-4f);

        const auto previousA = hitbox.Previous.Rotation * hitbox.Shape.PointA;
        const auto previousB = hitbox.Previous.Rotation * hitbox.Shape.PointB;
        const auto currentA = hitbox.Current.Rotation * hitbox.Shape.PointA;
        const auto currentB = hitbox.Current.Rotation * hitbox.Shape.PointB;
        const M::Sphere sphere(hurtbox.Current.Position, hurtbox.Shape.Radius);
        for (uint32_t i = 0; i <= substeps; ++i)
        {
            const float s = float(i) / float(substeps);
            const M::Capsule linear(previousA * (1 - s) + currentA * s,
                                    previousB * (1 - s) + currentB * s, hitbox.Shape.Radius);
            M::GeometryContact linearContact;
            SWEEP_CHECK(!M::ComputeContact(linear, sphere, linearContact));
        }
    }

    void SubstepLimits()
    {
        auto hitbox = Sphere(0.05f, {});
        const auto hurtbox = Sphere(0.05f, {1, 0, 0});
        CombatSweepSettings settings;
        hitbox.Current.Position.x = 10.0f;
        SWEEP_CHECK(ComputeCombatSubstepCount(hitbox, hurtbox, settings) == 16);
        settings.MaxSubsteps = 4;
        SWEEP_CHECK(ComputeCombatSubstepCount(hitbox, hurtbox, settings) == 4);

        hitbox.Current.Position.x = 0.1f;
        settings.MaxSubsteps = 64;
        const auto expected = uint32_t(std::ceil(double(hitbox.Current.Position.x) / (0.5 * hitbox.Shape.Radius)));
        const auto originalCount = ComputeCombatSubstepCount(hitbox, hurtbox, settings);
        SWEEP_CHECK(expected < settings.MaxSubsteps && originalCount == expected);
        settings.MinRadius = 2 * hitbox.Shape.Radius;
        const auto enlargedRadiusCount = uint32_t(std::ceil(double(hitbox.Current.Position.x) /
                                                           (0.5 * settings.MinRadius)));
        SWEEP_CHECK(enlargedRadiusCount < originalCount);
        SWEEP_CHECK(ComputeCombatSubstepCount(hitbox, hurtbox, settings) == enlargedRadiusCount);
        hitbox.Current = hitbox.Previous;
        SWEEP_CHECK(ComputeCombatSubstepCount(hitbox, hurtbox, settings) == 1);
    }

    void BothVolumesMove()
    {
        auto hitbox = Sphere(0.02f, {-0.25f, 0, 0});
        auto hurtbox = Sphere(0.05f, {0.25f, 0, 0});
        hitbox.Current.Position.x = 0.25f;
        hurtbox.Current.Position.x = -0.25f;
        const float closingTravel = (hitbox.Current.Position.x - hitbox.Previous.Position.x) -
                                    (hurtbox.Current.Position.x - hurtbox.Previous.Position.x);
        const float firstS = (hurtbox.Previous.Position.x - hitbox.Previous.Position.x -
                              hitbox.Shape.Radius - hurtbox.Shape.Radius) / closingTravel;
        const auto hitCenter = hitbox.Previous.Position * (1 - firstS) + hitbox.Current.Position * firstS;
        const auto hurtCenter = hurtbox.Previous.Position * (1 - firstS) + hurtbox.Current.Position * firstS;
        const auto direction = (hurtCenter - hitCenter) / Distance(hurtCenter, hitCenter);
        const auto point = hitCenter + direction * hitbox.Shape.Radius;
        SWEEP_CHECK(Distance(point, hurtCenter - direction * hurtbox.Shape.Radius) < 1e-6f);
        CombatSweepContact contact;
        SWEEP_CHECK(SweepCombatVolumes(hitbox, hurtbox, {}, contact) == ECombatSweepResult::Hit);
        SWEEP_CHECK(std::fabs(contact.S - firstS) <= 0.05f);
        SWEEP_CHECK(Distance(contact.Point, point) <= 0.025f);
        CombatSweepContact atContact;
        SWEEP_CHECK(ComputeCombatContactAt(hitbox, hurtbox, contact.S, atContact));
        SWEEP_CHECK(Distance(atContact.Point, contact.Point) < 1e-6f);
    }

    void ReversedShapeOrders()
    {
        CombatSweepVolume box;
        box.Shape.Type = ECombatShapeType::Box;
        box.Shape.HalfExtents = {0.1f, 0.2f, 0.2f};
        const auto sphere = Sphere(0.05f, {0.13f, 0, 0});
        CombatSweepContact contact;
        SWEEP_CHECK(SweepCombatVolumes(box, sphere, {}, contact) == ECombatSweepResult::Hit);
        SWEEP_CHECK(Dot(contact.Normal, box.Current.Position - sphere.Current.Position) > 0);

        const auto capsule = Blade({0, -0.2f, 0}, {0, 0.2f, 0});
        const auto attackingSphere = Sphere(0.05f, {0.06f, 0, 0});
        SWEEP_CHECK(SweepCombatVolumes(attackingSphere, capsule, {}, contact) == ECombatSweepResult::Hit);
        SWEEP_CHECK(Dot(contact.Normal, attackingSphere.Current.Position - capsule.Current.Position) > 0);
    }

    void InvalidInputPreservesContact()
    {
        CombatSweepContact sentinel;
        sentinel.Point = {11, 12, 13};
        sentinel.Normal = {21, 22, 23};
        sentinel.Depth = 31;
        sentinel.S = 41;
        sentinel.SubstepCount = 51;
        const auto valid = Sphere(0.05f, {});
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const auto checkInvalid = [&](const CombatSweepVolume& hitbox, const CombatSweepVolume& hurtbox,
                                      const CombatSweepSettings& settings)
        {
            auto contact = sentinel;
            SWEEP_CHECK(SweepCombatVolumes(hitbox, hurtbox, settings, contact) == ECombatSweepResult::InvalidInput);
            SWEEP_CHECK(contact.Point == sentinel.Point && contact.Normal == sentinel.Normal &&
                        contact.Depth == sentinel.Depth && contact.S == sentinel.S &&
                        contact.SubstepCount == sentinel.SubstepCount);
        };
        auto broken = valid;
        broken.Previous.Position.x = nan;
        checkInvalid(broken, valid, {});
        broken = valid;
        broken.Current.Rotation.y = infinity;
        checkInvalid(valid, broken, {});
        broken = valid;
        broken.Previous.Rotation = M::Quaternion(0, 0, 0, 0);
        checkInvalid(broken, valid, {});
        broken = valid;
        broken.Shape.Radius = 0;
        checkInvalid(valid, broken, {});
        broken = valid;
        broken.Shape.Type = ECombatShapeType::Box;
        broken.Shape.HalfExtents = {0.1f, -0.1f, 0.1f};
        checkInvalid(broken, valid, {});
        CombatSweepSettings settings;
        settings.MaxSubsteps = 0;
        checkInvalid(valid, valid, settings);
        settings = {};
        settings.MinRadius = 0;
        checkInvalid(valid, valid, settings);
        settings.MinRadius = nan;
        checkInvalid(valid, valid, settings);
    }

    void DistantVolumesMiss()
    {
        CombatSweepContact contact;
        SWEEP_CHECK(SweepCombatVolumes(Sphere(0.05f, {}), Sphere(0.05f, {100, 0, 0}), {}, contact) == ECombatSweepResult::Miss);
    }
} // namespace

int main()
{
    TranslationDoesNotTunnel();
    RotationFollowsArc();
    SubstepLimits();
    BothVolumesMove();
    ReversedShapeOrders();
    InvalidInputPreservesContact();
    DistantVolumesMiss();
    std::puts("CombatSweepTest PASS");
    return 0;
}
