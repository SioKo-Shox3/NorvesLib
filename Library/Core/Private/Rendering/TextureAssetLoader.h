#pragma once

#include "Asset/CookedTextureFormat.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/TextureAssetResolver.h"
#include "Rendering/TextureAssetTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct CookedTextureAsyncPayload
    {
        Asset::CookedTextureData Texture;
    };

    class TextureAssetStbiPixels
    {
    public:
        TextureAssetStbiPixels() = default;
        explicit TextureAssetStbiPixels(unsigned char *pixels) noexcept;
        ~TextureAssetStbiPixels();

        TextureAssetStbiPixels(const TextureAssetStbiPixels &) = delete;
        TextureAssetStbiPixels &operator=(const TextureAssetStbiPixels &) = delete;

        TextureAssetStbiPixels(TextureAssetStbiPixels &&other) noexcept;
        TextureAssetStbiPixels &operator=(TextureAssetStbiPixels &&other) noexcept;

        [[nodiscard]] unsigned char *Get() const noexcept { return m_Pixels; }
        [[nodiscard]] bool IsValid() const noexcept { return m_Pixels != nullptr; }
        void Reset() noexcept;

    private:
        unsigned char *m_Pixels = nullptr;
    };

    struct TextureAssetCpuLoadResult
    {
        Container::String Path;
        Container::String ResolvedPath;
        Container::String CacheKey;
        Container::AnsiString LogicalPath;
        TextureCreateInfo CreateInfo;
        Container::VariableArray<uint8_t> PixelData;
        TextureAssetStbiPixels DirectPixels;
        Container::TSharedPtr<CookedTextureAsyncPayload> CookedTexture;
        TextureLoadSource Source = TextureLoadSource::LegacyFile;
        TextureAssetFallbackMode FallbackMode = TextureAssetFallbackMode::FailOnCookedFailure;
        uint64_t AssetGeneration = 0;
        size_t PixelDataSize = 0;
        bool bSuccess = false;

        [[nodiscard]] const void *GetPixelData() const noexcept
        {
            return DirectPixels.IsValid() ? DirectPixels.Get() : PixelData.data();
        }
    };

    struct TextureAssetLooseDecodeResult
    {
        TextureCreateInfo CreateInfo;
        Container::VariableArray<uint8_t> PixelData;
        bool bSuccess = false;
    };

    class TextureAssetLoader
    {
    public:
        [[nodiscard]] static TextureAssetCpuLoadResult LoadForCaller(const TextureAssetLoadPlan &plan);
        [[nodiscard]] static TextureAssetCpuLoadResult LoadLooseFileForCaller(
            const TextureAssetLoadPlan &plan,
            TextureLoadSource source);
        [[nodiscard]] static TextureAssetCpuLoadResult LoadForWorker(
            const TextureAssetLoadPlan &plan,
            uint32_t requestId);
        [[nodiscard]] static TextureAssetLooseDecodeResult DecodeLooseFallbackForMainRender(
            const Container::String &path,
            const Container::AnsiString &logicalPath,
            const Container::String &resolvedPath);
        [[nodiscard]] static PreparedTextureAsset PrepareForWorker(
            const PreparedTextureAssetPlan &plan,
            const char *role,
            uint32_t requestId);
        [[nodiscard]] static bool TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
            const PreparedTextureAsset &prepared,
            PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
            Container::String *pOutReason,
            const char *role,
            uint32_t requestId);
    };
}
