#pragma once
#include "Math/Quaternion.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include <cmath>
namespace Game::GameModes::DogMovementSmokeGeometry
{
    namespace Container = NorvesLib::Core::Container;
    namespace M = NorvesLib::Math;
    using NorvesLib::Core::Rendering::Mesh3DVertex;
    using NorvesLib::Core::Rendering::ProceduralMeshGenerator;
    // 外部素材に依存しない比較用カプセル。犬の最終体格やrigを規定しない。
    inline void Capsule(Container::VariableArray<Mesh3DVertex>& vertices, Container::VariableArray<uint32_t>& indices)
    {
        constexpr unsigned slices = 24, halfStacks = 8;
        constexpr float pi = 3.14159265358979323846f;
        for (unsigned half = 0; half < 2; ++half)
            for (unsigned row = 0; row <= halfStacks; ++row)
            {
                const float phi = (float(half) + float(row) / halfStacks) * pi * .5f;
                for (unsigned col = 0; col <= slices; ++col)
                {
                    const float theta = float(col) / slices * 2 * pi;
                    const M::Vector3 normal(std::sin(phi) * std::cos(theta), std::cos(phi),
                                            std::sin(phi) * std::sin(theta));
                    Mesh3DVertex v{};
                    v.Position[0] = normal.x * .3f;
                    v.Position[1] = .4f + normal.y * .3f + (half == 0 ? .1f : -.1f);
                    v.Position[2] = normal.z * .3f;
                    v.Normal[0] = normal.x;
                    v.Normal[1] = normal.y;
                    v.Normal[2] = normal.z;
                    v.TexCoord[0] = float(col) / slices;
                    v.TexCoord[1] = phi / pi;
                    vertices.push_back(v);
                }
            }
        constexpr unsigned rows = (halfStacks + 1) * 2;
        for (unsigned row = 0; row + 1 < rows; ++row)
            for (unsigned col = 0; col < slices; ++col)
            {
                const uint32_t a = row * (slices + 1) + col, b = a + slices + 1;
                // 既存ProceduralMeshGeneratorと同じClockwise表面。
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(a + 1);
                indices.push_back(a + 1);
                indices.push_back(b);
                indices.push_back(b + 1);
            }
    }
    inline void WallBox(Container::VariableArray<Mesh3DVertex>& vertices, Container::VariableArray<uint32_t>& indices)
    {
        vertices.clear();
        indices.clear();
        Container::VariableArray<Mesh3DVertex> plane;
        Container::VariableArray<uint32_t> triangles;
        ProceduralMeshGenerator::GeneratePlane(1, 1, 1, 1, plane, triangles);
        const M::Vector3 centers[] = {{0, 0, .1f}, {0, 0, -.1f}, {4, 0, 0}, {-4, 0, 0}, {0, 1.5f, 0}, {0, -1.5f, 0}};
        const M::Vector3 scales[] = {{8, 1, 3}, {8, 1, 3}, {3, 1, .2f}, {3, 1, .2f}, {8, 1, .2f}, {8, 1, .2f}};
        const M::Quaternion rotations[] = {M::Quaternion(M::Vector3::UnitX, 1.57079632679f),
                                           M::Quaternion(M::Vector3::UnitX, -1.57079632679f),
                                           M::Quaternion(M::Vector3::UnitZ, -1.57079632679f),
                                           M::Quaternion(M::Vector3::UnitZ, 1.57079632679f),
                                           M::Quaternion(),
                                           M::Quaternion(M::Vector3::UnitX, 3.14159265359f)};
        for (unsigned face = 0; face < 6; ++face)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (auto vertex : plane)
            {
                const M::Vector3 local(vertex.Position[0] * scales[face].x, 0, vertex.Position[2] * scales[face].z);
                const auto position = centers[face] + rotations[face] * local;
                const auto normal = rotations[face] * M::Vector3::UnitY;
                vertex.Position[0] = position.x;
                vertex.Position[1] = position.y;
                vertex.Position[2] = position.z;
                vertex.Normal[0] = normal.x;
                vertex.Normal[1] = normal.y;
                vertex.Normal[2] = normal.z;
                vertices.push_back(vertex);
            }
            for (const auto index : triangles)
                indices.push_back(base + index);
        }
    }
} // namespace Game::GameModes::DogMovementSmokeGeometry
