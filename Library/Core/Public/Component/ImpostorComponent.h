#pragma once

#include "Component/BillboardComponent.h"
#include "Rendering/ImpostorBake.h"

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

        Rendering::TextureHandle GetBakedAtlasTextureHandle() const { return BakedAtlasTextureHandle; }
        const Rendering::ImpostorBakeMetadata &GetBakedAtlasMetadata() const { return BakedAtlasMetadata; }
        bool HasBakedAtlas() const { return BakedAtlasTextureHandle->IsValid() && BakedAtlasMetadata->IsValid(); }

        bool BuildBoardProxy(Rendering::BoardProxy &outProxy,
                             const Rendering::MaterialResources *materials = nullptr) const override;

    protected:
        PROPERTY(Rendering::TextureHandle, BakedAtlasTextureHandle)
        PROPERTY(Rendering::ImpostorBakeMetadata, BakedAtlasMetadata)
    };

    using ImpostorComponentPtr = Container::TSharedPtr<ImpostorComponent>;
    using ImpostorComponentWeakPtr = Container::TWeakPtr<ImpostorComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::ImpostorComponent, NorvesLib::Core::EClassCastFlags::ImpostorComponent)
