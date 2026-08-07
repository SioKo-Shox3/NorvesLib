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
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void SkinnedMeshResource::Unload()
    {
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
} // namespace NorvesLib::Core
