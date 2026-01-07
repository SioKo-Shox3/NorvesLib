#include "Rendering/PersistentResourceCache.h"
#include "Resource/MeshResource.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IBuffer.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    PersistentResourceCache::~PersistentResourceCache()
    {
        Shutdown();
    }

    bool PersistentResourceCache::Initialize(RHI::IGPUResourceAllocator *allocator)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!allocator)
        {
            NORVES_LOG_ERROR("PersistentResourceCache", "Allocator is null");
            return false;
        }

        m_Allocator = allocator;
        m_bInitialized = true;

        NORVES_LOG_INFO("PersistentResourceCache", "PersistentResourceCache initialized");
        return true;
    }

    void PersistentResourceCache::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ReleaseAll();
        m_Allocator = nullptr;
        m_bInitialized = false;

        NORVES_LOG_INFO("PersistentResourceCache", "PersistentResourceCache shutdown");
    }

    void PersistentResourceCache::BeginFrame(uint64_t frameIndex)
    {
        m_CurrentFrame = frameIndex;
    }

    MeshGPUHandle PersistentResourceCache::GetOrUpload(const MeshResource *mesh)
    {
        if (!mesh || !mesh->IsLoaded())
        {
            return MeshGPUHandle{};
        }

        uint64_t resourceId = mesh->GetResourceId();

        Thread::ScopedLock lock(m_Mutex);

        // キャッシュチェック
        auto it = m_MeshCache.find(resourceId);
        if (it != m_MeshCache.end())
        {
            // 最終使用フレーム更新
            it->second.LastUsedFrame = m_CurrentFrame;

            MeshGPUHandle handle;
            handle.ResourceId = resourceId;
            handle.Data = &it->second;
            return handle;
        }

        // アップロード
        CachedMeshGPUData gpuData = UploadMesh(mesh);
        if (!gpuData.IsValid())
        {
            NORVES_LOG_ERROR("PersistentResourceCache", "Failed to upload mesh");
            return MeshGPUHandle{};
        }

        gpuData.LastUsedFrame = m_CurrentFrame;
        m_MeshCache[resourceId] = gpuData;

        MeshGPUHandle handle;
        handle.ResourceId = resourceId;
        handle.Data = &m_MeshCache[resourceId];
        return handle;
    }

    void PersistentResourceCache::Release(const MeshResource *mesh)
    {
        if (!mesh)
        {
            return;
        }

        ReleaseMesh(mesh->GetResourceId());
    }

    void PersistentResourceCache::ReleaseMesh(uint64_t resourceId)
    {
        Thread::ScopedLock lock(m_Mutex);

        auto it = m_MeshCache.find(resourceId);
        if (it == m_MeshCache.end())
        {
            return;
        }

        CachedMeshGPUData &data = it->second;

        // GPUバッファを解放
        if (m_Allocator)
        {
            if (data.VertexBuffer)
            {
                RHI::BufferAllocation allocation;
                allocation.Buffer = data.VertexBuffer;
                m_Allocator->FreeBuffer(allocation);
            }

            if (data.IndexBuffer)
            {
                RHI::BufferAllocation allocation;
                allocation.Buffer = data.IndexBuffer;
                m_Allocator->FreeBuffer(allocation);
            }
        }

        m_MeshCache.erase(it);
    }

    size_t PersistentResourceCache::ReleaseUnused(uint32_t frameThreshold)
    {
        Thread::ScopedLock lock(m_Mutex);

        Container::VariableArray<uint64_t> toRemove;

        for (auto &[resourceId, data] : m_MeshCache)
        {
            if (m_CurrentFrame - data.LastUsedFrame > frameThreshold)
            {
                toRemove.push_back(resourceId);
            }
        }

        for (uint64_t resourceId : toRemove)
        {
            auto it = m_MeshCache.find(resourceId);
            if (it != m_MeshCache.end())
            {
                CachedMeshGPUData &data = it->second;

                if (m_Allocator)
                {
                    if (data.VertexBuffer)
                    {
                        RHI::BufferAllocation allocation;
                        allocation.Buffer = data.VertexBuffer;
                        m_Allocator->FreeBuffer(allocation);
                    }

                    if (data.IndexBuffer)
                    {
                        RHI::BufferAllocation allocation;
                        allocation.Buffer = data.IndexBuffer;
                        m_Allocator->FreeBuffer(allocation);
                    }
                }

                m_MeshCache.erase(it);
            }
        }

        if (!toRemove.empty())
        {
            NORVES_LOG_DEBUG("PersistentResourceCache",
                             "Released " + Container::String(std::to_string(toRemove.size())) + " unused mesh(es)");
        }

        return toRemove.size();
    }

    void PersistentResourceCache::ReleaseAll()
    {
        Thread::ScopedLock lock(m_Mutex);

        for (auto &[resourceId, data] : m_MeshCache)
        {
            if (m_Allocator)
            {
                if (data.VertexBuffer)
                {
                    RHI::BufferAllocation allocation;
                    allocation.Buffer = data.VertexBuffer;
                    m_Allocator->FreeBuffer(allocation);
                }

                if (data.IndexBuffer)
                {
                    RHI::BufferAllocation allocation;
                    allocation.Buffer = data.IndexBuffer;
                    m_Allocator->FreeBuffer(allocation);
                }
            }
        }

        m_MeshCache.clear();
        NORVES_LOG_INFO("PersistentResourceCache", "All GPU resources released");
    }

    size_t PersistentResourceCache::GetCachedMeshCount() const
    {
        Thread::ScopedLock lock(m_Mutex);
        return m_MeshCache.size();
    }

    size_t PersistentResourceCache::GetGPUMemoryUsage() const
    {
        Thread::ScopedLock lock(m_Mutex);

        size_t total = 0;
        for (const auto &[resourceId, data] : m_MeshCache)
        {
            if (data.VertexBuffer)
            {
                total += data.VertexBuffer->GetSize();
            }
            if (data.IndexBuffer)
            {
                total += data.IndexBuffer->GetSize();
            }
        }
        return total;
    }

    CachedMeshGPUData PersistentResourceCache::UploadMesh(const MeshResource *mesh)
    {
        CachedMeshGPUData gpuData;

        if (!m_Allocator || !mesh)
        {
            return gpuData;
        }

        const auto &vertexData = mesh->GetVertexData();
        const auto &indexData = mesh->GetIndexData();

        if (vertexData.empty() || indexData.empty())
        {
            return gpuData;
        }

        // 頂点バッファ作成
        RHI::BufferDesc vbDesc;
        vbDesc.Size = vertexData.size();
        vbDesc.Usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::TransferDst;
        vbDesc.CPUAccessible = true; // 初回データ転送用

        auto vbAllocation = m_Allocator->AllocateBuffer(vbDesc, RHI::AllocationType::Dedicated);
        if (!vbAllocation.IsValid())
        {
            NORVES_LOG_ERROR("PersistentResourceCache", "Failed to allocate vertex buffer");
            return gpuData;
        }

        // 頂点データをコピー
        vbAllocation.Buffer->Update(vertexData.data(), vertexData.size(), 0);

        // インデックスバッファ作成
        RHI::BufferDesc ibDesc;
        ibDesc.Size = indexData.size() * sizeof(uint32_t);
        ibDesc.Usage = RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::TransferDst;
        ibDesc.CPUAccessible = true;

        auto ibAllocation = m_Allocator->AllocateBuffer(ibDesc, RHI::AllocationType::Dedicated);
        if (!ibAllocation.IsValid())
        {
            // 頂点バッファを解放
            m_Allocator->FreeBuffer(vbAllocation);
            NORVES_LOG_ERROR("PersistentResourceCache", "Failed to allocate index buffer");
            return gpuData;
        }

        // インデックスデータをコピー
        ibAllocation.Buffer->Update(indexData.data(), indexData.size() * sizeof(uint32_t), 0);

        gpuData.VertexBuffer = vbAllocation.Buffer;
        gpuData.IndexBuffer = ibAllocation.Buffer;
        gpuData.VertexCount = mesh->GetVertexCount();
        gpuData.IndexCount = mesh->GetIndexCount();
        gpuData.VertexStride = mesh->GetVertexLayout().Stride;

        return gpuData;
    }

} // namespace NorvesLib::Core::Rendering
