#include "Math/GeometryIntersection.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using namespace NorvesLib::Math;

namespace
{
    bool Near(float a, float b, float epsilon = 1e-3f)
    {
        return std::fabs(a - b) <= epsilon;
    }

    void HitAt(const GeometrySweepHit& hit, float distance, const Vector3& normal)
    {
        assert(hit.Result == EGeometrySweepResult::Hit);
        assert(Near(hit.Distance, distance));
        assert(Near(hit.Normal.x, normal.x));
        assert(Near(hit.Normal.y, normal.y));
        assert(Near(hit.Normal.z, normal.z));
    }

    OBB BoxAt(const Vector3& center, const Vector3& extent = Vector3(0.5f))
    {
        OBB result;
        result.Center = center;
        result.HalfExtents = extent;
        return result;
    }

    void CheckCapsuleAcceptance()
    {
        const Vector3 outward(-1.0f, 0.0f, 0.0f);
        const OBB box = BoxAt(Vector3(3.0f, 0.0f, 0.0f), Vector3(1.0f));
        const Capsule face(Vector3(0.0f, -0.5f, 0.0f), Vector3(0.0f, 0.5f, 0.0f), 0.5f);
        const Capsule edge(Vector3(0.0f, -0.5f, 1.3f), Vector3(0.0f, 0.5f, 1.3f), 0.5f);
        const Capsule corner(Vector3(0.0f, 1.3f, 1.3f), Vector3(0.0f, 2.3f, 1.3f), 0.5f);
        // 面は半径、辺/角は半径断面の直角三角形から距離と外向き法線を求める。
        HitAt(SweepCapsule(face, box, Vector3::UnitX, 5.0f), 1.5f, outward);
        HitAt(SweepCapsule(edge, box, Vector3::UnitX, 5.0f), 1.6f, Vector3(-0.8f, 0.0f, 0.6f));
        const float cornerOffset = std::sqrt(0.07f);
        HitAt(SweepCapsule(corner, box, Vector3::UnitX, 5.0f), 2.0f-cornerOffset,
            Vector3(-2.0f*cornerOffset, 0.6f, 0.6f));
        const Capsule horizontal(Vector3(-0.5f, 0.0f, 0.0f), Vector3(0.5f, 0.0f, 0.0f), 0.5f);
        HitAt(SweepCapsule(horizontal, box, Vector3::UnitX, 5.0f), 1.0f, outward);
        const Capsule target(Vector3(3.0f, -1.0f, 0.0f), Vector3(3.0f, 1.0f, 0.0f), 0.5f);
        HitAt(SweepCapsule(face, target, Vector3::UnitX, 5.0f), 2.0f, outward);
        // 初期接触も初期重なりとして通知する。内側中心の対称ケースは-Xを選ぶ。
        for (const float x : {1.5f, 1.75f, 3.0f})
        {
            const Capsule initial(Vector3(x, -0.5f, 0.0f), Vector3(x, 0.5f, 0.0f), 0.5f);
            const auto hit = SweepCapsule(initial, box, Vector3::UnitX, 5.0f);
            HitAt(hit, 0.0f, outward);
            assert(hit.bStartPenetrating && Near(hit.Depth, x-1.5f));
            const auto stationary = SweepCapsule(initial, box, Vector3(0.0f), 0.0f);
            HitAt(stationary, 0.0f, outward);
            assert(stationary.bStartPenetrating && Near(stationary.Depth, hit.Depth));
            GeometrySweepSettings ignored;
            ignored.bReportStartOverlap = false;
            assert(SweepCapsule(initial, box, Vector3::UnitX, 5.0f, ignored).Result == EGeometrySweepResult::NoHit);
        }
        std::cout << "capsule_face_edge_corner_rotation_initial passed\n";
    }

    template<typename Target>
    float ContactOracle(const Capsule& capsule, const Target& target, const Vector3& direction, float maxDistance)
    {
        GeometryContact contact;
        auto overlaps = [&](float distance)
        {
            const Vector3 offset = direction * distance;
            return ComputeContact(Capsule(capsule.PointA + offset, capsule.PointB + offset, capsule.Radius), target, contact);
        };
        if (overlaps(0.0f))
        {
            return 0.0f;
        }
        float previous = 0.0f;
        for (int step = 1; ; ++step)
        {
            const float distance = std::fmin(maxDistance, step * 0.002f);
            if (overlaps(distance))
            {
                float low = previous;
                float high = distance;
                for (int i = 0; i < 20; ++i)
                {
                    const float midpoint = (low + high) * 0.5f;
                    if (overlaps(midpoint))
                    {
                        high = midpoint;
                    }
                    else
                    {
                        low = midpoint;
                    }
                }
                return high;
            }
            if (distance == maxDistance)
            {
                return -1.0f;
            }
            previous = distance;
        }
    }

    uint32_t randomState = 0x67af2d3u;
    float Sample()
    {
        randomState = randomState * 1664525u + 1013904223u;
        return static_cast<float>(randomState & 0xffffu) / 65535.0f;
    }

    Vector3 UnitSample()
    {
        Vector3 direction(Sample() * 2.0f - 1.0f, Sample() * 2.0f - 1.0f, Sample() * 2.0f - 1.0f);
        if (VectorUtils::Length(direction) < 0.01f)
        {
            return Vector3(1.0f, 0.0f, 0.0f);
        }
        return direction / VectorUtils::Length(direction);
    }

    template<typename Target>
    void CompareOracle(const Capsule& capsule, const Target& target, const Vector3& direction)
    {
        const auto hit = SweepCapsule(capsule, target, direction, 6.0f);
        const float expected = ContactOracle(capsule, target, direction, 6.0f);
        assert(expected >= 0.0f);
        if (hit.Result == EGeometrySweepResult::IterationLimit)
        {
            GeometryContact contact;
            const Vector3 offset = direction * hit.Distance;
            assert(!ComputeContact(Capsule(capsule.PointA + offset, capsule.PointB + offset, capsule.Radius), target, contact));
            assert(hit.Distance <= expected);
        }
        else
        {
            if (hit.Result != EGeometrySweepResult::Hit || !Near(hit.Distance, expected, 0.002f))
            {
                std::cerr << "oracle_mismatch expected=" << expected << " actual=" << hit.Distance
                    << " result=" << static_cast<int>(hit.Result) << "\n";
                assert(false);
            }
        }
        assert(Near(VectorUtils::Length(hit.Normal), 1.0f, 1e-4f));
    }
}

int main()
{
    CheckCapsuleAcceptance();
    const Vector3 right(1.0f, 0.0f, 0.0f);
    const Sphere sphere(Vector3(0.0f), 0.5f);
    const OBB box = BoxAt(Vector3(5.0f, 0.0f, 0.0f));
    HitAt(SweepSphere(sphere, box, right, 10.0f), 4.0f, -1.0f * right);
    HitAt(SweepSphere(sphere, box, right * 100.0f, 10.0f), 4.0f, -1.0f * right);
    assert(SweepSphere(sphere, box, -1.0f * right, 10.0f).Result == EGeometrySweepResult::NoHit);
    assert(SweepSphere(sphere, box, right, 3.9f).Result == EGeometrySweepResult::NoHit);
    HitAt(SweepSphere(sphere, box, right, 4.0f), 4.0f, -1.0f * right);
    const Capsule upright(Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.5f);
    HitAt(SweepCapsule(upright, box, right, 10.0f), 4.0f, -1.0f * right);
    HitAt(SweepCapsule(upright, Capsule(Vector3(5.0f, -2.0f, 0.0f), Vector3(5.0f, 2.0f, 0.0f), 0.5f), right, 10.0f), 4.0f, -1.0f * right);
    HitAt(SweepCapsule(Capsule(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.5f), box, right, 10.0f), 3.0f, -1.0f * right);
    std::cout << "faces_capsules_distance passed\n";

    const float q = 1.0f / std::sqrt(2.0f);
    const OBB unitBox = BoxAt(Vector3(0.0f), Vector3(1.0f));
    HitAt(SweepSphere(Sphere(Vector3(3.0f, 3.0f, 0.0f), 0.5f), unitBox, Vector3(-q, -q, 0.0f), 10.0f),
        std::sqrt(8.0f) - 0.5f, Vector3(q, q, 0.0f));
    const float c = 1.0f / std::sqrt(3.0f);
    HitAt(SweepSphere(Sphere(Vector3(3.0f), 0.5f), unitBox, Vector3(-c), 10.0f), std::sqrt(12.0f) - 0.5f, Vector3(c));
    assert(SweepSphere(Sphere(Vector3(0.0f, 1.01f, 0.0f), 0.5f), box, right, 10.0f).Result == EGeometrySweepResult::NoHit);
    assert(SweepSphere(Sphere(Vector3(0.0f, 0.99f, 0.0f), 0.5f), box, right, 10.0f).Result == EGeometrySweepResult::Hit);
    OBB rotated = unitBox;
    rotated.Axes[0] = Vector3(q, q, 0.0f);
    rotated.Axes[1] = Vector3(-q, q, 0.0f);
    HitAt(SweepSphere(Sphere(rotated.Axes[0] * 3.0f, 0.5f), rotated, -1.0f * rotated.Axes[0], 5.0f), 1.5f, rotated.Axes[0]);
    std::cout << "edges_corners_grazing_rotation passed\n";

    const auto initial = SweepSphere(sphere, unitBox, right, 3.0f);
    assert(initial.Result == EGeometrySweepResult::Hit && initial.Distance == 0.0f && initial.bStartPenetrating);
    assert(Near(initial.Depth, 1.5f));
    GeometrySweepSettings ignored;
    ignored.bReportStartOverlap = false;
    assert(SweepSphere(sphere, unitBox, right, 3.0f, ignored).Result == EGeometrySweepResult::NoHit);
    assert(SweepSphere(sphere, unitBox, Vector3(0.0f), 0.0f).Result == EGeometrySweepResult::Hit);
    assert(SweepSphere(sphere, box, Vector3(0.0f), 0.0f).Result == EGeometrySweepResult::NoHit);
    assert(SweepSphere(Sphere(Vector3(3.99995f, 0.0f, 0.0f), 0.5f), box, right, 0.0f).Result == EGeometrySweepResult::NoHit);
    GeometrySweepSettings limited;
    limited.MaxIterations = 1;
    const auto unresolved = SweepSphere(sphere, box, right, 10.0f, limited);
    assert(unresolved.Result == EGeometrySweepResult::IterationLimit && unresolved.Distance == 0.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(SweepSphere(sphere, box, Vector3(nan, 0.0f, 0.0f), 1.0f).Result == EGeometrySweepResult::InvalidArgument);
    assert(SweepSphere(sphere, box, Vector3(0.0f), 1.0f).Result == EGeometrySweepResult::InvalidArgument);
    assert(SweepSphere(Sphere(Vector3(0.0f), -1.0f), box, right, 1.0f).Result == EGeometrySweepResult::InvalidArgument);
    OBB badBox = box;
    badBox.Axes[0] = Vector3(2.0f, 0.0f, 0.0f);
    assert(SweepSphere(sphere, badBox, right, 1.0f).Result == EGeometrySweepResult::InvalidArgument);
    std::cout << "initial_zero_invalid_limit passed\n";

    // 2mm刻みの既存接触判定と二分法を独立oracleとする。全候補は中心を横切る。
    for (int i = 0; i < 10000; ++i)
    {
        const Vector3 direction = UnitSample();
        const Vector3 halfSegment = UnitSample() * (Sample() * 0.8f);
        const Vector3 start = -3.0f * direction;
        const Capsule moving(start - halfSegment, start + halfSegment, 0.1f + Sample() * 0.4f);
        if (i % 3 == 0)
        {
            CompareOracle(moving, Sphere(Vector3(0.0f), 0.1f + Sample() * 0.4f), direction);
        }
        else if (i % 3 == 1)
        {
            OBB target = BoxAt(Vector3(0.0f), Vector3(0.2f + Sample() * 0.3f));
            const float angle = Sample() * Constants::TWO_PI;
            target.Axes[0] = Vector3(std::cos(angle), std::sin(angle), 0.0f);
            target.Axes[1] = Vector3(-std::sin(angle), std::cos(angle), 0.0f);
            CompareOracle(moving, target, direction);
        }
        else
        {
            const Vector3 axis = UnitSample() * (0.1f + Sample());
            CompareOracle(moving, Capsule(-1.0f * axis, axis, 0.1f + Sample() * 0.4f), direction);
        }
    }
    std::cout << "contact_oracle_10000 passed\nGeometrySweepTest passed\n";
    return 0;
}
