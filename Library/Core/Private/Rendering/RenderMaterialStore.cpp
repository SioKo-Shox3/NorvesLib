#include "Rendering/RenderMaterialStore.h"

#include "Rendering/ITextureHandleRegistrar.h"
#include "Logging/LogMacros.h"
#include "RHI/IDevice.h"

#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        MaterialResourceData CopyMaterialCreateData(const MaterialCreateData &createInfo)
        {
            MaterialResourceData data;
            data.AlbedoTexture = createInfo.AlbedoTexture;
            data.NormalTexture = createInfo.NormalTexture;
            data.MetallicTexture = createInfo.MetallicTexture;
            data.RoughnessTexture = createInfo.RoughnessTexture;
            data.AOTexture = createInfo.AOTexture;
            data.HeightTexture = createInfo.HeightTexture;
            data.HeightScale = createInfo.HeightScale;
            data.EmissiveColor[0] = createInfo.EmissiveColor[0];
            data.EmissiveColor[1] = createInfo.EmissiveColor[1];
            data.EmissiveColor[2] = createInfo.EmissiveColor[2];
            data.EmissiveStrength = createInfo.EmissiveStrength;
            data.Blend = createInfo.Blend;
            data.Shading = createInfo.Shading;
            data.bTwoSided = createInfo.bTwoSided;
            data.bCastShadows = createInfo.bCastShadows;
            data.RefCount = 1;
            data.DebugName = createInfo.DebugName;
            return data;
        }

        MaterialCreateData BuildMaterialCreateDataFromNeuralResource(
            const NeuralMaterialDesc &desc,
            const Container::TSharedPtr<NeuralMaterialResource> &neuralMaterial)
        {
            MaterialCreateData matInfo;
            matInfo.DebugName = desc.DebugName;

            TextureHandle albedoHandle = neuralMaterial->GetOutputTextureHandle(0);
            if (albedoHandle.IsValid())
            {
                matInfo.AlbedoTexture = albedoHandle;
            }

            if (neuralMaterial->GetOutputSlotCount() > 1)
            {
                TextureHandle normalHandle = neuralMaterial->GetOutputTextureHandle(1);
                if (normalHandle.IsValid())
                {
                    matInfo.NormalTexture = normalHandle;
                }
            }

            return matInfo;
        }
    }

    RenderMaterialStore::RenderMaterialStore(Thread::Atomic<uint64_t> &nextHandleId)
        : m_NextHandleId(nextHandleId)
    {
    }

    RenderMaterialStore::~RenderMaterialStore()
    {
        ClearUnregistered();
    }

    MaterialHandle RenderMaterialStore::AllocateMaterialHandle()
    {
        MaterialHandle handle;
        handle.Id = m_NextHandleId.FetchAdd(1, std::memory_order_relaxed);
        return handle;
    }

    MaterialHandle RenderMaterialStore::CreateMaterial(const MaterialCreateData &createInfo)
    {
        MaterialHandle handle = AllocateMaterialHandle();
        MaterialResourceData data = CopyMaterialCreateData(createInfo);

        Thread::ScopedLock lock(m_Mutex);
        m_Materials[handle.Id] = std::move(data);
        return handle;
    }

    const MaterialResourceData *RenderMaterialStore::GetMaterialData(MaterialHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Materials.find(handle.Id);
        if (it != m_Materials.end())
        {
            return &it->second;
        }

        return nullptr;
    }

    bool RenderMaterialStore::UpdateMaterial(MaterialHandle handle, const MaterialCreateData &createInfo)
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Materials.find(handle.Id);
        if (it == m_Materials.end())
        {
            return false;
        }

        MaterialResourceData data = CopyMaterialCreateData(createInfo);
        data.RefCount = it->second.RefCount;
        it->second = std::move(data);
        return true;
    }

    void RenderMaterialStore::ReleaseMaterial(MaterialHandle handle, ITextureHandleRegistrar &textureRegistrar)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Container::TSharedPtr<NeuralMaterialResource> neuralMaterial;
        {
            Thread::ScopedLock lock(m_Mutex);
            auto neuralIt = m_NeuralMaterials.find(handle.Id);
            if (neuralIt != m_NeuralMaterials.end())
            {
                neuralMaterial = std::move(neuralIt->second);
                m_NeuralMaterials.erase(neuralIt);
            }

            m_Materials.erase(handle.Id);
        }

        if (neuralMaterial)
        {
            neuralMaterial->ReleaseOutputTextures(textureRegistrar);
            neuralMaterial->Shutdown();
        }
    }

    MaterialHandle RenderMaterialStore::CreateNeuralMaterial(
        RHI::IDevice *device,
        ITextureHandleRegistrar &textureRegistrar,
        const NeuralMaterialDesc &desc)
    {
        if (!device)
        {
            return MaterialHandle::Invalid();
        }

        auto neuralMaterial = Container::MakeShared<NeuralMaterialResource>();
        if (!neuralMaterial->Initialize(device, desc))
        {
            NORVES_LOG_WARNING("RenderMaterialStore", "Failed to initialize neural material: %s",
                               desc.DebugName.c_str());
            return MaterialHandle::Invalid();
        }

        if (!neuralMaterial->RegisterOutputTextures(textureRegistrar))
        {
            NORVES_LOG_WARNING("RenderMaterialStore", "Failed to register neural material output textures: %s",
                               desc.DebugName.c_str());
            neuralMaterial->Shutdown();
            return MaterialHandle::Invalid();
        }

        MaterialHandle handle = AllocateMaterialHandle();
        MaterialResourceData materialData =
            CopyMaterialCreateData(BuildMaterialCreateDataFromNeuralResource(desc, neuralMaterial));

        {
            Thread::ScopedLock lock(m_Mutex);
            m_Materials[handle.Id] = std::move(materialData);
            m_NeuralMaterials[handle.Id] = std::move(neuralMaterial);
        }

        NORVES_LOG_INFO("RenderMaterialStore", "Neural material created: %s (handle=%llu)",
                        desc.DebugName.c_str(), static_cast<unsigned long long>(handle.Id));
        return handle;
    }

    Container::VariableArray<NeuralMaterialResource *> RenderMaterialStore::GetNeuralMaterialResources() const
    {
        Thread::ScopedLock lock(m_Mutex);
        Container::VariableArray<NeuralMaterialResource *> result;
        result.reserve(m_NeuralMaterials.size());
        for (const auto &[id, resource] : m_NeuralMaterials)
        {
            (void)id;
            if (resource && resource->IsInitialized())
            {
                result.push_back(resource.get());
            }
        }

        return result;
    }

    void RenderMaterialStore::Clear(ITextureHandleRegistrar &textureRegistrar)
    {
        Container::VariableArray<Container::TSharedPtr<NeuralMaterialResource>> neuralMaterials;

        {
            Thread::ScopedLock lock(m_Mutex);
            neuralMaterials.reserve(m_NeuralMaterials.size());
            for (auto &[id, resource] : m_NeuralMaterials)
            {
                (void)id;
                if (resource)
                {
                    neuralMaterials.push_back(std::move(resource));
                }
            }

            m_NeuralMaterials.clear();
            m_Materials.clear();
        }

        for (auto &resource : neuralMaterials)
        {
            if (resource)
            {
                resource->ReleaseOutputTextures(textureRegistrar);
                resource->Shutdown();
            }
        }
    }

    void RenderMaterialStore::ClearUnregistered()
    {
        Container::VariableArray<Container::TSharedPtr<NeuralMaterialResource>> neuralMaterials;

        {
            Thread::ScopedLock lock(m_Mutex);
            neuralMaterials.reserve(m_NeuralMaterials.size());
            for (auto &[id, resource] : m_NeuralMaterials)
            {
                (void)id;
                if (resource)
                {
                    neuralMaterials.push_back(std::move(resource));
                }
            }

            m_NeuralMaterials.clear();
            m_Materials.clear();
        }

        for (auto &resource : neuralMaterials)
        {
            if (resource)
            {
                ShutdownNeuralMaterialWithoutRegistrar(*resource);
            }
        }
    }

    void RenderMaterialStore::ShutdownNeuralMaterialWithoutRegistrar(NeuralMaterialResource &neuralMaterial)
    {
        bool bHasRegisteredTextureHandles = false;
        for (uint32_t slotIndex = 0; slotIndex < neuralMaterial.GetOutputSlotCount(); ++slotIndex)
        {
            if (neuralMaterial.GetOutputTextureHandle(slotIndex).IsValid())
            {
                bHasRegisteredTextureHandles = true;
                break;
            }
        }

        if (bHasRegisteredTextureHandles)
        {
            NORVES_LOG_ERROR("RenderMaterialStore",
                             "Neural material destroyed with registered texture handles; call Clear(registrar) first");
        }

        neuralMaterial.Shutdown();
    }

} // namespace NorvesLib::Core::Rendering
