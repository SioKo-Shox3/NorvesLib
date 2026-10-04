#include "Resource/SkeletalSubmeshLayout.h"
#include "Resource/SkeletalLimits.h"
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    SkeletalSubmeshLayoutResult ResolveSkeletalSubmeshLayout(Container::Span<const SkeletalSubMesh> submeshes,
        uint64_t totalIndexCount, uint64_t materialSlotCount) noexcept
    {
        const auto fail = [](SkeletalSubmeshLayoutStatus status)
        {
            SkeletalSubmeshLayoutResult result;
            result.Status = status;
            return result;
        };
        if (!submeshes.data() && !submeshes.empty())
        {
            return fail(SkeletalSubmeshLayoutStatus::InvalidInput);
        }
        if (totalIndexCount == 0 || totalIndexCount > std::numeric_limits<uint32_t>::max() || totalIndexCount % 3 != 0)
        {
            return fail(SkeletalSubmeshLayoutStatus::InvalidTotalIndexCount);
        }
        if (submeshes.empty() && materialSlotCount == 0)
        {
            SkeletalSubmeshLayoutResult result;
            result.Status = SkeletalSubmeshLayoutStatus::Success;
            result.SubmeshCount = 1;
            result.MaterialSlotCount = 1;
            result.bUsesImplicitSingleSubmesh = true;
            result.ImplicitSubmesh = {0, static_cast<uint32_t>(totalIndexCount), 0};
            return result;
        }
        if (submeshes.empty() || materialSlotCount == 0)
        {
            return fail(SkeletalSubmeshLayoutStatus::IncompleteTables);
        }
        if (submeshes.size() > MaximumSubmeshCount)
        {
            return fail(SkeletalSubmeshLayoutStatus::SubmeshLimitExceeded);
        }
        if (materialSlotCount > MaximumMaterialSlotCount)
        {
            return fail(SkeletalSubmeshLayoutStatus::MaterialSlotLimitExceeded);
        }
        uint64_t nextIndex = 0;
        for (const auto& submesh : submeshes)
        {
            // u32の加算を先に行わず、差分で残量と比較する。
            if (submesh.IndexStart != nextIndex || submesh.IndexCount == 0 || submesh.IndexCount % 3 != 0 ||
                submesh.IndexCount > totalIndexCount - nextIndex)
            {
                return fail(SkeletalSubmeshLayoutStatus::InvalidIndexRange);
            }
            if (submesh.MaterialSlot >= materialSlotCount)
            {
                return fail(SkeletalSubmeshLayoutStatus::InvalidMaterialSlot);
            }
            nextIndex += submesh.IndexCount;
        }
        if (nextIndex != totalIndexCount)
        {
            return fail(SkeletalSubmeshLayoutStatus::InvalidIndexRange);
        }
        SkeletalSubmeshLayoutResult result;
        result.Status = SkeletalSubmeshLayoutStatus::Success;
        result.SubmeshCount = static_cast<uint32_t>(submeshes.size());
        result.MaterialSlotCount = static_cast<uint32_t>(materialSlotCount);
        return result;
    }
} // namespace NorvesLib::Core::Skeletal
