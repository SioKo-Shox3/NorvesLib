#pragma once

#include "Rendering/RenderResourceManager.h"

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
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
        void ReleaseMaterial(MaterialHandle handle);

        MaterialHandle CreateNeuralMaterial(RHI::IDevice *device,
                                            RenderResourceManager &resourceManager,
                                            const NeuralMaterialDesc &desc);
        Container::VariableArray<NeuralMaterialResource *> GetNeuralMaterialResources() const;

        void Clear();

    private:
        MaterialHandle AllocateMaterialHandle();

        Container::Map<uint64_t, MaterialResourceData> m_Materials;
        Container::Map<uint64_t, Container::TSharedPtr<NeuralMaterialResource>> m_NeuralMaterials;
        mutable Thread::Mutex m_Mutex;
        Thread::Atomic<uint64_t> &m_NextHandleId;
    };

} // namespace NorvesLib::Core::Rendering
