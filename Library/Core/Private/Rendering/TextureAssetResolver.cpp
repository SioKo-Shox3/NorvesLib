#include "Rendering/TextureAssetResolver.h"

#include "Asset/AssetPath.h"
#include "Asset/AssetSystem.h"

#include <string>

namespace NorvesLib::Core::Rendering
{
    TextureAssetResolver::TextureAssetResolver()
        : m_AssetRoot(GetDefaultAssetRoot())
    {
        m_System = CreateSystemSnapshot();
    }

    void TextureAssetResolver::SetAssetRoot(const Container::String &assetRoot)
    {
        m_AssetRoot = ToAnsiString(assetRoot);
        m_System = CreateSystemSnapshot();
        ++m_Generation;
    }

    bool TextureAssetResolver::LoadManifestFromJsonText(const Container::String &jsonText,
                                                        const Container::String &sourceName)
    {
        auto newSystem = Container::MakeShared<Asset::AssetSystem>(m_AssetRoot);
        const bool bLoaded = newSystem->LoadManifestFromJsonText(jsonText, ToAnsiString(sourceName));
        m_System = newSystem;
        m_ManifestJson = jsonText;
        m_ManifestSourceName = sourceName;
        m_bManifestLoadAttempted = true;
        ++m_Generation;
        return bLoaded;
    }

    void TextureAssetResolver::ResetManifest()
    {
        m_System = Container::MakeShared<Asset::AssetSystem>(m_AssetRoot);
        m_ManifestJson.clear();
        m_ManifestSourceName.clear();
        m_bManifestLoadAttempted = false;
        ++m_Generation;
    }

    void TextureAssetResolver::SetFallbackMode(TextureAssetFallbackMode mode)
    {
        m_FallbackMode = ToAssetFallbackMode(mode);
        ++m_Generation;
    }

    TextureAssetLoadPlan TextureAssetResolver::BuildTextureLoadPlan(const Container::String &requestPath) const
    {
        TextureAssetLoadPlan plan;
        plan.RequestPath = requestPath;

        const Asset::AssetPath assetPath = Asset::AssetPath::Normalize(ToAnsiString(requestPath), m_AssetRoot);
        plan.Generation = m_Generation;
        plan.FallbackMode = m_FallbackMode;

        if (assetPath.IsValid() && assetPath.HasLogicalPath() && !assetPath.IsAbsolute())
        {
            plan.bUseAssetSystem = true;
            plan.bPathValid = true;
            plan.LogicalPath = assetPath.GetLogicalPath();
            plan.ResolvedPath = assetPath.HasResolvedPath()
                                    ? ToString(assetPath.GetResolvedPath())
                                    : ResolveLoosePath(requestPath);
            plan.CacheKey = MakeAssetTextureCacheKey(plan.Generation, plan.LogicalPath);
            plan.AssetSystem = m_System;
            return plan;
        }

        plan.bUseAssetSystem = false;
        plan.bPathValid = assetPath.IsValid();
        plan.ResolvedPath = (assetPath.IsValid() && assetPath.HasResolvedPath())
                                ? ToString(assetPath.GetResolvedPath())
                                : ResolveLoosePath(requestPath);
        plan.CacheKey = MakeLegacyTextureCacheKey(plan.ResolvedPath);
        return plan;
    }

    PreparedTextureAssetPlan TextureAssetResolver::BuildPreparedTexturePlan(
        const Container::String &requestPath,
        const Container::String &resolvedFallbackPath) const
    {
        PreparedTextureAssetPlan plan;
        plan.Prepared.RequestPath = requestPath;
        plan.Prepared.ResolvedFallbackPath = resolvedFallbackPath;
        plan.Prepared.Generation = m_Generation;
        plan.Prepared.FallbackMode = ToTextureAssetFallbackMode(m_FallbackMode);
        plan.AssetRoot = m_AssetRoot;
        plan.AssetSystem = m_System;

        const Asset::AssetPath assetPath = Asset::AssetPath::Normalize(ToAnsiString(requestPath), m_AssetRoot);
        if (!assetPath.IsValid())
        {
            plan.BlockedStatus = PreparedTextureAssetStatus::InvalidPath;
            plan.BlockedReason = "asset path is invalid";
            return plan;
        }

        if (assetPath.IsAbsolute())
        {
            if (plan.Prepared.ResolvedFallbackPath.empty() && assetPath.HasResolvedPath())
            {
                plan.Prepared.ResolvedFallbackPath = ToString(assetPath.GetResolvedPath());
            }
            plan.BlockedStatus = PreparedTextureAssetStatus::AbsolutePathUnsupported;
            plan.BlockedReason = "absolute asset path is unsupported";
            return plan;
        }

        if (!assetPath.HasLogicalPath())
        {
            plan.BlockedStatus = PreparedTextureAssetStatus::InvalidPath;
            plan.BlockedReason = "asset path has no logical path";
            return plan;
        }

        plan.Prepared.LogicalPath = assetPath.GetLogicalPath();
        plan.Prepared.CacheKey = MakeAssetTextureCacheKey(plan.Prepared.Generation, plan.Prepared.LogicalPath);
        if (plan.Prepared.ResolvedFallbackPath.empty())
        {
            plan.Prepared.ResolvedFallbackPath = assetPath.HasResolvedPath()
                                                    ? ToString(assetPath.GetResolvedPath())
                                                    : ResolveLoosePath(requestPath);
        }

        if (!plan.AssetSystem)
        {
            plan.BlockedStatus = PreparedTextureAssetStatus::ManifestInvalid;
            plan.BlockedReason = "asset system is unavailable";
            return plan;
        }

        plan.bReadyForManifest = true;
        return plan;
    }

    bool TextureAssetResolver::IsGenerationCurrent(uint64_t generation) const noexcept
    {
        return m_Generation == generation;
    }

    Asset::AssetManifestResolveResult TextureAssetResolver::FindPreparedCookedVariant(
        const PreparedTextureAssetPlan &plan)
    {
        if (!plan.AssetSystem)
        {
            return {};
        }

        return plan.AssetSystem->FindCookedVariant(
            plan.Prepared.LogicalPath,
            Asset::AssetKind::Texture,
            Asset::AssetManifest::DefaultVariant);
    }

    Asset::AssetFallbackMode TextureAssetResolver::ToAssetFallbackMode(TextureAssetFallbackMode mode)
    {
        switch (mode)
        {
        case TextureAssetFallbackMode::DebugAllowLooseFallback:
            return Asset::AssetFallbackMode::DebugAllowLooseFallback;
        case TextureAssetFallbackMode::FailOnCookedFailure:
        default:
            return Asset::AssetFallbackMode::FailOnCookedFailure;
        }
    }

    TextureAssetFallbackMode TextureAssetResolver::ToTextureAssetFallbackMode(Asset::AssetFallbackMode mode)
    {
        switch (mode)
        {
        case Asset::AssetFallbackMode::DebugAllowLooseFallback:
            return TextureAssetFallbackMode::DebugAllowLooseFallback;
        case Asset::AssetFallbackMode::FailOnCookedFailure:
        default:
            return TextureAssetFallbackMode::FailOnCookedFailure;
        }
    }

    bool TextureAssetResolver::AllowsDebugLooseFallback(TextureAssetFallbackMode mode)
    {
        return mode == TextureAssetFallbackMode::DebugAllowLooseFallback;
    }

    Container::TSharedPtr<const Asset::AssetSystem> TextureAssetResolver::CreateSystemSnapshot() const
    {
        auto system = Container::MakeShared<Asset::AssetSystem>(m_AssetRoot);
        if (m_bManifestLoadAttempted)
        {
            const bool bLoaded = system->LoadManifestFromJsonText(m_ManifestJson, ToAnsiString(m_ManifestSourceName));
            (void)bLoaded;
        }
        return system;
    }

    Container::String TextureAssetResolver::ResolveLoosePath(const Container::String &path) const
    {
        Container::String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
        if (path.size() > 0 && path[0] != '/' && path[0] != '\\' &&
            (path.size() < 2 || path[1] != ':'))
        {
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

    Container::AnsiString TextureAssetResolver::ToAnsiString(const Container::String &value)
    {
        return Container::AnsiString(value.c_str());
    }

    Container::String TextureAssetResolver::ToString(const Container::AnsiString &value)
    {
        return Container::String(value.c_str());
    }

    Container::AnsiString TextureAssetResolver::GetDefaultAssetRoot()
    {
#ifdef NORVES_ASSET_DIR
        return Container::AnsiString(NORVES_ASSET_DIR);
#else
        return {};
#endif
    }

    Container::String TextureAssetResolver::MakeLegacyTextureCacheKey(const Container::String &resolvedPath)
    {
        Container::String cacheKey("legacy:");
        cacheKey += resolvedPath;
        return cacheKey;
    }

    Container::String TextureAssetResolver::MakeAssetTextureCacheKey(uint64_t generation,
                                                                     const Container::AnsiString &logicalPath)
    {
        Container::String cacheKey("asset:");
        const std::string generationText = std::to_string(generation);
        cacheKey += generationText.c_str();
        cacheKey += ":default:";
        cacheKey += logicalPath.c_str();
        return cacheKey;
    }
}
