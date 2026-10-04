#include "Resource/SkeletalDrawRange.h"
#include "Resource/SkeletalLimits.h"

namespace NorvesLib::Core::Skeletal
{
    bool ResolveSkeletalDrawRange(Container::Span<const SkeletalSubMesh> submeshes,
        uint64_t totalIndices, uint32_t submeshIndex, uint32_t materialSlot, uint32_t indexOffset,
        uint32_t indexCount, uint32_t vertexOffset, bool bShadow, bool bCastShadow, SkeletalDrawRange& out) noexcept
    {
        if (totalIndices == 0 || totalIndices > UINT32_MAX || totalIndices % 3 != 0 || vertexOffset != 0 ||
            submeshes.size() > MaximumSubmeshCount || (!submeshes.empty() && !submeshes.data()) ||
            (bShadow && !bCastShadow))
        {
            return false;
        }
        if (submeshes.empty())
        {
            if (submeshIndex != 0 || materialSlot != 0 || indexOffset != 0 ||
                (indexCount != 0 && indexCount != totalIndices))
            {
                return false;
            }
            indexCount = static_cast<uint32_t>(totalIndices);
        }
        else
        {
            if (submeshIndex >= submeshes.size())
            {
                return false;
            }
            const auto& selected = submeshes[submeshIndex];
            if (selected.IndexStart != indexOffset || selected.IndexCount != indexCount ||
                selected.MaterialSlot != materialSlot || (bShadow && selected.bNoShadow))
            {
                return false;
            }
        }
        if (indexCount == 0 || indexOffset % 3 != 0 || indexCount % 3 != 0 ||
            indexOffset > totalIndices || indexCount > totalIndices - indexOffset)
        {
            return false;
        }
        out = {indexOffset,indexCount};
        return true;
    }
} // namespace NorvesLib::Core::Skeletal
