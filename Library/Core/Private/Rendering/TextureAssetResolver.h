#pragma once

#include "Asset/AssetManifest.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/TextureAssetTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Rendering
{
    struct TextureAssetLoadPlan
    {
        bool bUseAssetSystem = false;
        bool bPathValid = false;
        Container::String RequestPath;
        Container::String ResolvedPath;
        Container::String CacheKey;
        Container::AnsiString LogicalPath;
        uint64_t Generation = 0;
        Asset::AssetFallbackMode FallbackMode = Asset::AssetFallbackMode::FailOnCookedFailure;
        Container::TSharedPtr<const Asset::AssetSystem> AssetSystem;
    };

    struct PreparedTextureAssetPlan
    {
        PreparedTextureAsset Prepared;
        Container::AnsiString AssetRoot;
        Container::TSharedPtr<const Asset::AssetSystem> AssetSystem;
        PreparedTextureAssetStatus BlockedStatus = PreparedTextureAssetStatus::InvalidRequest;
        const char *BlockedReason = "";
        bool bReadyForManifest = false;
    };

    class TextureAssetResolver
    {
    public:
        TextureAssetResolver();

        void SetAssetRoot(const Container::String &assetRoot);
        [[nodiscard]] bool LoadManifestFromJsonText(const Container::String &jsonText,
                                                    const Container::String &sourceName);
        void ResetManifest();
        void SetFallbackMode(TextureAssetFallbackMode mode);

        [[nodiscard]] TextureAssetLoadPlan BuildTextureLoadPlan(const Container::String &requestPath) const;
        [[nodiscard]] PreparedTextureAssetPlan BuildPreparedTexturePlan(
            const Container::String &requestPath,
            const Container::String &resolvedFallbackPath) const;

        [[nodiscard]] bool IsGenerationCurrent(uint64_t generation) const noexcept;
        [[nodiscard]] uint64_t GetGeneration() const noexcept { return m_Generation; }

        [[nodiscard]] static Asset::AssetManifestResolveResult FindPreparedCookedVariant(
            const PreparedTextureAssetPlan &plan);
        [[nodiscard]] static Asset::AssetFallbackMode ToAssetFallbackMode(TextureAssetFallbackMode mode);
        [[nodiscard]] static TextureAssetFallbackMode ToTextureAssetFallbackMode(Asset::AssetFallbackMode mode);
        [[nodiscard]] static bool AllowsDebugLooseFallback(TextureAssetFallbackMode mode);

    private:
        [[nodiscard]] Container::TSharedPtr<const Asset::AssetSystem> CreateSystemSnapshot() const;
        [[nodiscard]] Container::String ResolveLoosePath(const Container::String &path) const;

        [[nodiscard]] static Container::AnsiString ToAnsiString(const Container::String &value);
        [[nodiscard]] static Container::String ToString(const Container::AnsiString &value);
        [[nodiscard]] static Container::AnsiString GetDefaultAssetRoot();
        [[nodiscard]] static Container::String MakeLegacyTextureCacheKey(const Container::String &resolvedPath);
        [[nodiscard]] static Container::String MakeAssetTextureCacheKey(uint64_t generation,
                                                                        const Container::AnsiString &logicalPath);

        Container::AnsiString m_AssetRoot;
        Container::String m_ManifestJson;
        Container::String m_ManifestSourceName;
        bool m_bManifestLoadAttempted = false;
        Asset::AssetFallbackMode m_FallbackMode = Asset::AssetFallbackMode::FailOnCookedFailure;
        uint64_t m_Generation = 1;
        Container::TSharedPtr<const Asset::AssetSystem> m_System;
    };
}
