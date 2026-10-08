#include "Resource/SkeletalSubmeshLayout.h"
#include <cmath>
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
    SkeletalSubmeshLayoutResult ValidateSkeletalSubmeshData(
        Container::Span<const SkeletalSubMesh> submeshes, Container::Span<const uint32_t> indices,
        uint64_t vertexCount, uint64_t materialSlotCount) noexcept
    {
        const auto layout = ResolveSkeletalSubmeshLayout(submeshes, indices.size(), materialSlotCount);
        if (!layout.Succeeded())
        {
            return layout;
        }
        SkeletalSubmeshLayoutResult failure;
        failure.Status = SkeletalSubmeshLayoutStatus::InvalidVertexData;
        if (!indices.data() || vertexCount == 0 || vertexCount > UINT32_MAX)
        {
            return failure;
        }
        for (uint32_t index : indices)
        {
            if (index >= vertexCount)
            {
                return failure;
            }
        }
        for (const auto& submesh : submeshes)
        {
            if (submesh.VertexCount > vertexCount)
            {
                return failure;
            }
            if (submesh.VertexCount != 0)
            {
                for (size_t index = submesh.IndexStart; index < size_t(submesh.IndexStart) + submesh.IndexCount; ++index)
                {
                    if (indices[index] >= submesh.VertexCount)
                    {
                        return failure;
                    }
                }
            }
            failure.Status = SkeletalSubmeshLayoutStatus::InvalidMetadata;
            if (!std::isfinite(submesh.BoundsRadius) || submesh.BoundsRadius < 0.0f)
            {
                return failure;
            }
            for (float center : submesh.BoundsCenter)
            {
                if (!std::isfinite(center))
                {
                    return failure;
                }
            }
            failure.Status = SkeletalSubmeshLayoutStatus::InvalidVertexData;
        }
        return layout;
    }
} // namespace NorvesLib::Core::Skeletal
