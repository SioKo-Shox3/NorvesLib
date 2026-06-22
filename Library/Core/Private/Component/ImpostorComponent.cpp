#include "Component/ImpostorComponent.h"

#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(ImpostorComponent, BillboardComponent)

    ImpostorComponent::ImpostorComponent()
        : BillboardComponent()
    {
        BakedAtlasTextureHandle = Rendering::TextureHandle::Invalid();
        BakedAtlasMetadata = Rendering::ImpostorBakeMetadata{};
        Space = Rendering::BoardSpace::WorldSpace;
    }

    ImpostorComponent::ImpostorComponent(const FieldInitializer *initializer)
        : BillboardComponent(initializer)
    {
        BakedAtlasTextureHandle = Rendering::TextureHandle::Invalid();
        BakedAtlasMetadata = Rendering::ImpostorBakeMetadata{};
        Space = Rendering::BoardSpace::WorldSpace;
    }

    ImpostorComponent::ImpostorComponent(const IUnknown *sourceObject)
        : BillboardComponent(sourceObject)
    {
        BakedAtlasTextureHandle = Rendering::TextureHandle::Invalid();
        BakedAtlasMetadata = Rendering::ImpostorBakeMetadata{};
        Space = Rendering::BoardSpace::WorldSpace;
    }

    ImpostorComponent::~ImpostorComponent()
    {
        if (BakedAtlasTextureHandle->IsValid())
        {
            NORVES_LOG_WARNING("ImpostorComponent",
                               "ImpostorComponent destroyed with a baked atlas handle still attached; call ReleaseBakedAtlas with TextureResources first");
        }
    }

    bool ImpostorComponent::SetBakedAtlas(Rendering::TextureHandle texture,
                                          const Rendering::ImpostorBakeMetadata &metadata)
    {
        if (!texture.IsValid() || !metadata.IsValid())
        {
            return false;
        }

        if (BakedAtlasTextureHandle->IsValid() && BakedAtlasTextureHandle.Get() != texture)
        {
            NORVES_LOG_ERROR("ImpostorComponent",
                             "Refusing to replace a baked atlas without an explicit release");
            return false;
        }

        BakedAtlasTextureHandle = texture;
        BakedAtlasMetadata = metadata;
        MarkRenderStateDirty();
        return true;
    }

    void ImpostorComponent::ReleaseBakedAtlas(Rendering::TextureResources &textures,
                                              Rendering::RenderWorld *renderWorld)
    {
        if (renderWorld)
        {
            renderWorld->WaitForRender();
        }

        if (BakedAtlasTextureHandle->IsValid())
        {
            textures.ReleaseTexture(BakedAtlasTextureHandle);
        }

        BakedAtlasTextureHandle = Rendering::TextureHandle::Invalid();
        BakedAtlasMetadata = Rendering::ImpostorBakeMetadata{};
        MarkRenderStateDirty();
    }

    bool ImpostorComponent::BuildBoardProxy(Rendering::BoardProxy &outProxy,
                                            const Rendering::MaterialResources *materials) const
    {
        if (!BillboardComponent::BuildBoardProxy(outProxy, materials))
        {
            return false;
        }

        if (HasBakedAtlas())
        {
            outProxy.Texture = BakedAtlasTextureHandle;
        }

        outProxy.Space = Rendering::BoardSpace::WorldSpace;
        return true;
    }

} // namespace NorvesLib::Core::Component
