#pragma once

#include "Rendering/RenderTypes.h"
#include "Rendering/TextureAssetTypes.h"
#include "Rendering/TextureAsyncTypes.h"

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"
#include "Thread/Mutex.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ITexture;
}

namespace NorvesLib::Core::Rendering
{
    class GpuResourceStore;
    class TextureAsyncLoadQueue;
    class TextureAssetResolver;
    class TextureHandleCache;

    class TextureAssetRuntime final
    {
    public:
        TextureAssetRuntime();
        ~TextureAssetRuntime();

        TextureAssetRuntime(const TextureAssetRuntime &) = delete;
        TextureAssetRuntime &operator=(const TextureAssetRuntime &) = delete;

        void Bind(RHI::IDevice *pDevice, GpuResourceStore *pGpuResources);
        void Unbind();
        void ClearRuntimeResources();

        TextureHandle CreateTexture(const TextureCreateInfo &createInfo,
                                    const void *data,
                                    size_t dataSize);
        TextureHandle LoadTexture(const Container::String &path);
        uint32_t LoadTextureAsync(
            const Container::String &path,
            NorvesLib::Core::Delegate<void, TextureHandle> callback = {});
        uint32_t FlushCompletedTextureLoads();
        uint32_t GetPendingAsyncLoadCount() const;

        bool SetTextureAssetRoot(const Container::String &assetRoot);
        bool LoadTextureAssetManifestFromJsonText(const Container::String &jsonText,
                                                  const Container::String &sourceName = Container::String());
        bool ResetTextureAssetManifest();
        bool SetTextureAssetFallbackMode(TextureAssetFallbackMode mode);

        [[nodiscard]] PreparedTextureAsset PrepareTextureAssetForWorker(
            const Container::String &requestPath,
            const Container::String &resolvedFallbackPath = {},
            const char *role = "worker",
            uint32_t requestId = 0);
        [[nodiscard]] bool IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const;
        [[nodiscard]] TextureHandle FinalizePreparedTextureAsset(
            const PreparedTextureAsset &prepared,
            const char *role = "main_render",
            uint32_t requestId = 0);
        [[nodiscard]] bool TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
            const PreparedTextureAsset &prepared,
            PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
            Container::String *pOutReason = nullptr,
            const char *role = "worker",
            uint32_t requestId = 0) const;

    private:
        [[nodiscard]] bool IsBound() const noexcept;
        TextureAssetResolver &GetTextureAssetResolverLocked();
        TextureHandle RegisterUploadedTexture(Container::TSharedPtr<RHI::ITexture> rhiTexture,
                                              const TextureCreateInfo &createInfo);
        void ReleaseTexture(TextureHandle handle);

        RHI::IDevice *m_pDevice = nullptr;
        GpuResourceStore *m_pGpuResources = nullptr;

        // Lock order when nested: texture asset -> async queue -> texture cache.
        Container::TUniquePtr<TextureAssetResolver> m_TextureAssetResolver;
        Container::TUniquePtr<TextureHandleCache> m_TextureHandleCache;
        Container::TUniquePtr<TextureAsyncLoadQueue> m_TextureAsyncLoads;
        mutable Thread::Mutex m_TextureAssetMutex;
    };
}
