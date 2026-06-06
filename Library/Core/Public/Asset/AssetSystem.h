#pragma once

#include "Asset/AssetFileReader.h"
#include "Asset/AssetManifest.h"
#include "Asset/AssetResolveResult.h"
#include "Container/String.h"
#include "Container/StringView.h"

namespace NorvesLib::Core::Asset
{
    /**
     * @brief Facade for resolving logical asset paths to cooked package entries or loose fallback blobs.
     *
     * Thread contract:
     * - ResolveAsset() and FindCookedVariant() are reentrant when no manifest mutation is running.
     * - Concurrent SetManifest(), ResetManifest(), or LoadManifestFromJsonText() with ResolveAsset()
     *   is not supported in Phase 7.
     * - This facade does not create GPU resources and owns no package cache.
     */
    class AssetSystem
    {
    public:
        AssetSystem();
        explicit AssetSystem(const Container::AnsiString &assetRoot);

        void ResetManifest();
        void SetManifest(const AssetManifest &manifest);
        [[nodiscard]] bool LoadManifestFromJsonText(const Container::String &jsonText, Container::AnsiStringView sourceName = {});

        [[nodiscard]] AssetManifestResolveResult FindCookedVariant(Container::AnsiStringView logicalPath,
                                                                   AssetKind kind,
                                                                   Container::AnsiStringView variant = AssetManifest::DefaultVariant) const;

        [[nodiscard]] AssetResolveResult ResolveAsset(const AssetResolveRequest &request) const;

        [[nodiscard]] AssetResolveResult ResolveAsset(Container::AnsiStringView logicalPath,
                                                      AssetKind kind,
                                                      Container::AnsiStringView variant = AssetManifest::DefaultVariant,
                                                      AssetFallbackMode fallbackMode = AssetFallbackMode::FailOnCookedFailure) const;

    private:
        AssetManifest m_Manifest;
        AssetFileReader m_FileReader;
    };
}
