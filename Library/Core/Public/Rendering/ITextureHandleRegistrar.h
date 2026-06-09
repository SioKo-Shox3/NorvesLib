#pragma once

#include "Rendering/RenderTypes.h"
#include "Container/PointerTypes.h"
#include "Container/String.h"

namespace NorvesLib::RHI
{
    class ITexture;
}

namespace NorvesLib::Core::Rendering
{
    class ITextureHandleRegistrar
    {
    public:
        virtual ~ITextureHandleRegistrar() = default;

        virtual TextureHandle RegisterExternalTexture(
            Container::TSharedPtr<RHI::ITexture> rhiTexture,
            const Container::String &debugName = Container::String()) = 0;

        virtual void ReleaseTexture(TextureHandle handle) = 0;
    };

} // namespace NorvesLib::Core::Rendering
