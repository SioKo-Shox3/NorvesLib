#include "Rendering/SkinnedMeshTypes.h"
#include "Resource/SkeletalSubmeshLayout.h"
#include "Asset/CookedSkeletalNameCodec.h"

namespace NorvesLib::Core::Rendering
{
    SkinnedMeshAssetLease::SkinnedMeshAssetLease(SkinnedMeshHandle handle,
        Container::VariableArray<SkinnedMeshVertex>&& vertices, Container::VariableArray<uint32_t>&& indices)
        : SkinnedMeshAssetLease(handle, std::move(vertices), std::move(indices), {}, {})
    {
    }

    SkinnedMeshAssetLease::SkinnedMeshAssetLease(SkinnedMeshHandle handle,
        Container::VariableArray<SkinnedMeshVertex>&& vertices, Container::VariableArray<uint32_t>&& indices,
        Container::VariableArray<Skeletal::SkeletalSubMesh>&& submeshes, Container::VariableArray<Container::String>&& slotNames)
        : m_Handle(handle), m_Vertices(std::move(vertices)), m_Indices(std::move(indices)),
          m_SubMeshes(std::move(submeshes)), m_MaterialSlotNames(std::move(slotNames))
    {
        if (!m_Handle.IsValid() || !Skeletal::ValidateSkeletalSubmeshData({m_SubMeshes.data(),m_SubMeshes.size()},
                {m_Indices.data(),m_Indices.size()},m_Vertices.size(),m_MaterialSlotNames.size()).Succeeded())
        {
            return;
        }
        for (const auto& name : m_MaterialSlotNames)
        {
            if (!Asset::MeasureSkeletalNameEncoding<Container::String::value_type>(2,{name.data(),name.size()}).Succeeded())
            {
                return;
            }
        }
        m_bValidRenderData = true;
    }
} // namespace NorvesLib::Core::Rendering
