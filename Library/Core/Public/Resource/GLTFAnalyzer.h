#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderTypes.h"

namespace NorvesLib::Core::Rendering
{
    class RenderResourceManager;
}

namespace NorvesLib::Core::Resource
{
    /**
     * @brief Analyzes a glTF file and builds a renderable model resource.
     */
    class GLTFAnalyzer
    {
    public:
        /**
         * @brief Loads a glTF file and builds a model resource.
         * @param gltfPath Path to the glTF file.
         * @param resourceManager Render resource manager used for GPU resources.
         * @return Model handle, or Invalid on failure.
         */
        static Rendering::ModelHandle LoadModel(const Container::String& gltfPath,
                                                Rendering::RenderResourceManager& resourceManager);
    };

} // namespace NorvesLib::Core::Resource
