#include "VulkanGPUResourceAllocator.h"
#include "VulkanDevice.h"

namespace NorvesLib::RHI::Vulkan
{
    namespace
    {
        size_t GetFormatBytesPerPixel(Format format)
        {
            switch (format)
            {
            case Format::R8_UNORM:
                return 1;
            case Format::R8G8_UNORM:
                return 2;
            case Format::R8G8B8A8_UNORM:
            case Format::R8G8B8A8_SRGB:
            case Format::B8G8R8A8_UNORM:
            case Format::B8G8R8A8_SRGB:
            case Format::R32_FLOAT:
            case Format::D24_UNORM_S8_UINT:
            case Format::D32_FLOAT:
                return 4;
            case Format::R16_FLOAT:
            case Format::D16_UNORM:
                return 2;
            case Format::R16G16_FLOAT:
                return 4;
            case Format::R16G16B16A16_FLOAT:
                return 8;
            case Format::R32G32_FLOAT:
                return 8;
            case Format::R32G32B32_FLOAT:
                return 12;
            case Format::R32G32B32A32_FLOAT:
                return 16;
            default:
                return 4;
            }
        }
    } // namespace

    VulkanGPUResourceAllocator::VulkanGPUResourceAllocator(VulkanDevice* device)
        : m_Device(device)
    {
    }

    VulkanGPUResourceAllocator::~VulkanGPUResourceAllocator()
    {
        m_Buffers.clear();
        m_Textures.clear();
        m_AllocatedMemory = 0;
        m_UsedMemory = 0;
    }

    BufferAllocation VulkanGPUResourceAllocator::AllocateBuffer(const BufferDesc& desc, AllocationType type)
    {
        if (!m_Device)
        {
            return {};
        }

        BufferPtr buffer = m_Device->CreateBuffer(desc);
        if (!buffer)
        {
            return {};
        }

        BufferAllocation allocation;
        allocation.Buffer = buffer.get();
        allocation.Offset = 0;
        allocation.Size = desc.Size;
        allocation.Type = type;

        BufferRecord record;
        record.Buffer = buffer;
        record.Allocation = allocation;
        m_Buffers.push_back(record);

        m_AllocatedMemory += static_cast<size_t>(allocation.Size);
        m_UsedMemory += static_cast<size_t>(allocation.Size);
        return allocation;
    }

    void VulkanGPUResourceAllocator::FreeBuffer(BufferAllocation& allocation)
    {
        if (!allocation.IsValid())
        {
            return;
        }

        for (auto it = m_Buffers.begin(); it != m_Buffers.end(); ++it)
        {
            if (it->Allocation.Buffer == allocation.Buffer)
            {
                m_AllocatedMemory -= static_cast<size_t>(it->Allocation.Size);
                m_UsedMemory -= static_cast<size_t>(it->Allocation.Size);
                it = m_Buffers.erase(it);
                allocation = {};
                return;
            }
        }
    }

    TextureAllocation VulkanGPUResourceAllocator::AllocateTexture(const TextureDesc& desc, AllocationType type)
    {
        if (!m_Device)
        {
            return {};
        }

        TexturePtr texture = m_Device->CreateTexture(desc);
        if (!texture)
        {
            return {};
        }

        TextureAllocation allocation;
        allocation.Texture = texture.get();
        allocation.Size = EstimateTextureSize(desc);
        allocation.Type = type;

        TextureRecord record;
        record.Texture = texture;
        record.Allocation = allocation;
        m_Textures.push_back(record);

        m_AllocatedMemory += allocation.Size;
        m_UsedMemory += allocation.Size;
        return allocation;
    }

    void VulkanGPUResourceAllocator::FreeTexture(TextureAllocation& allocation)
    {
        if (!allocation.IsValid())
        {
            return;
        }

        for (auto it = m_Textures.begin(); it != m_Textures.end(); ++it)
        {
            if (it->Allocation.Texture == allocation.Texture)
            {
                m_AllocatedMemory -= it->Allocation.Size;
                m_UsedMemory -= it->Allocation.Size;
                it = m_Textures.erase(it);
                allocation = {};
                return;
            }
        }
    }

    void VulkanGPUResourceAllocator::Trim()
    {
    }

    size_t VulkanGPUResourceAllocator::EstimateTextureSize(const TextureDesc& desc)
    {
        return static_cast<size_t>(desc.Width) *
               static_cast<size_t>(desc.Height) *
               static_cast<size_t>(desc.Depth) *
               static_cast<size_t>(desc.ArraySize) *
               static_cast<size_t>(GetFormatBytesPerPixel(desc.TextureFormat));
    }

} // namespace NorvesLib::RHI::Vulkan
