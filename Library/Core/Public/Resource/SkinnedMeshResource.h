#pragma once

#include "Rendering/SkinnedMeshTypes.h"
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

        Rendering::SkinnedMeshHandle GetRenderMeshHandle() const;
        const Container::TSharedPtr<Rendering::SkinnedMeshAssetLease>& GetRenderAssetLease() const;
        const Container::VariableArray<Skeletal::SkeletalVertex>& GetVertices() const;
        const Container::VariableArray<uint32_t>& GetIndices() const;
        void RefreshRenderAssetLease();
        void ReleaseRenderAssetLease();


    private:
        Container::TSharedPtr<Rendering::SkinnedMeshAssetLease> m_RenderAssetLease;
        uint64_t m_RenderAssetGeneration = 0;
        Container::VariableArray<Skeletal::SkeletalVertex> m_Vertices;
        Container::VariableArray<uint32_t> m_Indices;
    };
} // namespace NorvesLib::Core
