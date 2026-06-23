#pragma once

#include "Component/BillboardComponent.h"
#include "Rendering/ImpostorBake.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
    class TextureResources;
}

namespace NorvesLib::Core::Component
{
    class ImpostorComponent : public BillboardComponent
    {
        REFLECTION_CLASS(ImpostorComponent, BillboardComponent)

    public:
        ImpostorComponent();
        explicit ImpostorComponent(const FieldInitializer *initializer);
        explicit ImpostorComponent(const IUnknown *sourceObject);
        virtual ~ImpostorComponent();

        bool SetBakedAtlas(Rendering::TextureHandle texture,
                           const Rendering::ImpostorBakeMetadata &metadata);
        void ReleaseBakedAtlas(Rendering::TextureResources &textures,
                               Rendering::RenderWorld *renderWorld = nullptr);
        void SetSourceMeshComponentId(uint64_t componentId);
        uint64_t GetSourceMeshComponentId() const { return m_SourceMeshComponentId; }
        void SetLODSwitchDistance(float distance);
        float GetLODSwitchDistance() const { return LODSwitchDistance; }

        Rendering::TextureHandle GetBakedAtlasTextureHandle() const { return BakedAtlasTextureHandle; }
        const Rendering::ImpostorBakeMetadata &GetBakedAtlasMetadata() const { return BakedAtlasMetadata; }
        bool HasBakedAtlas() const { return BakedAtlasTextureHandle->IsValid() && BakedAtlasMetadata->IsValid(); }

        bool BuildBoardProxy(Rendering::BoardProxy &outProxy,
                             const Rendering::MaterialResources *materials = nullptr) const override;

    protected:
        PROPERTY(Rendering::TextureHandle, BakedAtlasTextureHandle)
        PROPERTY(Rendering::ImpostorBakeMetadata, BakedAtlasMetadata)
        PROPERTY(float, LODSwitchDistance)
        uint64_t m_SourceMeshComponentId = 0;
    };

    using ImpostorComponentPtr = Container::TSharedPtr<ImpostorComponent>;
    using ImpostorComponentWeakPtr = Container::TWeakPtr<ImpostorComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::ImpostorComponent, NorvesLib::Core::EClassCastFlags::ImpostorComponent)
