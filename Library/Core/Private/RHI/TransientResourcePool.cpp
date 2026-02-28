#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::RHI
{

    TransientResourcePool::~TransientResourcePool()
    {
        Shutdown();
    }

    bool TransientResourcePool::Initialize(IGPUResourceAllocator *allocator)
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
        m_CurrentFrame = frameIndex;
    }

    void TransientResourcePool::EndFrame()
    {
        // 使用中リソースをプールに返却
        m_UsedRenderTargets.clear();
        m_UsedBuffers.clear();
    }

    ITexture *TransientResourcePool::AcquireRenderTarget(uint32_t width, uint32_t height, Format format, const char *debugName)
    {
        // TODO: プールから再利用 or 新規作成
        (void)width;
        (void)height;
        (void)format;
        (void)debugName;
        return nullptr;
    }

    ITexture *TransientResourcePool::AcquireDepthStencil(uint32_t width, uint32_t height, Format format, const char *debugName)
    {
        // TODO: プールから再利用 or 新規作成
        (void)width;
        (void)height;
        (void)format;
        (void)debugName;
        return nullptr;
    }

    IBuffer *TransientResourcePool::AcquireBuffer(uint64_t size, ResourceUsage usage, const char *debugName)
    {
        // TODO: プールから再利用 or 新規作成
        (void)size;
        (void)usage;
        (void)debugName;
        return nullptr;
    }

    void TransientResourcePool::Trim()
    {
        // TODO: 余剰リソースの解放
    }

    void TransientResourcePool::ReleaseAll()
    {
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
