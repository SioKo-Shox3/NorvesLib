#include "Math/GeometryIntersection.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Math;

namespace
{
    bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    void CheckVector(const Vector3& actual, const Vector3& expected, float tolerance = 1e-4f)
    {
        assert(Near(actual.x, expected.x, tolerance));
        assert(Near(actual.y, expected.y, tolerance));
        assert(Near(actual.z, expected.z, tolerance));
    }

    void Check(const GeometrySeparation& result, float distance, const Vector3& normal)
    {
        assert(Near(result.Distance, distance));
        CheckVector(result.NormalAToB, normal);
        assert(Near(VectorUtils::Length(result.NormalAToB), 1.0f));
        assert(result.bPenetrating == (distance <= 0.0f));
        assert(Near(VectorUtils::Dot(result.PointB - result.PointA, result.NormalAToB), result.Distance));
    }

    Capsule Ball(const Vector3& center, float radius = 0.5f)
    {
        return Capsule(center, center, radius);
    }

    OBB Box()
    {
        OBB box;
        box.HalfExtents = Vector3(1.0f);
        return box;
    }
}

int main()
{
    const Capsule vertical(Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.5f);
    // 面、辺、角の解析距離と最近表面点。
    const OBB box = Box();
    Check(ComputeSeparation(Ball(Vector3(3.0f, 0.0f, 0.0f)), box), 1.5f, Vector3(-1.0f, 0.0f, 0.0f));
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    Check(ComputeSeparation(Ball(Vector3(2.0f, 2.0f, 0.0f)), box), std::sqrt(2.0f) - 0.5f,
        Vector3(-invSqrt2, -invSqrt2, 0.0f));
    const float invSqrt3 = 1.0f / std::sqrt(3.0f);
    Check(ComputeSeparation(Ball(Vector3(2.0f)), box), std::sqrt(3.0f) - 0.5f, Vector3(-invSqrt3));
    const auto face = ComputeSeparation(Ball(Vector3(3.0f, 0.0f, 0.0f)), box);
    CheckVector(face.PointA, Vector3(2.5f, 0.0f, 0.0f));
    CheckVector(face.PointB, Vector3(1.0f, 0.0f, 0.0f));
    const Capsule faceCapsule(Vector3(3.0f, -2.0f, 0.0f), Vector3(3.0f, 2.0f, 0.0f), 0.5f);
    Check(ComputeSeparation(faceCapsule, box), 1.5f, Vector3(-1.0f, 0.0f, 0.0f));
    std::cout << "faces_edges_corners passed\n";

    // 接触、浅い侵入、箱中心線の深い侵入。
    Check(ComputeSeparation(Ball(Vector3(1.5f, 0.0f, 0.0f)), box), 0.0f, Vector3(-1.0f, 0.0f, 0.0f));
    Check(ComputeSeparation(Ball(Vector3(1.25f, 0.0f, 0.0f)), box), -0.25f, Vector3(-1.0f, 0.0f, 0.0f));
    const auto deep = ComputeSeparation(Ball(Vector3(0.0f)), box);
    assert(deep.bPenetrating && Near(deep.Distance, -1.5f));
    assert(Near(VectorUtils::Length(deep.NormalAToB), 1.0f));
    GeometryContact oldContact;
    assert(ComputeContact(Ball(Vector3(0.0f)), box, oldContact));
    CheckVector(deep.NormalAToB, oldContact.Normal);
    assert(Near(-deep.Distance, oldContact.Depth));
    std::cout << "touching_penetration passed\n";

    // カプセルの中央・端点、平行、斜交、縮退した球。
    Check(ComputeSeparation(vertical, Sphere(Vector3(3.0f, 0.0f, 0.0f), 0.5f)), 2.0f, Vector3(1.0f, 0.0f, 0.0f));
    Check(ComputeSeparation(vertical, Sphere(Vector3(0.0f, 4.0f, 0.0f), 0.5f)), 2.0f, Vector3(0.0f, 1.0f, 0.0f));
    const Capsule parallel(Vector3(3.0f, -1.0f, 0.0f), Vector3(3.0f, 1.0f, 0.0f), 0.5f);
    Check(ComputeSeparation(vertical, parallel), 2.0f, Vector3(1.0f, 0.0f, 0.0f));
    const Capsule skew(Vector3(-1.0f, 0.0f, 3.0f), Vector3(1.0f, 0.0f, 3.0f), 0.5f);
    Check(ComputeSeparation(vertical, skew), 2.0f, Vector3(0.0f, 0.0f, 1.0f));
    const auto reversed = ComputeSeparation(skew, vertical);
    Check(reversed, 2.0f, Vector3(0.0f, 0.0f, -1.0f));
    Check(ComputeSeparation(Ball(Vector3(0.0f)), Ball(Vector3(1.0f, 0.0f, 0.0f))), 0.0f, Vector3(1.0f, 0.0f, 0.0f));
    Check(ComputeSeparation(vertical, vertical), -1.0f, Vector3(1.0f, 0.0f, 0.0f));
    const Capsule tiny(Vector3(0.0f), Vector3(0.0f, 0.0001f, 0.0f), 0.0f);
    Check(ComputeSeparation(tiny, Sphere(Vector3(0.0f, 0.0002f, 0.0f), 0.0f)), 0.0001f, Vector3(0.0f, 1.0f, 0.0f));
    const Capsule shortCorner(Vector3(1.0001f, 1.0003f, 0.0f), Vector3(1.0003f, 1.0001f, 0.0f), 0.0003f);
    const auto cornerResult = ComputeSeparation(shortCorner, box);
    const float halfSum = ((shortCorner.PointA.x - 1.0f) + (shortCorner.PointA.y - 1.0f)) * 0.5f;
    assert(Near(cornerResult.Distance, std::sqrt(2.0f) * halfSum - shortCorner.Radius, 1e-7f));
    assert(cornerResult.bPenetrating);
    const Capsule shortXAxis(Vector3(-0.0001f, 0.0f, 0.0f), Vector3(0.0001f, 0.0f, 0.0f), 0.5f);
    const auto shortResult = ComputeSeparation(shortXAxis, Sphere(Vector3(0.0f), 0.5f));
    assert(Near(shortResult.Distance, -1.0f));
    assert(Near(shortResult.NormalAToB.x, 0.0f, 1e-7f));
    assert(Near(VectorUtils::Length(shortResult.PointA), 0.5f));
    const Capsule crossingA(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 1e-9f);
    const Capsule crossingB(Vector3(-1.0f, -1e-8f, 0.0f), Vector3(1.0f, 1e-8f, 0.0f), 1e-9f);
    const auto crossing = ComputeSeparation(crossingA, crossingB);
    assert(Near(crossing.Distance, -2e-9f, 1e-12f) && crossing.bPenetrating);
    assert(Near(VectorUtils::Length(crossing.NormalAToB), 1.0f));
    const auto subnormal = ComputeSeparation(Ball(Vector3(0.0f), 0.0f), Sphere(Vector3(1e-40f, 0.0f, 0.0f), 0.0f));
    CheckVector(subnormal.NormalAToB, Vector3(1.0f, 0.0f, 0.0f));
    assert(subnormal.Distance > 0.0f && !subnormal.bPenetrating);
    assert(std::isfinite(subnormal.PointA.x) && std::isfinite(subnormal.PointB.x));
    const float tinyScale = 1e-23f;
    const Capsule tinyCrossA(Vector3(-tinyScale, 0.0f, 0.0f), Vector3(tinyScale, 0.0f, 0.0f), tinyScale);
    const Capsule tinyCrossB(Vector3(0.0f, -tinyScale, 0.0f), Vector3(0.0f, tinyScale, 0.0f), tinyScale);
    const auto tinyCross = ComputeSeparation(tinyCrossA, tinyCrossB);
    CheckVector(tinyCross.NormalAToB, Vector3(0.0f, 0.0f, 1.0f));
    assert(tinyCross.PointA.z > 0.0f && tinyCross.PointB.z < 0.0f);
    std::cout << "capsule_pairs_degeneracy passed\n";

    // 箱の回転と並進に対して距離は不変、法線は同じ回転に従う。
    OBB rotated = box;
    rotated.Center = Vector3(10.0f, 4.0f, 2.0f);
    rotated.Axes[0] = Vector3(invSqrt2, invSqrt2, 0.0f);
    rotated.Axes[1] = Vector3(-invSqrt2, invSqrt2, 0.0f);
    Check(ComputeSeparation(Ball(rotated.Center + rotated.Axes[0] * 3.0f), rotated), 1.5f, -1.0f * rotated.Axes[0]);
    std::cout << "rotated_box passed\n";

    // 固定乱数の縮退カプセルを独立な球の解析式で比較する。
    uint32_t seed = 0x4813ac29u;
    auto sample = [&seed]()
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed & 0xffffu) / 65535.0f * 10.0f - 5.0f;
    };
    for (int i = 0; i < 10000; ++i)
    {
        const Vector3 a(sample(), sample(), sample());
        const Vector3 b(sample(), sample(), sample());
        const float ra = std::fabs(sample()) * 0.1f;
        const float rb = std::fabs(sample()) * 0.1f;
        const double dx = static_cast<double>(b.x) - a.x;
        const double dy = static_cast<double>(b.y) - a.y;
        const double dz = static_cast<double>(b.z) - a.z;
        const float expected = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz) - ra - rb);
        const auto result = ComputeSeparation(Ball(a, ra), Ball(b, rb));
        assert(Near(result.Distance, expected));
        assert(result.bPenetrating == (expected <= 0.0f));
        assert(Near(VectorUtils::Length(result.NormalAToB), 1.0f));
        const auto sphereResult = ComputeSeparation(Ball(a, ra), Sphere(b, rb));
        assert(Near(result.Distance, sphereResult.Distance));
        CheckVector(result.PointA, sphereResult.PointA);
        CheckVector(result.PointB, sphereResult.PointB);
    }
    std::cout << "random_sphere_oracle_10000 passed\nGeometrySeparationTest passed\n";
    return 0;
}
