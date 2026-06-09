#pragma once

#include "Rendering/MaterialTypes.h"
#include "Rendering/NeuralMaterialResource.h"
#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class ITextureHandleRegistrar;

    class RenderMaterialStore
    {
    public:
        explicit RenderMaterialStore(Thread::Atomic<uint64_t> &nextHandleId);
        ~RenderMaterialStore();

        RenderMaterialStore(const RenderMaterialStore &) = delete;
        RenderMaterialStore &operator=(const RenderMaterialStore &) = delete;

        MaterialHandle CreateMaterial(const MaterialCreateData &createInfo);
        const MaterialResourceData *GetMaterialData(MaterialHandle handle) const;
        bool UpdateMaterial(MaterialHandle handle, const MaterialCreateData &createInfo);
        void ReleaseMaterial(MaterialHandle handle, ITextureHandleRegistrar &textureRegistrar);

        MaterialHandle CreateNeuralMaterial(RHI::IDevice *device,
                                            ITextureHandleRegistrar &textureRegistrar,
                                            const NeuralMaterialDesc &desc);
        Container::VariableArray<NeuralMaterialResource *> GetNeuralMaterialResources() const;

        void Clear(ITextureHandleRegistrar &textureRegistrar);

    private:
        MaterialHandle AllocateMaterialHandle();
        void ClearUnregistered();
        void ShutdownNeuralMaterialWithoutRegistrar(NeuralMaterialResource &neuralMaterial);

        Container::Map<uint64_t, MaterialResourceData> m_Materials;
        Container::Map<uint64_t, Container::TSharedPtr<NeuralMaterialResource>> m_NeuralMaterials;
        mutable Thread::Mutex m_Mutex;
        Thread::Atomic<uint64_t> &m_NextHandleId;
    };

} // namespace NorvesLib::Core::Rendering
