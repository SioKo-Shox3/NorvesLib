#pragma once

#include "Container/Containers.h"
#include "Delegate/Delegate.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/RenderTypes.h"

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
         * @param resources Render resources used for texture and model finalization.
         * @return Model handle, or Invalid on failure.
         */
        static Rendering::ModelHandle LoadModel(const Container::String& gltfPath,
                                                Rendering::ModelLoadResourceContext resources);

        /**
         * @brief Loads a glTF file asynchronously and builds the GPU resources on the main thread later.
         * @param gltfPath Path to the glTF file.
         * @param resources Render resources used later for main-thread finalization.
         * @param callback Optional callback invoked on completion with the final model handle.
         * @return Async request id, or 0 on failure.
         */
        static uint32_t LoadModelAsync(const Container::String& gltfPath,
                                       Rendering::ModelLoadResourceContext resources,
                                       Delegate<void, Rendering::ModelHandle> callback = {});

        /**
         * @brief Flushes completed asynchronous glTF model loads.
         * @param resources Fresh render resources used for final GPU creation.
         * @param maxLoadsPerFrame Maximum number of completed loads to finalize this frame. 0 means no limit.
         * @return Number of completed loads finalized.
         */
        static uint32_t FlushCompletedModelLoads(Rendering::ModelLoadResourceContext resources,
                                                 uint32_t maxLoadsPerFrame = 1);

        /**
         * @brief Cancels a single in-flight asynchronous glTF model load.
         * @param requestId The id returned from LoadModelAsync. 0 is a no-op.
         *
         * After cancellation the completion callback will NOT fire. If the load
         * already produced a model by the time it is flushed, the loader releases
         * it itself, so no resource leaks. Safe to call from the game thread.
         */
        static void CancelModelLoad(uint32_t requestId);

        /**
         * @brief Detaches and waits for pending asynchronous glTF model loads without invoking callbacks.
         */
        static void CancelPendingModelLoadsAndWait();

        /**
         * @brief Gets the number of pending asynchronous glTF model loads.
         * @return Pending request count.
         */
        static uint32_t GetPendingAsyncModelLoadCount();
    };

} // namespace NorvesLib::Core::Resource
