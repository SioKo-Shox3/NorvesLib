#include "Resource/SkinnedMeshResource.h"

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
        SetResourceState(ResourceState::Unloaded);
    }

    size_t SkinnedMeshResource::GetMemorySize() const
    {
        return sizeof(SkinnedMeshResource) + m_Vertices.size() * sizeof(Skeletal::SkeletalVertex) +
               m_Indices.size() * sizeof(uint32_t);
    }

    void SkinnedMeshResource::SetVertices(Container::VariableArray<Skeletal::SkeletalVertex>&& vertices)
    {
        m_Vertices = std::move(vertices);
    }

    void SkinnedMeshResource::SetIndices(Container::VariableArray<uint32_t>&& indices)
    {
        m_Indices = std::move(indices);
    }

    const Container::VariableArray<Skeletal::SkeletalVertex>& SkinnedMeshResource::GetVertices() const
    {
        return m_Vertices;
    }

    const Container::VariableArray<uint32_t>& SkinnedMeshResource::GetIndices() const
    {
        return m_Indices;
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
        for (uint32_t index : m_Indices)
        {
            if (static_cast<size_t>(index) >= m_Vertices.size())
            {
                return;
            }
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
            std::move(renderIndices));
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
