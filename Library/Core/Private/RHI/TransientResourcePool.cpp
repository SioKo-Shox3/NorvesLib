#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::RHI
{
    namespace
    {
        RenderTargetKey MakeRenderTargetKey(const TextureDesc& desc)
        {
            RenderTargetKey key;
            key.Width = desc.Width;
            key.Height = desc.Height;
            key.Depth = desc.Depth;
            key.MipLevels = desc.MipLevels;
            key.ArraySize = desc.ArraySize;
            key.Dimension = desc.Dimension;
            key.IsCubemap = desc.IsCubemap;
            key.Format = desc.TextureFormat;
            key.Usage = desc.Usage;
            return key;
        }
    } // namespace

    TransientResourcePool::~TransientResourcePool()
    {
        Shutdown();
    }

    bool TransientResourcePool::Initialize(IGPUResourceAllocator* allocator, uint32_t framesInFlight)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!allocator)
        {
            return false;
        }

        m_Allocator = allocator;
        m_FramesInFlight = framesInFlight > 0 ? framesInFlight : 1;
        m_CurrentFrameSlot = 0;
        m_CurrentSerial = 0;
        m_bInitialized = true;
        return true;
    }

    void TransientResourcePool::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ReleaseAll();
        m_Allocator = nullptr;
        m_bInitialized = false;
    }

    void TransientResourcePool::BeginFrame(uint64_t frameIndex)
    {
        m_CurrentFrameSlot = static_cast<uint32_t>(frameIndex % m_FramesInFlight);
        ++m_CurrentSerial;
    }

    void TransientResourcePool::EndFrame()
    {
        for (auto& resource : m_UsedRenderTargets)
        {
            resource.LastUsedFrameSlot = m_CurrentFrameSlot;
            resource.LastUsedSerial = m_CurrentSerial;
            m_RTPool[resource.TextureKey].push_back(resource);
        }

        for (auto& resource : m_UsedBuffers)
        {
            resource.LastUsedFrameSlot = m_CurrentFrameSlot;
            resource.LastUsedSerial = m_CurrentSerial;
            m_BufferPool[resource.BufferKeyValue].push_back(resource);
        }

        m_UsedRenderTargets.clear();
        m_UsedBuffers.clear();
    }

    ITexture* TransientResourcePool::AcquireTexture(const TextureDesc& desc)
    {
        if (!m_bInitialized || !m_Allocator)
        {
            return nullptr;
        }

        RenderTargetKey key = MakeRenderTargetKey(desc);

        auto it = m_RTPool.find(key);
        if (it != m_RTPool.end())
        {
            auto& resources = it->second;
            for (auto resourceIt = resources.begin(); resourceIt != resources.end(); ++resourceIt)
            {
                if (resourceIt->LastUsedFrameSlot == m_CurrentFrameSlot &&
                    resourceIt->LastUsedSerial < m_CurrentSerial &&
                    resourceIt->Texture.IsValid())
                {
                    PooledResource<ITexture> resource = *resourceIt;
                    resources.erase(resourceIt);
                    ITexture* texture = resource.Texture.Texture;
                    m_UsedRenderTargets.push_back(resource);
                    return texture;
                }
            }
        }

        TextureAllocation allocation = m_Allocator->AllocateTexture(desc, AllocationType::Transient);
        if (!allocation.IsValid())
        {
            return nullptr;
        }

        if (allocation.Size == 0)
        {
            allocation.Size = EstimateTextureSize(desc);
        }

        PooledResource<ITexture> resource;
        resource.Resource = allocation.Texture;
        resource.Texture = allocation;
        resource.TextureKey = key;
        resource.Size = allocation.Size;
        m_UsedRenderTargets.push_back(resource);
        return allocation.Texture;
    }

    ITexture* TransientResourcePool::AcquireRenderTarget(uint32_t width, uint32_t height, Format format, const char* debugName)
    {
        TextureDesc desc = TextureDesc::RenderTarget(width, height, format, debugName);
        return AcquireTexture(desc);
    }

    ITexture* TransientResourcePool::AcquireDepthStencil(uint32_t width, uint32_t height, Format format, const char* debugName)
    {
        TextureDesc desc = TextureDesc::DepthStencil(width, height, format, debugName);
        return AcquireTexture(desc);
    }

    IBuffer* TransientResourcePool::AcquireBuffer(uint64_t size, ResourceUsage usage, const char* debugName)
    {
        if (!m_bInitialized || !m_Allocator)
        {
            return nullptr;
        }

        BufferKey key;
        key.Size = size;
        key.Usage = usage;
        key.CPUAccessible = false;

        auto it = m_BufferPool.find(key);
        if (it != m_BufferPool.end())
        {
            auto& resources = it->second;
            for (auto resourceIt = resources.begin(); resourceIt != resources.end(); ++resourceIt)
            {
                if (resourceIt->LastUsedFrameSlot == m_CurrentFrameSlot &&
                    resourceIt->LastUsedSerial < m_CurrentSerial &&
                    resourceIt->Buffer.IsValid())
                {
                    PooledResource<IBuffer> resource = *resourceIt;
                    resources.erase(resourceIt);
                    IBuffer* buffer = resource.Buffer.Buffer;
                    m_UsedBuffers.push_back(resource);
                    return buffer;
                }
            }
        }

        BufferDesc desc(size, usage, false, debugName);
        BufferAllocation allocation = m_Allocator->AllocateBuffer(desc, AllocationType::Transient);
        if (!allocation.IsValid())
        {
            return nullptr;
        }

        if (allocation.Size == 0)
        {
            allocation.Size = size;
        }

        PooledResource<IBuffer> resource;
        resource.Resource = allocation.Buffer;
        resource.Buffer = allocation;
        resource.BufferKeyValue = key;
        resource.Size = static_cast<size_t>(allocation.Size);
        m_UsedBuffers.push_back(resource);
        return allocation.Buffer;
    }

    void TransientResourcePool::Trim()
    {
        if (!m_Allocator)
        {
            return;
        }

        size_t memoryUsage = GetPoolMemoryUsage();
        if (memoryUsage <= m_MaxPoolMemory)
        {
            return;
        }

        for (auto& [key, resources] : m_RTPool)
        {
            for (auto it = resources.begin(); it != resources.end() && memoryUsage > m_MaxPoolMemory;)
            {
                if (it->LastUsedFrameSlot == m_CurrentFrameSlot &&
                    it->LastUsedSerial < m_CurrentSerial)
                {
                    memoryUsage -= it->Size;
                    m_Allocator->FreeTexture(it->Texture);
                    it = resources.erase(it);
                    continue;
                }
                ++it;
            }
        }

        for (auto& [key, resources] : m_BufferPool)
        {
            for (auto it = resources.begin(); it != resources.end() && memoryUsage > m_MaxPoolMemory;)
            {
                if (it->LastUsedFrameSlot == m_CurrentFrameSlot &&
                    it->LastUsedSerial < m_CurrentSerial)
                {
                    memoryUsage -= it->Size;
                    m_Allocator->FreeBuffer(it->Buffer);
                    it = resources.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }

    void TransientResourcePool::ReleaseAll()
    {
        if (m_Allocator)
        {
            for (auto& [key, resources] : m_RTPool)
            {
                for (auto& resource : resources)
                {
                    m_Allocator->FreeTexture(resource.Texture);
                }
            }

            for (auto& [key, resources] : m_BufferPool)
            {
                for (auto& resource : resources)
                {
                    m_Allocator->FreeBuffer(resource.Buffer);
                }
            }

            for (auto& resource : m_UsedRenderTargets)
            {
                m_Allocator->FreeTexture(resource.Texture);
            }

            for (auto& resource : m_UsedBuffers)
            {
                m_Allocator->FreeBuffer(resource.Buffer);
            }
        }

        m_RTPool.clear();
        m_BufferPool.clear();
        m_UsedRenderTargets.clear();
        m_UsedBuffers.clear();
    }

    size_t TransientResourcePool::GetPooledRenderTargetCount() const
    {
        size_t count = 0;
        for (const auto &[key, resources] : m_RTPool)
        {
            count += resources.size();
        }
        return count;
    }

    size_t TransientResourcePool::GetPooledBufferCount() const
    {
        size_t count = 0;
        for (const auto &[size, resources] : m_BufferPool)
        {
            count += resources.size();
        }
        return count;
    }

    size_t TransientResourcePool::GetPoolMemoryUsage() const
    {
        size_t total = 0;
        for (const auto &[key, resources] : m_RTPool)
        {
            for (const auto &res : resources)
            {
                total += res.Size;
            }
        }
        for (const auto &[size, resources] : m_BufferPool)
        {
            for (const auto &res : resources)
            {
                total += res.Size;
            }
        }
        return total;
    }

} // namespace NorvesLib::RHI
