#pragma once

#include "Rendering/GpuResourceTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/VertexLayout.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IBuffer;
    class IDevice;
    class IShader;
    class ITexture;
}

namespace NorvesLib::Core::Rendering
{
    class GpuResourceStore final
    {
    public:
        struct TextureUploadResult
        {
            double UploadMs = 0.0;
            double MipgenMs = 0.0;
            bool bTextureFound = false;
            bool bUploadAttempted = false;
            bool bMipgenSuccess = true;
        };

        GpuResourceStore(Container::TSharedPtr<RHI::IDevice> device,
                         Thread::Atomic<uint64_t> &nextHandleId);
        ~GpuResourceStore();

        GpuResourceStore(const GpuResourceStore &) = delete;
        GpuResourceStore &operator=(const GpuResourceStore &) = delete;

        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo);
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo,
                                  const void *data,
                                  size_t dataSize);
        bool UpdateBuffer(BufferHandle handle,
                          const void *data,
                          size_t dataSize,
                          size_t offset = 0);
        void ReleaseBuffer(BufferHandle handle);

        TextureHandle CreateTexture(const TextureCreateInfo &createInfo);
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo,
                                    const void *data,
                                    size_t dataSize);
        TextureUploadResult UploadTextureData(TextureHandle handle,
                                              const TextureCreateInfo &createInfo,
                                              const void *data,
                                              size_t dataSize);
        TextureHandle RegisterUploadedTexture(Container::TSharedPtr<RHI::ITexture> rhiTexture,
                                              const TextureCreateInfo &createInfo);
        TextureHandle RegisterExternalTexture(Container::TSharedPtr<RHI::ITexture> rhiTexture,
                                              const Container::String &debugName);
        void ReleaseTexture(TextureHandle handle);

        SamplerHandle GetDefaultSampler();
        SamplerHandle GetPointSampler();
        void ReleaseSampler(SamplerHandle handle);

        ShaderHandle CreateShader(const ShaderCreateInfo &createInfo);
        ShaderHandle LoadShader(const Container::String &path, ShaderStage stage);
        void ReleaseShader(ShaderHandle handle);

        VertexLayoutHandle RegisterVertexLayout(const VertexLayout &layout);
        const VertexLayout *GetVertexLayout(VertexLayoutHandle handle) const;

        RHI::IBuffer *GetRHIBuffer(BufferHandle handle) const;
        RHI::ITexture *GetRHITexture(TextureHandle handle) const;
        Container::TSharedPtr<RHI::ITexture> GetRHITexturePtr(TextureHandle handle) const;
        RHI::IShader *GetRHIShader(ShaderHandle handle) const;

        void Clear();
        ResourceStats GetResourceStats() const;

    private:
        template <typename HandleType>
        HandleType AllocateHandle()
        {
            HandleType handle;
            handle.Id = m_NextHandleId.FetchAdd(1, std::memory_order_relaxed);
            return handle;
        }

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Thread::Atomic<uint64_t> &m_NextHandleId;

        mutable Thread::Mutex m_Mutex;
        Container::Map<uint64_t, BufferResourceData> m_Buffers;
        Container::Map<uint64_t, TextureResourceData> m_Textures;
        Container::Map<uint64_t, SamplerResourceData> m_Samplers;
        Container::Map<uint64_t, ShaderResourceData> m_Shaders;
        Container::Map<uint64_t, PipelineResourceData> m_Pipelines;
        Container::Map<uint64_t, VertexLayout> m_VertexLayouts;

        SamplerHandle m_DefaultSampler;
        SamplerHandle m_PointSampler;
    };
}
