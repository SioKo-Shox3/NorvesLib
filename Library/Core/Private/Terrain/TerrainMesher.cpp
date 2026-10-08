#include "Terrain/TerrainMesher.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Terrain
{
    bool BuildTerrainMesh(const HeightField& field, Container::VariableArray<Rendering::Mesh3DVertex>& vertices,
                          Container::VariableArray<uint32_t>& indices, uint32_t step, float uvScale, float skirtDepth)
    {
        if (field.GetWidth() < 2 || field.GetDepth() < 2 || !step || !std::isfinite(uvScale) || uvScale <= 0 ||
            !std::isfinite(skirtDepth) || skirtDepth < 0)
            return false;
        const uint32_t nx = uint32_t((uint64_t(field.GetWidth() - 2) + step) / step) + 1;
        const uint32_t nz = uint32_t((uint64_t(field.GetDepth() - 2) + step) / step) + 1;
        Container::VariableArray<Rendering::Mesh3DVertex> result;
        Container::VariableArray<uint32_t> triangles;
        result.reserve(size_t(nx) * nz);
        for (uint32_t z = 0; z < nz; ++z)
            for (uint32_t x = 0; x < nx; ++x)
            {
                const uint32_t sx = uint32_t(std::min(uint64_t(x) * step, uint64_t(field.GetWidth() - 1)));
                const uint32_t sz = uint32_t(std::min(uint64_t(z) * step, uint64_t(field.GetDepth() - 1)));
                const auto p = field.GetVertex(sx, sz), n = field.GetVertexNormal(sx, sz);
                Rendering::Mesh3DVertex vertex{};
                vertex.Position[0] = p.x;
                vertex.Position[1] = p.y;
                vertex.Position[2] = p.z;
                vertex.Normal[0] = n.x;
                vertex.Normal[1] = n.y;
                vertex.Normal[2] = n.z;
                vertex.TexCoord[0] = p.x * uvScale;
                vertex.TexCoord[1] = p.z * uvScale;
                result.push_back(vertex);
            }
        for (uint32_t z = 0; z + 1 < nz; ++z)
            for (uint32_t x = 0; x + 1 < nx; ++x)
            {
                const uint32_t a = z * nx + x, b = a + 1, c = a + nx, d = c + 1;
                for (uint32_t index : {a, b, c, b, d, c})
                    triangles.push_back(index);
            }
        if (skirtDepth > 0)
        {
            Container::VariableArray<uint32_t> border;
            for (uint32_t x = 0; x < nx; ++x)
                border.push_back(x);
            for (uint32_t z = 1; z < nz; ++z)
                border.push_back(z * nx + nx - 1);
            for (uint32_t x = nx - 1; x-- > 0;)
                border.push_back((nz - 1) * nx + x);
            for (uint32_t z = nz - 1; z-- > 1;)
                border.push_back(z * nx);
            const auto first = uint32_t(result.size());
            for (auto index : border)
            {
                auto vertex = result[index];
                vertex.Position[1] -= skirtDepth;
                result.push_back(vertex);
            }
            for (uint32_t i = 0; i < border.size(); ++i)
            {
                const uint32_t next = (i + 1) % uint32_t(border.size());
                for (uint32_t index : {border[i], first + i, border[next], border[next], first + i, first + next})
                    triangles.push_back(index);
            }
        }
        vertices = std::move(result);
        indices = std::move(triangles);
        return true;
    }
} // namespace NorvesLib::Core::Terrain
