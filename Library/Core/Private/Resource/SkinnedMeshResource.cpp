#include "Resource/SkinnedMeshResource.h"
#include "Asset/CookedSkinMeshV1.h"
#include "Asset/RigSplitWire.h"
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
        if (m_bSplitV1)
        {
            ReleaseRenderAssetLease();
            SetResourceState(m_SplitMesh ? ResourceState::Loaded : ResourceState::Failed);
            return bool(m_SplitMesh);
        }
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
        m_SplitMesh.reset();
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
        if (m_SplitMesh)
        {
            size += sizeof(Skeletal::SkinMeshV1Data) + m_SplitMesh->Vertices.size() * sizeof(Skeletal::SkeletalVertex) +
                    m_SplitMesh->Indices.size() * sizeof(uint32_t);
            size += m_SplitMesh->InverseBindMatrices.size() * sizeof(Container::FixedArray<float, 16>);
            size += m_SplitMesh->Topology.CanonicalBytes.size() + m_SplitMesh->SkeletonPath.size();
            size += m_SplitMesh->SubMeshes.size() * sizeof(Skeletal::SkeletalSubMesh);
            for (const auto& slot : m_SplitMesh->Slots)
            {
                size += sizeof(slot) + slot.Name.size();
            }
            for (const auto& material : m_SplitMesh->Materials)
            {
                size += sizeof(material);
                for (const auto& path : material.Textures)
                {
                    size += path.size();
                }
            }
        }
        return size;
    }

    bool SkinnedMeshResource::SetSplitMesh(const Skeletal::SkinMeshV1& mesh)
    {
        if (IsLoaded() || !mesh.m_Data)
        {
            return false;
        }
        Container::VariableArray<Skeletal::SkeletalMaterialSlot> slots;
        slots.reserve(mesh.m_Data->Slots.size());
        for (const auto& slot : mesh.m_Data->Slots)
        {
            Skeletal::SkeletalMaterialSlot native;
            if (!Skeletal::SplitWire::NativeName(slot.Name, native.Name))
            {
                return false;
            }
            slots.push_back(std::move(native));
        }
        ReleaseRenderAssetLease();
        m_Vertices.clear();
        m_Indices.clear();
        m_SubMeshes.clear();
        m_MaterialSlots = std::move(slots);
        m_SplitMesh = mesh.m_Data;
        m_bSplitV1 = true;
        return true;
    }
    bool SkinnedMeshResource::IsSplitV1() const noexcept
    {
        return m_bSplitV1;
    }
    const Skeletal::SkinMeshV1Data* SkinnedMeshResource::GetSplitMesh() const noexcept
    {
        return m_SplitMesh.get();
    }
    void SkinnedMeshResource::SetVertices(Container::VariableArray<Skeletal::SkeletalVertex>&& vertices)
    {
        if (m_bSplitV1)
        {
            return;
        }
        m_Vertices = std::move(vertices);
    }

    void SkinnedMeshResource::SetIndices(Container::VariableArray<uint32_t>&& indices)
    {
        if (m_bSplitV1)
        {
            return;
        }
        m_Indices = std::move(indices);
    }

    void SkinnedMeshResource::SetSubmeshTables(Container::VariableArray<Skeletal::SkeletalSubMesh>&& submeshes,
        Container::VariableArray<Skeletal::SkeletalMaterialSlot>&& slots)
    {
        if (m_bSplitV1)
        {
            return;
        }
        m_SubMeshes = std::move(submeshes);
        m_MaterialSlots = std::move(slots);
    }

    const Container::VariableArray<Skeletal::SkeletalSubMesh>& SkinnedMeshResource::GetSubMeshes() const
    {
        return m_SplitMesh ? m_SplitMesh->SubMeshes : m_SubMeshes;
    }

    const Container::VariableArray<Skeletal::SkeletalMaterialSlot>& SkinnedMeshResource::GetMaterialSlots() const
    {
        return m_MaterialSlots;
    }

    void SkinnedMeshResource::SetMeshNodeGlobalTransform(const Container::FixedArray<float, 16>& transform)
    {
        if (m_bSplitV1)
        {
            return;
        }
        m_MeshNodeGlobalTransform = transform;
    }

    const Container::VariableArray<Skeletal::SkeletalVertex>& SkinnedMeshResource::GetVertices() const
    {
        return m_SplitMesh ? m_SplitMesh->Vertices : m_Vertices;
    }

    const Container::VariableArray<uint32_t>& SkinnedMeshResource::GetIndices() const
    {
        return m_SplitMesh ? m_SplitMesh->Indices : m_Indices;
    }

    const Container::FixedArray<float, 16>& SkinnedMeshResource::GetMeshNodeGlobalTransform() const
    {
        return m_SplitMesh ? m_SplitMesh->MeshTransform : m_MeshNodeGlobalTransform;
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
        if (m_bSplitV1)
        {
            return;
        }
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
