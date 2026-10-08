#include "Terrain/HeightField.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Terrain
{
    Container::TSharedPtr<const HeightField> HeightField::Create(uint32_t width, uint32_t depth, float spacing,
                                                                 Container::Span<const float> heights,
                                                                 Container::Span<const uint64_t> surfaces)
    {
        if (width < 2 || depth < 2 || uint64_t(width) * depth > 16777216 || heights.size() != uint64_t(width) * depth ||
            !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(float(width - 1) * spacing) ||
            !std::isfinite(float(depth - 1) * spacing) ||
            (!surfaces.empty() && surfaces.size() != uint64_t(width - 1) * (depth - 1)))
            return {};
        float minimum = heights[0], maximum = heights[0];
        for (float height : heights)
        {
            if (!std::isfinite(height))
                return {};
            minimum = std::min(minimum, height);
            maximum = std::max(maximum, height);
        }
        auto result = Container::MakeShared<HeightField>();
        result->m_Width = width;
        result->m_Depth = depth;
        result->m_Spacing = spacing;
        result->m_Heights.assign(heights.begin(), heights.end());
        if (!surfaces.empty())
            result->m_Surfaces.assign(surfaces.begin(), surfaces.end());
        result->m_Bounds =
            Math::AABB({0, minimum, 0}, {float(width - 1) * spacing, maximum, float(depth - 1) * spacing});
        return result;
    }
    uint64_t HeightField::GetSurfaceTag(uint32_t x, uint32_t z) const
    {
        return m_Surfaces.empty() ? 0 : m_Surfaces[size_t(z) * (m_Width - 1) + x];
    }
    Math::Vector3 HeightField::GetVertex(uint32_t x, uint32_t z) const
    {
        return {x * m_Spacing, GetHeight(x, z), z * m_Spacing};
    }
    Math::Vector3 HeightField::GetVertexNormal(uint32_t x, uint32_t z) const
    {
        const uint32_t left = x ? x - 1 : 0, right = std::min(x + 1, m_Width - 1), back = z ? z - 1 : 0,
                       front = std::min(z + 1, m_Depth - 1);
        const double dx = (double(GetHeight(right, z)) - GetHeight(left, z)) / ((right - left) * double(m_Spacing));
        const double dz = (double(GetHeight(x, front)) - GetHeight(x, back)) / ((front - back) * double(m_Spacing));
        const double length = std::sqrt(dx * dx + 1 + dz * dz);
        return {float(-dx / length), float(1 / length), float(-dz / length)};
    }
    void HeightField::GetTriangle(uint32_t x, uint32_t z, uint32_t triangle, Math::Vector3& a, Math::Vector3& b,
                                  Math::Vector3& c) const
    {
        if (triangle == 0)
        {
            a = GetVertex(x, z);
            b = GetVertex(x, z + 1);
            c = GetVertex(x + 1, z);
        }
        else
        {
            a = GetVertex(x + 1, z);
            b = GetVertex(x, z + 1);
            c = GetVertex(x + 1, z + 1);
        }
    }
    bool HeightField::Sample(float x, float z, HeightFieldSample& out) const
    {
        if (m_Width < 2 || m_Depth < 2 || !std::isfinite(x) || !std::isfinite(z) || x < 0 || z < 0 ||
            x > float(m_Width - 1) * m_Spacing || z > float(m_Depth - 1) * m_Spacing)
            return false;
        const double gx = double(x) / m_Spacing, gz = double(z) / m_Spacing;
        const uint32_t ix = std::min(uint32_t(gx), m_Width - 2), iz = std::min(uint32_t(gz), m_Depth - 2);
        const double u = std::clamp(gx - ix, 0.0, 1.0), v = std::clamp(gz - iz, 0.0, 1.0);
        const double h00 = GetHeight(ix, iz), h10 = GetHeight(ix + 1, iz), h01 = GetHeight(ix, iz + 1),
                     h11 = GetHeight(ix + 1, iz + 1);
        double height, dx, dz;
        if (u + v <= 1)
        {
            height = h00 + (h10 - h00) * u + (h01 - h00) * v;
            dx = (h10 - h00) / m_Spacing;
            dz = (h01 - h00) / m_Spacing;
        }
        else
        {
            height = h11 + (h01 - h11) * (1 - u) + (h10 - h11) * (1 - v);
            dx = (h11 - h01) / m_Spacing;
            dz = (h11 - h10) / m_Spacing;
        }
        const double length = std::sqrt(dx * dx + 1 + dz * dz);
        out = {float(height),
               {float(-dx / length), float(1 / length), float(-dz / length)},
               GetSurfaceTag(ix, iz),
               ix,
               iz};
        return true;
    }
} // namespace NorvesLib::Core::Terrain
