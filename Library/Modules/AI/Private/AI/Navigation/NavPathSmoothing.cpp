#include "AI/Navigation/NavPathSmoothing.h"

#include "AI/Navigation/NavDynamicOverlay.h"
#include "AI/Navigation/NavGridQueries.h"

namespace NorvesLib::Modules::AI
{
    bool SmoothNavPath(const NavGrid& grid, Core::Container::Span<const NavCellCoord> cells, float radius,
                       Core::Container::VariableArray<Math::Vector3>& outPoints, const NavDynamicOverlay* overlay)
    {
        outPoints.clear();
        if (cells.empty())
        {
            return false;
        }

        const bool bCheckCost = overlay && !overlay->IsEmpty();
        size_t anchor = 0;
        outPoints.push_back(grid.GetCellCenter(cells[anchor]));
        while (anchor < cells.size() - 1)
        {
            const Math::Vector3 start = outPoints.back();
            Math::Vector3 lastVisible = start;
            size_t next = anchor;
            uint64_t pathExtra = 0;
            for (size_t j = anchor + 1; j < cells.size(); ++j)
            {
                const Math::Vector3 end = grid.GetCellCenter(cells[j]);
                if (bCheckCost)
                {
                    pathExtra += overlay->GetCostExtraPer128(cells[j]);
                }
                // 隣接辺は元の経路に含まれる。折れ点を飛ばすときだけ、角に触れるセルも含めて比べる。
                uint64_t shortcutExtra = 0;
                const auto ray = bCheckCost && j > anchor + 1
                    ? NavGridQueries::Raycast(grid, start, end, radius, *overlay, shortcutExtra)
                    : NavGridQueries::Raycast(grid, start, end, radius, overlay);
                if (ray.bHit || shortcutExtra > pathExtra)
                {
                    break;
                }
                lastVisible = end;
                next = j;
            }

            // 探索後の押印で隣接セルへの線分も塞がれた場合は、経路全体を破棄する。
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
