#include "AI/Spatial/UniformGridHash.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace NorvesLib::Modules::AI::Spatial
{
    namespace
    {
        int32_t ToCell(double position, double cellSize)
        {
            constexpr double limit = 1 << 30;
            return static_cast<int32_t>(std::clamp(std::floor(position / cellSize), -limit, limit));
        }
    }

    UniformGridHash::UniformGridHash(float cellSize)
    {
        SetCellSize(cellSize);
    }

    bool UniformGridHash::SetCellSize(float cellSize)
    {
        if (!std::isfinite(cellSize) || cellSize <= 0.0f)
        {
            return false;
        }
        m_CellSize = cellSize;
        return true;
    }

    float UniformGridHash::GetCellSize() const
    {
        return m_CellSize;
    }

    void UniformGridHash::Clear()
    {
        m_Xs.clear();
        m_Zs.clear();
        m_Entries.clear();
        m_Cells.clear();
    }

    void UniformGridHash::Build(Core::Container::Span<const float> xs, Core::Container::Span<const float> zs)
    {
        const size_t count = (std::min)(xs.size(), zs.size());
        if (count > UINT32_MAX)
        {
            throw std::length_error("格子に入力できる点の数が上限を超えました");
        }
        Clear();
        m_BuiltCellSize = m_CellSize;
        if (count == 0)
        {
            return;
        }

        m_Xs.assign(xs.begin(), xs.begin() + count);
        m_Zs.assign(zs.begin(), zs.begin() + count);
        m_Entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (std::isfinite(m_Xs[i]) && std::isfinite(m_Zs[i]))
            {
                m_Entries.push_back({{ToCell(m_Xs[i], m_BuiltCellSize), ToCell(m_Zs[i], m_BuiltCellSize)}, i});
            }
        }

        std::sort(m_Entries.begin(), m_Entries.end(), [](const Entry& a, const Entry& b)
        {
            return a.Cell == b.Cell ? a.Index < b.Index : a.Cell < b.Cell;
        });

        uint32_t begin = 0;
        while (begin < m_Entries.size())
        {
            const CellKey cell = m_Entries[begin].Cell;
            uint32_t end = begin + 1;
            while (end < m_Entries.size() && m_Entries[end].Cell == cell)
            {
                ++end;
            }
            m_Cells.push_back({cell, begin, end});
            begin = end;
        }
    }

    uint32_t UniformGridHash::GetPointCount() const
    {
        return static_cast<uint32_t>(m_Entries.size());
    }

    void UniformGridHash::QueryRadius(float x, float z, float radius, Core::Container::VariableArray<uint32_t>& out,
                                      uint32_t excludeIndex) const
    {
        out.clear();
        if (m_Cells.empty() || !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radius) || radius < 0.0f)
        {
            return;
        }

        const double radiusSquared = static_cast<double>(radius) * radius;
        const auto visit = [&](const CellRange& range)
        {
            for (uint32_t i = range.Begin; i < range.End; ++i)
            {
                const uint32_t index = m_Entries[i].Index;
                const double dx = static_cast<double>(m_Xs[index]) - x;
                const double dz = static_cast<double>(m_Zs[index]) - z;
                if (index != excludeIndex && dx * dx + dz * dz <= radiusSquared)
                {
                    out.push_back(index);
                }
            }
        };

        // 座標差が丸めで半径と等しくなる点も、候補の矩形から取りこぼさないようにする。
        const double extent = radius == 0.0f ? 0.0 :
            std::nextafter(static_cast<double>(radius), std::numeric_limits<double>::infinity());
        const int32_t minX = ToCell(x - extent, m_BuiltCellSize);
        const int32_t maxX = ToCell(x + extent, m_BuiltCellSize);
        const int32_t minZ = ToCell(z - extent, m_BuiltCellSize);
        const int32_t maxZ = ToCell(z + extent, m_BuiltCellSize);
        const int64_t width = static_cast<int64_t>(maxX) - minX + 1;
        const int64_t height = static_cast<int64_t>(maxZ) - minZ + 1;
        if (width * height > static_cast<int64_t>(m_Cells.size()))
        {
            for (const CellRange& range : m_Cells)
            {
                if (minX <= range.Cell.X && range.Cell.X <= maxX && minZ <= range.Cell.Z && range.Cell.Z <= maxZ)
                {
                    visit(range);
                }
            }
        }
        else
        {
            for (int32_t cellX = minX; cellX <= maxX; ++cellX)
            {
                for (int32_t cellZ = minZ; cellZ <= maxZ; ++cellZ)
                {
                    const CellKey key{cellX, cellZ};
                    const auto it = std::lower_bound(m_Cells.begin(), m_Cells.end(), key,
                        [](const CellRange& range, CellKey target) { return range.Cell < target; });
                    if (it != m_Cells.end() && it->Cell == key)
                    {
                        visit(*it);
                    }
                }
            }
        }
        std::sort(out.begin(), out.end());
    }
}
