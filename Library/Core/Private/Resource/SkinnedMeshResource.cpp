#include "Resource/SkinnedMeshResource.h"
#include "Resource/SkeletalSubmeshLayout.h"
#include "Asset/CookedSkeletalNameCodec.h"

#include <utility>

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(SkinnedMeshResource, Resource)

    SkinnedMeshResource::SkinnedMeshResource() = default;

    SkinnedMeshResource::SkinnedMeshResource(const FieldInitializer* initializer)
        : Resource(initializer)
    {
    }

    SkinnedMeshResource::SkinnedMeshResource(const IUnknown* sourceObject)
        : Resource(sourceObject)
    {
    }

    SkinnedMeshResource::~SkinnedMeshResource()
    {
        Finalize();
    }

    void SkinnedMeshResource::Initialize()
    {
        Resource::Initialize();
    }

    void SkinnedMeshResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }

    bool SkinnedMeshResource::Load()
    {
        RefreshRenderAssetLease();
        if (!m_RenderAssetLease)
        {
            SetResourceState(ResourceState::Failed);
            return false;
        }
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void SkinnedMeshResource::Unload()
    {
        ReleaseRenderAssetLease();
        m_Vertices.clear();
        m_Indices.clear();
        m_SubMeshes.clear();
        m_MaterialSlots.clear();
        SetResourceState(ResourceState::Unloaded);
    }

    size_t SkinnedMeshResource::GetMemorySize() const
    {
        size_t size = sizeof(SkinnedMeshResource) + m_Vertices.size() * sizeof(Skeletal::SkeletalVertex) +
            m_Indices.size() * sizeof(uint32_t) + m_SubMeshes.size() * sizeof(Skeletal::SkeletalSubMesh) +
            m_MaterialSlots.size() * sizeof(Skeletal::SkeletalMaterialSlot);
        for (const auto& slot : m_MaterialSlots)
        {
            size += slot.Name.size() * sizeof(Container::String::value_type);
        }
        return size;
    }

    void SkinnedMeshResource::SetVertices(Container::VariableArray<Skeletal::SkeletalVertex>&& vertices)
    {
        m_Vertices = std::move(vertices);
    }

    void SkinnedMeshResource::SetIndices(Container::VariableArray<uint32_t>&& indices)
    {
        m_Indices = std::move(indices);
    }

    void SkinnedMeshResource::SetSubmeshTables(Container::VariableArray<Skeletal::SkeletalSubMesh>&& submeshes,
        Container::VariableArray<Skeletal::SkeletalMaterialSlot>&& slots)
    {
        m_SubMeshes = std::move(submeshes);
        m_MaterialSlots = std::move(slots);
    }

    const Container::VariableArray<Skeletal::SkeletalSubMesh>& SkinnedMeshResource::GetSubMeshes() const
    {
        return m_SubMeshes;
    }

    const Container::VariableArray<Skeletal::SkeletalMaterialSlot>& SkinnedMeshResource::GetMaterialSlots() const
    {
        return m_MaterialSlots;
    }

    void SkinnedMeshResource::SetMeshNodeGlobalTransform(const Container::FixedArray<float, 16>& transform)
    {
        m_MeshNodeGlobalTransform = transform;
    }

    const Container::VariableArray<Skeletal::SkeletalVertex>& SkinnedMeshResource::GetVertices() const
    {
        return m_Vertices;
    }

    const Container::VariableArray<uint32_t>& SkinnedMeshResource::GetIndices() const
    {
        return m_Indices;
    }

    const Container::FixedArray<float, 16>& SkinnedMeshResource::GetMeshNodeGlobalTransform() const
    {
        return m_MeshNodeGlobalTransform;
    }

    Rendering::SkinnedMeshHandle SkinnedMeshResource::GetRenderMeshHandle() const
    {
        return m_RenderAssetLease ? m_RenderAssetLease->GetHandle() : Rendering::SkinnedMeshHandle::Invalid();
    }

    const Container::TSharedPtr<Rendering::SkinnedMeshAssetLease>& SkinnedMeshResource::GetRenderAssetLease() const
    {
        return m_RenderAssetLease;
    }

    void SkinnedMeshResource::RefreshRenderAssetLease()
    {
        ReleaseRenderAssetLease();
        if (GetResourceId() == 0 || m_Vertices.empty() || m_Indices.empty())
        {
            return;
        }
        if (!Skeletal::ValidateSkeletalSubmeshData({m_SubMeshes.data(), m_SubMeshes.size()},
                {m_Indices.data(), m_Indices.size()}, m_Vertices.size(), m_MaterialSlots.size()).Succeeded())
        {
            return;
        }
        Container::VariableArray<Container::String> slotNames;
        slotNames.reserve(m_MaterialSlots.size());
        for (const auto& slot : m_MaterialSlots)
        {
            if (!Asset::MeasureSkeletalNameEncoding<Container::String::value_type>(2,
                    {slot.Name.data(), slot.Name.size()}).Succeeded())
            {
                return;
            }
            slotNames.push_back(slot.Name);
        }

        Container::VariableArray<Rendering::SkinnedMeshVertex> renderVertices;
        renderVertices.resize(m_Vertices.size());
        for (size_t vertexIndex = 0; vertexIndex < m_Vertices.size(); ++vertexIndex)
        {
            const Skeletal::SkeletalVertex& source = m_Vertices[vertexIndex];
            Rendering::SkinnedMeshVertex& destination = renderVertices[vertexIndex];
            destination.Position[0] = source.Position.X;
            destination.Position[1] = source.Position.Y;
            destination.Position[2] = source.Position.Z;
            destination.Normal[0] = source.Normal.X;
            destination.Normal[1] = source.Normal.Y;
            destination.Normal[2] = source.Normal.Z;
            destination.TexCoord[0] = source.TexCoord.U;
            destination.TexCoord[1] = source.TexCoord.V;
            for (uint32_t influenceIndex = 0; influenceIndex < 4; ++influenceIndex)
            {
                destination.BoneIndices[influenceIndex] = source.JointIndices[influenceIndex];
                destination.BoneWeights[influenceIndex] = source.JointWeights[influenceIndex];
            }
        }

        Container::VariableArray<uint32_t> renderIndices = m_Indices;
        Container::VariableArray<Skeletal::SkeletalSubMesh> renderSubmeshes = m_SubMeshes;
        Rendering::SkinnedMeshHandle handle;
        handle.Id = GetResourceId();
        ++m_RenderAssetGeneration;
        if (m_RenderAssetGeneration == 0)
        {
            ++m_RenderAssetGeneration;
        }
        handle.Generation = m_RenderAssetGeneration;
        m_RenderAssetLease = Container::MakeShared<Rendering::SkinnedMeshAssetLease>(
            handle,
            std::move(renderVertices),
            std::move(renderIndices),
            std::move(renderSubmeshes),
            std::move(slotNames));
    }

    void SkinnedMeshResource::ReleaseRenderAssetLease()
    {
        if (m_RenderAssetLease)
        {
            m_RenderAssetLease->ReleaseAssetLease();
            m_RenderAssetLease.reset();
        }
    }
} // namespace NorvesLib::Core
