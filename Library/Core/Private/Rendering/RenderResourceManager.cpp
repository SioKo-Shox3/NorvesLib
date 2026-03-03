#include "Rendering/RenderResourceManager.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    bool RenderResourceManager::Initialize(Container::TSharedPtr<RHI::IDevice> device)
    {
        if (m_bInitialized)
        {
            return true;
        }

        m_Device = device;
        if (!m_Device)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Device is null");
            return false;
        }

        m_bInitialized = true;
        LOG_INFO("RenderResourceManager initialized");
        return true;
    }

    void RenderResourceManager::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ClearAllResources();
        m_Device.reset();
        m_bInitialized = false;
        LOG_INFO("RenderResourceManager shutdown");
    }

    // ========================================
    // バッファ操作
    // ========================================

    BufferHandle RenderResourceManager::CreateBuffer(const BufferCreateInfo &createInfo)
    {
        if (!m_bInitialized)
        {
            return BufferHandle::Invalid();
        }

        RHI::ResourceUsage usage = RHI::ResourceUsage::VertexBuffer;
        switch (createInfo.UsageType)
        {
        case BufferCreateInfo::Usage::Vertex:
            usage = RHI::ResourceUsage::VertexBuffer;
            break;
        case BufferCreateInfo::Usage::Index:
            usage = RHI::ResourceUsage::IndexBuffer;
            break;
        case BufferCreateInfo::Usage::Constant:
            usage = RHI::ResourceUsage::ConstantBuffer;
            break;
        default:
            usage = RHI::ResourceUsage::VertexBuffer;
            break;
        }

        RHI::BufferDesc desc(
            static_cast<uint64_t>(createInfo.Size),
            usage,
            createInfo.bHostVisible,
            createInfo.DebugName.c_str());

        auto buffer = m_Device->CreateBuffer(desc);
        if (!buffer)
        {
            return BufferHandle::Invalid();
        }

        auto handle = AllocateHandle<BufferHandle>();

        BufferResourceData data;
        data.RHIBuffer = buffer;
        data.Size = createInfo.Size;
        data.Usage = createInfo.UsageType;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Buffers[handle.Id] = std::move(data);

        return handle;
    }

    BufferHandle RenderResourceManager::CreateBuffer(const BufferCreateInfo &createInfo,
                                                     const void *data, size_t dataSize)
    {
        auto handle = CreateBuffer(createInfo);
        if (handle.IsValid() && data && dataSize > 0)
        {
            UpdateBuffer(handle, data, dataSize);
        }
        return handle;
    }

    bool RenderResourceManager::UpdateBuffer(BufferHandle handle, const void *data,
                                             size_t dataSize, size_t offset)
    {
        if (!handle.IsValid() || !data)
        {
            return false;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Buffers.find(handle.Id);
        if (it == m_Buffers.end() || !it->second.RHIBuffer)
        {
            return false;
        }

        it->second.RHIBuffer->Update(data, dataSize);
        return true;
    }

    void RenderResourceManager::ReleaseBuffer(BufferHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Buffers.erase(handle.Id);
    }

    // ========================================
    // テクスチャ操作
    // ========================================

    TextureHandle RenderResourceManager::CreateTexture(const TextureCreateInfo &createInfo)
    {
        // TODO: テクスチャ作成の実装
        return TextureHandle::Invalid();
    }

    TextureHandle RenderResourceManager::CreateTexture(const TextureCreateInfo &createInfo,
                                                       const void *data, size_t dataSize)
    {
        // TODO: テクスチャ作成+データ転送の実装
        return TextureHandle::Invalid();
    }

    TextureHandle RenderResourceManager::LoadTexture(const Container::String &path)
    {
        // TODO: ファイルからテクスチャロードの実装
        return TextureHandle::Invalid();
    }

    void RenderResourceManager::ReleaseTexture(TextureHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures.erase(handle.Id);
    }

    // ========================================
    // サンプラー操作
    // ========================================

    SamplerHandle RenderResourceManager::GetDefaultSampler()
    {
        // TODO: デフォルトサンプラー作成
        return m_DefaultSampler;
    }

    SamplerHandle RenderResourceManager::GetPointSampler()
    {
        // TODO: ポイントサンプラー作成
        return m_PointSampler;
    }

    void RenderResourceManager::ReleaseSampler(SamplerHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Samplers.erase(handle.Id);
    }

    // ========================================
    // シェーダー操作
    // ========================================

    ShaderHandle RenderResourceManager::CreateShader(const ShaderCreateInfo &createInfo)
    {
        // TODO: シェーダー作成の実装
        return ShaderHandle::Invalid();
    }

    ShaderHandle RenderResourceManager::LoadShader(const Container::String &path, ShaderStage stage)
    {
        // TODO: ファイルからシェーダーロードの実装
        return ShaderHandle::Invalid();
    }

    void RenderResourceManager::ReleaseShader(ShaderHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Shaders.erase(handle.Id);
    }

    // ========================================
    // 頂点レイアウト操作
    // ========================================

    VertexLayoutHandle RenderResourceManager::RegisterVertexLayout(const VertexLayout &layout)
    {
        auto handle = AllocateHandle<VertexLayoutHandle>();
        Thread::ScopedLock lock(m_ResourceMutex);
        m_VertexLayouts[handle.Id] = layout;
        return handle;
    }

    const VertexLayout *RenderResourceManager::GetVertexLayout(VertexLayoutHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_VertexLayouts.find(handle.Id);
        if (it != m_VertexLayouts.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    // ========================================
    // 内部リソースアクセス
    // ========================================

    RHI::IBuffer *RenderResourceManager::GetRHIBuffer(BufferHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Buffers.find(handle.Id);
        if (it != m_Buffers.end())
        {
            return it->second.RHIBuffer.get();
        }
        return nullptr;
    }

    RHI::ITexture *RenderResourceManager::GetRHITexture(TextureHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end())
        {
            return it->second.RHITexture.get();
        }
        return nullptr;
    }

    RHI::IShader *RenderResourceManager::GetRHIShader(ShaderHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Shaders.find(handle.Id);
        if (it != m_Shaders.end())
        {
            return it->second.RHIShader.get();
        }
        return nullptr;
    }

    // ========================================
    // メッシュ操作
    // ========================================

    bool RenderResourceManager::RegisterMesh(MeshDataHandle handle,
                                             const void *vertices, size_t vertexSize,
                                             const uint32_t *indices, uint32_t indexCount)
    {
        if (!m_bInitialized || !handle.IsValid() || !vertices || !indices || indexCount == 0)
        {
            return false;
        }

        // 既に登録済みなら上書き
        UnregisterMesh(handle);

        // 頂点バッファ作成
        RHI::BufferDesc vbDesc(
            static_cast<uint64_t>(vertexSize),
            RHI::ResourceUsage::VertexBuffer,
            true,
            "MeshVB");
        auto vertexBuffer = m_Device->CreateBuffer(vbDesc);
        if (!vertexBuffer)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create vertex buffer for mesh");
            return false;
        }
        vertexBuffer->Update(vertices, vertexSize);

        // インデックスバッファ作成
        size_t ibSize = static_cast<size_t>(indexCount) * sizeof(uint32_t);
        RHI::BufferDesc ibDesc(
            static_cast<uint64_t>(ibSize),
            RHI::ResourceUsage::IndexBuffer,
            true,
            "MeshIB");
        auto indexBuffer = m_Device->CreateBuffer(ibDesc);
        if (!indexBuffer)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create index buffer for mesh");
            return false;
        }
        indexBuffer->Update(indices, ibSize);

        // 登録
        MeshGPUData gpuData;
        gpuData.VertexBuffer = vertexBuffer;
        gpuData.IndexBuffer = indexBuffer;
        gpuData.IndexCount = indexCount;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_MeshGPUDataMap[handle.Id] = std::move(gpuData);

        NORVES_LOG_INFO("RenderResourceManager", "Mesh registered successfully");
        return true;
    }

    const RenderResourceManager::MeshGPUData *RenderResourceManager::GetMeshGPUData(MeshDataHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_MeshGPUDataMap.find(handle.Id);
        if (it != m_MeshGPUDataMap.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    void RenderResourceManager::UnregisterMesh(MeshDataHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_MeshGPUDataMap.erase(handle.Id);
    }

    // ========================================
    // リソース管理
    // ========================================

    void RenderResourceManager::CleanupUnusedResources()
    {
        // TODO: 参照カウントベースのクリーンアップ
    }

    void RenderResourceManager::ClearAllResources()
    {
        Thread::ScopedLock lock(m_ResourceMutex);

        m_MeshGPUDataMap.clear();
        m_Buffers.clear();
        m_Textures.clear();
        m_Samplers.clear();
        m_Shaders.clear();
        m_Pipelines.clear();
        m_VertexLayouts.clear();
        m_TextureCache.clear();
        m_ShaderCache.clear();
    }

    RenderResourceManager::ResourceStats RenderResourceManager::GetResourceStats() const
    {
        Thread::ScopedLock lock(m_ResourceMutex);

        ResourceStats stats;
        stats.BufferCount = static_cast<uint32_t>(m_Buffers.size());
        stats.TextureCount = static_cast<uint32_t>(m_Textures.size());
        stats.ShaderCount = static_cast<uint32_t>(m_Shaders.size());
        stats.SamplerCount = static_cast<uint32_t>(m_Samplers.size());

        for (const auto &[id, data] : m_Buffers)
        {
            stats.TotalBufferMemory += data.Size;
        }

        return stats;
    }

} // namespace NorvesLib::Core::Rendering
