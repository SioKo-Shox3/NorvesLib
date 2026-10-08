#include "Game/GameModes/DogMovementSmoke/DogMovementSmokeGeometry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;
namespace
{
    unsigned Check(const Container::VariableArray<Mesh3DVertex>& vertices,
                   const Container::VariableArray<uint32_t>& indices)
    {
        assert(indices.size() % 3 == 0);
        for (const auto& v : vertices)
        {
            for (float value : v.Position)
                assert(std::isfinite(value));
            for (float value : v.Normal)
                assert(std::isfinite(value));
            const float n = v.Normal[0] * v.Normal[0] + v.Normal[1] * v.Normal[1] + v.Normal[2] * v.Normal[2];
            assert(std::fabs(n - 1) < 1e-5f);
        }
        unsigned degenerate = 0;
        const auto position = [&](uint32_t i) {
            assert(i < vertices.size());
            const auto& v = vertices[i];
            return Math::Vector3(v.Position[0], v.Position[1], v.Position[2]);
        };
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const auto a = position(indices[i]), b = position(indices[i + 1]), c = position(indices[i + 2]);
            const auto cross = Math::Vector3::Cross(b - a, c - a);
            if (cross.LengthSquared() < 1e-14f)
            {
                ++degenerate;
                continue;
            }
            const auto& v = vertices[indices[i]];
            const Math::Vector3 normal(v.Normal[0], v.Normal[1], v.Normal[2]);
            // GBufferのClockwise表面。通常の右手系外積とは反対向き。
            assert(Math::Vector3::Dot(cross, normal) < 0);
        }
        return degenerate;
    }
} // namespace
int main()
{
    Container::VariableArray<Mesh3DVertex> vertices;
    Container::VariableArray<uint32_t> indices;
    Game::GameModes::DogMovementSmokeGeometry::Capsule(vertices, indices);
    assert(vertices.size() == 450 && indices.size() == 2448);
    assert(Check(vertices, indices) == 48);
    float low = 1, high = -1;
    for (const auto& v : vertices)
    {
        assert(std::fabs(v.Position[0]) <= .300001f && std::fabs(v.Position[2]) <= .300001f);
        low = std::fmin(low, v.Position[1]);
        high = std::fmax(high, v.Position[1]);
    }
    assert(std::fabs(low) < 1e-6f && std::fabs(high - .8f) < 1e-6f);
    Game::GameModes::DogMovementSmokeGeometry::WallBox(vertices, indices);
    assert(vertices.size() == 24 && indices.size() == 36);
    assert(Check(vertices, indices) == 0);
    for (const auto& v : vertices)
        assert(std::fabs(v.Position[0]) <= 4.00001f && std::fabs(v.Position[1]) <= 1.50001f &&
               std::fabs(v.Position[2]) <= .10001f);
    std::cout << "DogMovementSmokeGeometryTest passed\n";
}
