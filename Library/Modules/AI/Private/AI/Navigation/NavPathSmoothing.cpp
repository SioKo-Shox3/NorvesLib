#include "AI/Navigation/NavPathSmoothing.h"

#include "AI/Navigation/NavGridQueries.h"

namespace NorvesLib::Modules::AI
{
    bool SmoothNavPath(const NavGrid& grid, Core::Container::Span<const NavCellCoord> cells, float radius,
                       Core::Container::VariableArray<Math::Vector3>& outPoints)
    {
        outPoints.clear();
        if (cells.empty())
        {
            return false;
        }

        size_t anchor = 0;
        outPoints.push_back(grid.GetCellCenter(cells[anchor]));
        while (anchor < cells.size() - 1)
        {
            const Math::Vector3 start = outPoints.back();
            Math::Vector3 lastVisible = start;
            size_t next = anchor;
            for (size_t j = anchor + 1; j < cells.size(); ++j)
            {
                const Math::Vector3 end = grid.GetCellCenter(cells[j]);
                if (NavGridQueries::Raycast(grid, start, end, radius).bHit)
                {
                    break;
                }
                lastVisible = end;
                next = j;
            }

            // A* の隣接セルは直進なら両端、斜めなら角を挟む 2 セルも通れるので、必ず先へ進める。
            if (next == anchor)
            {
                outPoints.clear();
                return false;
            }
            outPoints.push_back(lastVisible);
            anchor = next;
        }
        return true;
    }
} // namespace NorvesLib::Modules::AI
