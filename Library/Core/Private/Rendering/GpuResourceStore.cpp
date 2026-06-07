#include "Rendering/GpuResourceStore.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IShader.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        using StoreClock = std::chrono::steady_clock;

        StoreClock::time_point StoreNow()
        {
            return StoreClock::now();
        }

        double StoreElapsedMs(StoreClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(StoreClock::now() - startTime).count();
        }

        RHI::ResourceUsage ToRHIResourceUsage(BufferCreateInfo::Usage usageType)
        {
            switch (usageType)
            {
            case BufferCreateInfo::Usage::Vertex:
                return RHI::ResourceUsage::VertexBuffer;
            case BufferCreateInfo::Usage::Index:
                return RHI::ResourceUsage::IndexBuffer;
            case BufferCreateInfo::Usage::Constant:
                return RHI::ResourceUsage::ConstantBuffer;
            case BufferCreateInfo::Usage::Structured:
                return RHI::ResourceUsage::ShaderRead;
            case BufferCreateInfo::Usage::Storage:
                return RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
            default:
                return RHI::ResourceUsage::VertexBuffer;
            }
        }

        RHI::Format ToRHITextureFormat(TextureCreateInfo::Format format)
        {
            switch (format)
            {
            case TextureCreateInfo::Format::RGBA8_UNORM:
                return RHI::Format::R8G8B8A8_UNORM;
            case TextureCreateInfo::Format::RGBA8_SRGB:
                return RHI::Format::R8G8B8A8_SRGB;
            case TextureCreateInfo::Format::RGBA16_FLOAT:
                return RHI::Format::R16G16B16A16_FLOAT;
            case TextureCreateInfo::Format::RGBA32_FLOAT:
                return RHI::Format::R32G32B32A32_FLOAT;
            case TextureCreateInfo::Format::R8_UNORM:
                return RHI::Format::R8_UNORM;
            case TextureCreateInfo::Format::RG8_UNORM:
                return RHI::Format::R8G8_UNORM;
            case TextureCreateInfo::Format::D24_S8:
                return RHI::Format::D24_UNORM_S8_UINT;
            case TextureCreateInfo::Format::D32_FLOAT:
                return RHI::Format::D32_FLOAT;
            default:
                return RHI::Format::R8G8B8A8_UNORM;
            }
        }

        uint32_t GetTextureBytesPerPixel(TextureCreateInfo::Format format)
        {
            switch (format)
            {
            case TextureCreateInfo::Format::R8_UNORM:
                return 1;
            case TextureCreateInfo::Format::RG8_UNORM:
                return 2;
            case TextureCreateInfo::Format::RGBA8_UNORM:
            case TextureCreateInfo::Format::RGBA8_SRGB:
                return 4;
            case TextureCreateInfo::Format::RGBA16_FLOAT:
                return 8;
            case TextureCreateInfo::Format::RGBA32_FLOAT:
                return 16;
            default:
                return 4;
            }
        }
    }

    GpuResourceStore::GpuResourceStore(Container::TSharedPtr<RHI::IDevice> device,
                                       Thread::Atomic<uint64_t> &nextHandleId)
        : m_Device(std::move(device)),
          m_NextHandleId(nextHandleId)
    {
    }

    GpuResourceStore::~GpuResourceStore() = default;

    BufferHandle GpuResourceStore::CreateBuffer(const BufferCreateInfo &createInfo)
    {
        if (!m_Device)
        {
            return BufferHandle::Invalid();
        }

        RHI::BufferDesc desc(
            static_cast<uint64_t>(createInfo.Size),
            ToRHIResourceUsage(createInfo.UsageType),
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

        Thread::ScopedLock lock(m_Mutex);
        m_Buffers[handle.Id] = std::move(data);

        return handle;
    }

    BufferHandle GpuResourceStore::CreateBuffer(const BufferCreateInfo &createInfo,
                                                const void *data,
                                                size_t dataSize)
    {
        auto handle = CreateBuffer(createInfo);
        if (handle.IsValid() && data && dataSize > 0)
        {
            UpdateBuffer(handle, data, dataSize);
        }
        return handle;
    }

    bool GpuResourceStore::UpdateBuffer(BufferHandle handle,
                                        const void *data,
                                        size_t dataSize,
                                        size_t offset)
    {
        (void)offset;

        if (!handle.IsValid() || !data)
        {
            return false;
        }

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Buffers.find(handle.Id);
        if (it == m_Buffers.end() || !it->second.RHIBuffer)
        {
            return false;
        }

        it->second.RHIBuffer->Update(data, dataSize);
        return true;
    }

    void GpuResourceStore::ReleaseBuffer(BufferHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        m_Buffers.erase(handle.Id);
    }

    TextureHandle GpuResourceStore::CreateTexture(const TextureCreateInfo &createInfo)
    {
        if (!m_Device)
        {
            return TextureHandle::Invalid();
        }

        uint32_t mipLevels = std::max(1u, createInfo.MipLevels);

        RHI::TextureDesc desc;
        desc.Width = createInfo.Width;
        desc.Height = createInfo.Height;
        desc.Depth = createInfo.Depth;
        desc.MipLevels = mipLevels;
        desc.ArraySize = createInfo.ArraySize;
        desc.TextureFormat = ToRHITextureFormat(createInfo.PixelFormat);
        desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        if (mipLevels > 1)
        {
            desc.Usage = desc.Usage | RHI::ResourceUsage::TransferSrc;
        }
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

        Thread::ScopedLock lock(m_Mutex);
        m_Textures[handle.Id] = std::move(data);

        return handle;
    }

    TextureHandle GpuResourceStore::CreateTexture(const TextureCreateInfo &createInfo,
                                                  const void *data,
                                                  size_t dataSize)
    {
        auto handle = CreateTexture(createInfo);
        if (handle.IsValid() && data && dataSize > 0)
        {
            UploadTextureData(handle, createInfo, data, dataSize);
        }
        return handle;
    }

    GpuResourceStore::TextureUploadResult GpuResourceStore::UploadTextureData(
        TextureHandle handle,
        const TextureCreateInfo &createInfo,
        const void *data,
        size_t dataSize)
    {
        TextureUploadResult result;
        if (!handle.IsValid() || !data || dataSize == 0)
        {
            return result;
        }

        const uint32_t effectiveMipLevels = std::max(1u, createInfo.MipLevels);

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Textures.find(handle.Id);
        if (it == m_Textures.end() || !it->second.RHITexture)
        {
            return result;
        }

        result.bTextureFound = true;
        uint32_t bytesPerPixel = GetTextureBytesPerPixel(createInfo.PixelFormat);
        uint32_t rowPitch = createInfo.Width * bytesPerPixel;
        uint32_t slicePitch = rowPitch * createInfo.Height;

        auto uploadStartTime = StoreNow();
        it->second.RHITexture->Update(data, rowPitch, slicePitch);
        result.UploadMs = StoreElapsedMs(uploadStartTime);
        result.bUploadAttempted = true;

        if (effectiveMipLevels > 1)
        {
            auto mipgenStartTime = StoreNow();
            auto commandList = m_Device ? m_Device->CreateCommandList() : nullptr;
            if (!commandList)
            {
                result.bMipgenSuccess = false;
                NORVES_LOG_ERROR("RenderResourceManager", "Failed to create command list for mip generation");
            }
            else
            {
                commandList->Begin();
                commandList->GenerateMipmaps(it->second.RHITexture);
                commandList->End();
                commandList->Submit(true);
            }
            result.MipgenMs = StoreElapsedMs(mipgenStartTime);
        }

        return result;
    }

    TextureHandle GpuResourceStore::RegisterUploadedTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const TextureCreateInfo &createInfo)
    {
        if (!rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = std::move(rhiTexture);
        data.Width = createInfo.Width;
        data.Height = createInfo.Height;
        data.Format = createInfo.PixelFormat;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_Mutex);
        m_Textures[handle.Id] = std::move(data);
        return handle;
    }

    TextureHandle GpuResourceStore::RegisterExternalTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const Container::String &debugName)
    {
        if (!rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = std::move(rhiTexture);
        data.Width = 0;
        data.Height = 0;
        data.Format = TextureCreateInfo::Format::RGBA8_UNORM;
        data.RefCount = 1;
        data.DebugName = debugName;

        Thread::ScopedLock lock(m_Mutex);
        m_Textures[handle.Id] = std::move(data);

        NORVES_LOG_DEBUG("RenderResourceManager", "External texture registered: %s (handle=%llu)",
                         debugName.c_str(), handle.Id);

        return handle;
    }

    void GpuResourceStore::ReleaseTexture(TextureHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        m_Textures.erase(handle.Id);
    }

    SamplerHandle GpuResourceStore::GetDefaultSampler()
    {
        Thread::ScopedLock lock(m_Mutex);
        if (m_DefaultSampler.IsValid())
        {
            return m_DefaultSampler;
        }

        if (!m_Device)
        {
            return SamplerHandle::Invalid();
        }

        RHI::SamplerDesc desc;
        desc.filterMin = RHI::FilterMode::Anisotropic;
        desc.filterMag = RHI::FilterMode::Anisotropic;
        desc.filterMip = RHI::FilterMode::Anisotropic;
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

        m_Samplers[m_DefaultSampler.Id] = std::move(data);

        return m_DefaultSampler;
    }

    SamplerHandle GpuResourceStore::GetPointSampler()
    {
        Thread::ScopedLock lock(m_Mutex);
        if (m_PointSampler.IsValid())
        {
            return m_PointSampler;
        }

        if (!m_Device)
        {
            return SamplerHandle::Invalid();
        }

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

        m_Samplers[m_PointSampler.Id] = std::move(data);

        return m_PointSampler;
    }

    void GpuResourceStore::ReleaseSampler(SamplerHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        m_Samplers.erase(handle.Id);
    }

    ShaderHandle GpuResourceStore::CreateShader(const ShaderCreateInfo &createInfo)
    {
        (void)createInfo;
        return ShaderHandle::Invalid();
    }

    ShaderHandle GpuResourceStore::LoadShader(const Container::String &path, ShaderStage stage)
    {
        (void)path;
        (void)stage;
        return ShaderHandle::Invalid();
    }

    void GpuResourceStore::ReleaseShader(ShaderHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        m_Shaders.erase(handle.Id);
    }

    VertexLayoutHandle GpuResourceStore::RegisterVertexLayout(const VertexLayout &layout)
    {
        auto handle = AllocateHandle<VertexLayoutHandle>();
        Thread::ScopedLock lock(m_Mutex);
        m_VertexLayouts[handle.Id] = layout;
        return handle;
    }

    const VertexLayout *GpuResourceStore::GetVertexLayout(VertexLayoutHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_VertexLayouts.find(handle.Id);
        if (it != m_VertexLayouts.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    RHI::IBuffer *GpuResourceStore::GetRHIBuffer(BufferHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Buffers.find(handle.Id);
        if (it != m_Buffers.end())
        {
            return it->second.RHIBuffer.get();
        }
        return nullptr;
    }

    RHI::ITexture *GpuResourceStore::GetRHITexture(TextureHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end())
        {
            return it->second.RHITexture.get();
        }
        return nullptr;
    }

    Container::TSharedPtr<RHI::ITexture> GpuResourceStore::GetRHITexturePtr(TextureHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end())
        {
            return it->second.RHITexture;
        }
        return nullptr;
    }

    RHI::IShader *GpuResourceStore::GetRHIShader(ShaderHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Shaders.find(handle.Id);
        if (it != m_Shaders.end())
        {
            return it->second.RHIShader.get();
        }
        return nullptr;
    }

    void GpuResourceStore::Clear()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Buffers.clear();
        m_Textures.clear();
        m_Samplers.clear();
        m_Shaders.clear();
        m_Pipelines.clear();
        m_VertexLayouts.clear();
        m_DefaultSampler = SamplerHandle::Invalid();
        m_PointSampler = SamplerHandle::Invalid();
    }

    ResourceStats GpuResourceStore::GetResourceStats() const
    {
        Thread::ScopedLock lock(m_Mutex);

        ResourceStats stats;
        stats.BufferCount = static_cast<uint32_t>(m_Buffers.size());
        stats.TextureCount = static_cast<uint32_t>(m_Textures.size());
        stats.ShaderCount = static_cast<uint32_t>(m_Shaders.size());
        stats.SamplerCount = static_cast<uint32_t>(m_Samplers.size());

        for (const auto &[id, data] : m_Buffers)
        {
            (void)id;
            stats.TotalBufferMemory += data.Size;
        }

        return stats;
    }
}
