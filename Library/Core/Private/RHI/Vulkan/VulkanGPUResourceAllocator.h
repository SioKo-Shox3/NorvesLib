#pragma once

#include "RHI/IGPUResourceAllocator.h"
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    using ::NorvesLib::Core::Container::VariableArray;

    class VulkanDevice;

    /**
     * @brief Vulkan GPUリソースアロケーター
     */
    class VulkanGPUResourceAllocator final : public IGPUResourceAllocator
    {
    public:
        explicit VulkanGPUResourceAllocator(VulkanDevice* device);
        ~VulkanGPUResourceAllocator() override;

        BufferAllocation AllocateBuffer(const BufferDesc& desc, AllocationType type = AllocationType::Dedicated) override;
        void FreeBuffer(BufferAllocation& allocation) override;

        TextureAllocation AllocateTexture(const TextureDesc& desc, AllocationType type = AllocationType::Dedicated) override;
        void FreeTexture(TextureAllocation& allocation) override;

        size_t GetAllocatedMemory() const override { return m_AllocatedMemory; }
        size_t GetUsedMemory() const override { return m_UsedMemory; }
        void Trim() override;

    private:
        struct BufferRecord
        {
            BufferPtr Buffer;
            BufferAllocation Allocation;
        };

        struct TextureRecord
        {
            TexturePtr Texture;
            TextureAllocation Allocation;
        };

        static size_t EstimateTextureSize(const TextureDesc& desc);

        VulkanDevice* m_Device = nullptr;
        VariableArray<BufferRecord> m_Buffers;
        VariableArray<TextureRecord> m_Textures;
        size_t m_AllocatedMemory = 0;
        size_t m_UsedMemory = 0;
    };

} // namespace NorvesLib::RHI::Vulkan
