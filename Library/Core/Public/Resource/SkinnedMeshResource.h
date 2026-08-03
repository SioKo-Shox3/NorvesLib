#pragma once

#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core
{
    class SkinnedMeshResource : public Resource
    {
        REFLECTION_CLASS(SkinnedMeshResource, Resource)

    public:
        SkinnedMeshResource();
        explicit SkinnedMeshResource(const FieldInitializer* initializer);
        explicit SkinnedMeshResource(const IUnknown* sourceObject);
        ~SkinnedMeshResource() override;

        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;

        void SetVertices(Container::VariableArray<Skeletal::SkeletalVertex>&& vertices);
        void SetIndices(Container::VariableArray<uint32_t>&& indices);

        const Container::VariableArray<Skeletal::SkeletalVertex>& GetVertices() const;
        const Container::VariableArray<uint32_t>& GetIndices() const;

    private:
        Container::VariableArray<Skeletal::SkeletalVertex> m_Vertices;
        Container::VariableArray<uint32_t> m_Indices;
    };
} // namespace NorvesLib::Core
