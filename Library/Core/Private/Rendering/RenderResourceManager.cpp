#include "Rendering/RenderResourceManager.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"
#include "FileStream/FileStream.h"

// stb_image（テクスチャファイル読み込み用）
#include "stb_image.h"

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
        case BufferCreateInfo::Usage::Structured:
            usage = RHI::ResourceUsage::ShaderRead;
            break;
        case BufferCreateInfo::Usage::Storage:
            usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
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
        if (!m_bInitialized)
        {
            return TextureHandle::Invalid();
        }

        // TextureCreateInfo::Format → RHI::Format 変換
        RHI::Format rhiFormat = RHI::Format::R8G8B8A8_UNORM;
        switch (createInfo.PixelFormat)
        {
        case TextureCreateInfo::Format::RGBA8_UNORM:
            rhiFormat = RHI::Format::R8G8B8A8_UNORM;
            break;
        case TextureCreateInfo::Format::RGBA8_SRGB:
            rhiFormat = RHI::Format::R8G8B8A8_SRGB;
            break;
        case TextureCreateInfo::Format::RGBA16_FLOAT:
            rhiFormat = RHI::Format::R16G16B16A16_FLOAT;
            break;
        case TextureCreateInfo::Format::RGBA32_FLOAT:
            rhiFormat = RHI::Format::R32G32B32A32_FLOAT;
            break;
        case TextureCreateInfo::Format::R8_UNORM:
            rhiFormat = RHI::Format::R8_UNORM;
            break;
        case TextureCreateInfo::Format::RG8_UNORM:
            rhiFormat = RHI::Format::R8G8_UNORM;
            break;
        case TextureCreateInfo::Format::D24_S8:
            rhiFormat = RHI::Format::D24_UNORM_S8_UINT;
            break;
        case TextureCreateInfo::Format::D32_FLOAT:
            rhiFormat = RHI::Format::D32_FLOAT;
            break;
        }

        RHI::TextureDesc desc;
        desc.Width = createInfo.Width;
        desc.Height = createInfo.Height;
        desc.Depth = createInfo.Depth;
        desc.MipLevels = createInfo.MipLevels;
        desc.ArraySize = createInfo.ArraySize;
        desc.TextureFormat = rhiFormat;
        desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        desc.DebugName = createInfo.DebugName.c_str();

        if (createInfo.bRenderTarget)
        {
            desc.Usage = desc.Usage | RHI::ResourceUsage::RenderTarget;
        }
        if (createInfo.bDepthStencil)
        {
            desc.Usage = desc.Usage | RHI::ResourceUsage::DepthStencil;
        }

        auto rhiTexture = m_Device->CreateTexture(desc);
        if (!rhiTexture)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create texture");
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = rhiTexture;
        data.Width = createInfo.Width;
        data.Height = createInfo.Height;
        data.Format = createInfo.PixelFormat;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures[handle.Id] = std::move(data);

        return handle;
    }

    TextureHandle RenderResourceManager::CreateTexture(const TextureCreateInfo &createInfo,
                                                       const void *data, size_t dataSize)
    {
        auto handle = CreateTexture(createInfo);
        if (!handle.IsValid() || !data || dataSize == 0)
        {
            return handle;
        }

        // テクスチャにデータをアップロード
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end() && it->second.RHITexture)
        {
            uint32_t bytesPerPixel = 4; // RGBA8のデフォルト
            switch (createInfo.PixelFormat)
            {
            case TextureCreateInfo::Format::R8_UNORM:
                bytesPerPixel = 1;
                break;
            case TextureCreateInfo::Format::RG8_UNORM:
                bytesPerPixel = 2;
                break;
            case TextureCreateInfo::Format::RGBA8_UNORM:
            case TextureCreateInfo::Format::RGBA8_SRGB:
                bytesPerPixel = 4;
                break;
            case TextureCreateInfo::Format::RGBA16_FLOAT:
                bytesPerPixel = 8;
                break;
            case TextureCreateInfo::Format::RGBA32_FLOAT:
                bytesPerPixel = 16;
                break;
            default:
                bytesPerPixel = 4;
                break;
            }

            uint32_t rowPitch = createInfo.Width * bytesPerPixel;
            uint32_t slicePitch = rowPitch * createInfo.Height;
            it->second.RHITexture->Update(data, rowPitch, slicePitch);
        }

        return handle;
    }

    Container::String RenderResourceManager::ResolveTexturePath(const Container::String &path) const
    {
        Container::String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
        // 相対パスの場合（ドライブレター/UNCパスでない場合）NORVES_ASSET_DIRをベースにする
        if (path.size() > 0 && path[0] != '/' && path[0] != '\\' &&
            (path.size() < 2 || path[1] != ':'))
        {
            // "Assets/" プレフィックスを除去してNORVES_ASSET_DIRに結合
            Container::String relativePath = path;
            if (relativePath.size() > 7)
            {
                Container::String prefix = relativePath.substr(0, 7);
                if (prefix == "Assets/" || prefix == "Assets\\")
                {
                    relativePath = relativePath.substr(7);
                }
            }
            resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + relativePath;
        }
#endif
        return resolvedPath;
    }

    TextureHandle RenderResourceManager::LoadTexture(const Container::String &path)
    {
        if (!m_bInitialized)
        {
            return TextureHandle::Invalid();
        }

        Container::String resolvedPath = ResolveTexturePath(path);

        // キャッシュチェック
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            auto it = m_TextureCache.find(resolvedPath);
            if (it != m_TextureCache.end())
            {
                return it->second;
            }
        }

        // stb_imageでファイルを読み込み
        int width = 0, height = 0, channels = 0;
        unsigned char *pixels = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4); // RGBA強制
        if (!pixels)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to load texture file");
            NORVES_LOG_ERROR("RenderResourceManager", resolvedPath.c_str());
            return TextureHandle::Invalid();
        }

        TextureCreateInfo createInfo;
        createInfo.Width = static_cast<uint32_t>(width);
        createInfo.Height = static_cast<uint32_t>(height);
        createInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        createInfo.DebugName = path;

        size_t dataSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        auto handle = CreateTexture(createInfo, pixels, dataSize);

        stbi_image_free(pixels);

        if (handle.IsValid())
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            m_TextureCache[resolvedPath] = handle;
            NORVES_LOG_INFO("RenderResourceManager", "Texture loaded successfully");
        }

        return handle;
    }

    TextureHandle RenderResourceManager::RegisterExternalTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const Container::String &debugName)
    {
        if (!m_bInitialized || !rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = rhiTexture;
        data.Width = 0; // 外部テクスチャのため詳細不明（必要に応じて拡張）
        data.Height = 0;
        data.Format = TextureCreateInfo::Format::RGBA8_UNORM; // デフォルト
        data.RefCount = 1;
        data.DebugName = debugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures[handle.Id] = std::move(data);

        NORVES_LOG_DEBUG("RenderResourceManager", "External texture registered: %s (handle=%llu)",
                         debugName.c_str(), handle.Id);

        return handle;
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
        if (m_DefaultSampler.IsValid())
        {
            return m_DefaultSampler;
        }

        // Linear + Wrap サンプラーを作成
        RHI::SamplerDesc desc;
        desc.filterMin = RHI::FilterMode::Linear;
        desc.filterMag = RHI::FilterMode::Linear;
        desc.filterMip = RHI::FilterMode::Linear;
        desc.addressU = RHI::TextureAddressMode::Wrap;
        desc.addressV = RHI::TextureAddressMode::Wrap;
        desc.addressW = RHI::TextureAddressMode::Wrap;
        desc.maxAnisotropy = 4;

        auto rhiSampler = m_Device->CreateSampler(desc);
        if (!rhiSampler)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create default sampler");
            return SamplerHandle::Invalid();
        }

        m_DefaultSampler = AllocateHandle<SamplerHandle>();

        SamplerResourceData data;
        data.RHISampler = rhiSampler;
        data.RefCount = 1;
        data.DebugName = "DefaultSampler";

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Samplers[m_DefaultSampler.Id] = std::move(data);

        return m_DefaultSampler;
    }

    SamplerHandle RenderResourceManager::GetPointSampler()
    {
        if (m_PointSampler.IsValid())
        {
            return m_PointSampler;
        }

        // Point + Clamp サンプラーを作成
        RHI::SamplerDesc desc;
        desc.filterMin = RHI::FilterMode::Point;
        desc.filterMag = RHI::FilterMode::Point;
        desc.filterMip = RHI::FilterMode::Point;
        desc.addressU = RHI::TextureAddressMode::Clamp;
        desc.addressV = RHI::TextureAddressMode::Clamp;
        desc.addressW = RHI::TextureAddressMode::Clamp;

        auto rhiSampler = m_Device->CreateSampler(desc);
        if (!rhiSampler)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create point sampler");
            return SamplerHandle::Invalid();
        }

        m_PointSampler = AllocateHandle<SamplerHandle>();

        SamplerResourceData data;
        data.RHISampler = rhiSampler;
        data.RefCount = 1;
        data.DebugName = "PointSampler";

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Samplers[m_PointSampler.Id] = std::move(data);

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

    Container::TSharedPtr<RHI::ITexture> RenderResourceManager::GetRHITexturePtr(TextureHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end())
        {
            return it->second.RHITexture;
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

        // ニューラルマテリアルはGPUリソースを持つため、先にShutdownしてから解放
        for (auto &[id, resource] : m_NeuralMaterials)
        {
            if (resource)
            {
                resource->Shutdown();
            }
        }
        m_NeuralMaterials.clear();

        m_MeshGPUDataMap.clear();
        m_Materials.clear();
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

    // ========================================
    // マテリアル操作
    // ========================================

    MaterialHandle RenderResourceManager::CreateMaterial(const MaterialCreateData &createInfo)
    {
        auto handle = AllocateHandle<MaterialHandle>();

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

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Materials[handle.Id] = std::move(data);

        return handle;
    }

    const MaterialResourceData *RenderResourceManager::GetMaterialData(MaterialHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Materials.find(handle.Id);
        if (it != m_Materials.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool RenderResourceManager::UpdateMaterial(MaterialHandle handle, const MaterialCreateData &createInfo)
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Materials.find(handle.Id);
        if (it == m_Materials.end())
        {
            return false;
        }

        auto &data = it->second;
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
        data.DebugName = createInfo.DebugName;

        return true;
    }

    void RenderResourceManager::ReleaseMaterial(MaterialHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_NeuralMaterials.erase(handle.Id);
        m_Materials.erase(handle.Id);
    }

    // ========================================
    // ニューラルマテリアル操作
    // ========================================

    MaterialHandle RenderResourceManager::CreateNeuralMaterial(const NeuralMaterialDesc &desc)
    {
        if (!m_bInitialized || !m_Device)
        {
            return MaterialHandle::Invalid();
        }

        auto neuralMat = MakeShared<NeuralMaterialResource>();
        if (!neuralMat->Initialize(m_Device.get(), desc))
        {
            NORVES_LOG_WARNING("RenderResourceManager", "Failed to initialize neural material: %s",
                               desc.DebugName.c_str());
            return MaterialHandle::Invalid();
        }

        if (!neuralMat->RegisterOutputTextures(*this))
        {
            NORVES_LOG_WARNING("RenderResourceManager", "Failed to register neural material output textures: %s",
                               desc.DebugName.c_str());
            return MaterialHandle::Invalid();
        }

        // 出力TextureHandleでマテリアルを作成
        MaterialCreateData matInfo;
        matInfo.DebugName = desc.DebugName;

        // Albedoスロット(0)
        TextureHandle albedoHandle = neuralMat->GetOutputTextureHandle(0);
        if (albedoHandle.IsValid())
        {
            matInfo.AlbedoTexture = albedoHandle;
        }

        // Normalスロット(1)
        if (neuralMat->GetOutputSlotCount() > 1)
        {
            TextureHandle normalHandle = neuralMat->GetOutputTextureHandle(1);
            if (normalHandle.IsValid())
            {
                matInfo.NormalTexture = normalHandle;
            }
        }

        MaterialHandle handle = CreateMaterial(matInfo);
        if (!handle.IsValid())
        {
            return MaterialHandle::Invalid();
        }

        // 内部でNeuralMaterialResourceを保持
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            m_NeuralMaterials[handle.Id] = std::move(neuralMat);
        }

        NORVES_LOG_INFO("RenderResourceManager", "Neural material created: %s (handle=%llu)",
                        desc.DebugName.c_str(), handle.Id);
        return handle;
    }

    Container::VariableArray<NeuralMaterialResource *> RenderResourceManager::GetNeuralMaterialResources() const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        Container::VariableArray<NeuralMaterialResource *> result;
        result.reserve(m_NeuralMaterials.size());
        for (const auto &[id, resource] : m_NeuralMaterials)
        {
            if (resource && resource->IsInitialized())
            {
                result.push_back(resource.get());
            }
        }
        return result;
    }

    // ========================================
    // 非同期テクスチャ読み込み
    // ========================================

    uint32_t RenderResourceManager::LoadTextureAsync(const Container::String &path,
                                                     NorvesLib::Core::Delegate<void, TextureHandle> callback)
    {
        if (!m_bInitialized)
        {
            return 0;
        }

        Container::String resolvedPath = ResolveTexturePath(path);

        // キャッシュチェック（既に読み込み済みなら即コールバック）
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            auto it = m_TextureCache.find(resolvedPath);
            if (it != m_TextureCache.end())
            {
                callback.InvokeIfBound(it->second);
                return 0; // 即完了のためリクエストIDは不要
            }
        }

        auto request = Container::MakeShared<AsyncTextureRequest>();
        request->RequestId = m_NextAsyncRequestId.FetchAdd(1, std::memory_order_relaxed);
        request->Path = path;
        request->Callback = std::move(callback);
        request->Result.Path = path;
        request->Result.ResolvedPath = resolvedPath;

        // ファイルI/O + デコードをワーカースレッドで実行するタスクを作成
        auto taskFunction = [request]()
        {
            auto &result = request->Result;

            // FileStreamでバイナリ読み込み
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                result.ResolvedPath,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);

            if (!fileStream || !fileStream->IsOpen())
            {
                result.bSuccess = false;
                return;
            }

            int64_t fileSize = fileStream->GetSize();
            if (fileSize <= 0)
            {
                result.bSuccess = false;
                return;
            }

            // ファイル全体をメモリに読み込み
            Container::VariableArray<uint8_t> fileData(static_cast<size_t>(fileSize));
            size_t bytesRead = fileStream->Read(fileData.data(), static_cast<size_t>(fileSize));
            fileStream->Close();

            if (bytesRead != static_cast<size_t>(fileSize))
            {
                result.bSuccess = false;
                return;
            }

            // stbi_load_from_memoryでデコード（ワーカースレッドで実行）
            int width = 0, height = 0, channels = 0;
            unsigned char *pixels = stbi_load_from_memory(
                fileData.data(),
                static_cast<int>(fileData.size()),
                &width, &height, &channels, 4); // RGBA強制

            if (!pixels)
            {
                result.bSuccess = false;
                return;
            }

            // デコード結果をリクエストに保存
            size_t pixelDataSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
            result.PixelData.resize(pixelDataSize);
            std::memcpy(result.PixelData.data(), pixels, pixelDataSize);
            stbi_image_free(pixels);

            result.CreateInfo.Width = static_cast<uint32_t>(width);
            result.CreateInfo.Height = static_cast<uint32_t>(height);
            result.CreateInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            result.CreateInfo.DebugName = result.Path;
            result.bSuccess = true;
        };

        request->Task = Thread::Task::Create(taskFunction, Thread::TaskPriority::NORMAL);

        // ペンディングリストに追加
        {
            Thread::ScopedLock lock(m_AsyncLoadMutex);
            m_PendingTextureLoads.push_back(request);
        }

        // JobSystemに投入
        Thread::JobSystem::Get().SubmitTask(request->Task);

        NORVES_LOG_INFO("RenderResourceManager", "Async texture load started: %s (RequestId=%u)",
                        path.c_str(), static_cast<unsigned int>(request->RequestId));

        return request->RequestId;
    }

    uint32_t RenderResourceManager::FlushCompletedTextureLoads()
    {
        Thread::ScopedLock lock(m_AsyncLoadMutex);

        uint32_t processedCount = 0;

        // 完了したリクエストを処理（後ろから走査して削除）
        for (auto it = m_PendingTextureLoads.begin(); it != m_PendingTextureLoads.end();)
        {
            auto &request = *it;

            if (!request->Task->IsCompleted())
            {
                ++it;
                continue;
            }

            auto &result = request->Result;

            if (result.bSuccess)
            {
                // メインスレッドでGPUアップロード
                auto handle = CreateTexture(
                    result.CreateInfo,
                    result.PixelData.data(),
                    result.PixelData.size());

                if (handle.IsValid())
                {
                    // キャッシュ登録
                    {
                        Thread::ScopedLock resourceLock(m_ResourceMutex);
                        m_TextureCache[result.ResolvedPath] = handle;
                    }

                    NORVES_LOG_INFO("RenderResourceManager", "Async texture loaded: %s",
                                    result.Path.c_str());
                }
                else
                {
                    NORVES_LOG_ERROR("RenderResourceManager", "Async texture GPU upload failed: %s",
                                     result.Path.c_str());
                }

                // コールバック実行
                request->Callback.InvokeIfBound(handle);
            }
            else
            {
                NORVES_LOG_ERROR("RenderResourceManager", "Async texture load failed: %s",
                                 result.ResolvedPath.c_str());

                // 失敗コールバック
                request->Callback.InvokeIfBound(TextureHandle::Invalid());
            }

            // ピクセルデータ解放（GPUに転送済み）
            result.PixelData.clear();
            result.PixelData.shrink_to_fit();

            it = m_PendingTextureLoads.erase(it);
            ++processedCount;
        }

        return processedCount;
    }

    uint32_t RenderResourceManager::GetPendingAsyncLoadCount() const
    {
        Thread::ScopedLock lock(m_AsyncLoadMutex);
        return static_cast<uint32_t>(m_PendingTextureLoads.size());
    }

} // namespace NorvesLib::Core::Rendering
