#pragma once

#include "Rendering/RenderResourcesFwd.h"

namespace NorvesLib::Core::Rendering
{
    struct ModelLoadResourceContext
    {
        TextureResources &Textures;
        MegaGeometryResources &MegaGeometry;
    };

    struct RenderResourceFrameContext
    {
        GpuResources *Gpu = nullptr;
        TextureResources *Textures = nullptr;
        MaterialResources *Materials = nullptr;
        MeshResources *Meshes = nullptr;
        MegaGeometryResources *MegaGeometry = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
