#pragma once
// cache・transactionが同じreferenceの意味比較を使う。derived hex表示は比較しない。
#include "Asset/AssetManifest.h"
#include "CookOutputPaths.h"
namespace NorvesLib::Tools::AssetCook::Detail::CookReferenceValues
{
    using CookOutputPaths::EqualName;
    using Core::Asset::AssetCookedReference;
    inline bool SameKey(const AssetCookedReference& a, const AssetCookedReference& b)
    {
        return a.Kind == b.Kind && EqualName(a.LogicalPath, b.LogicalPath) && EqualName(a.Variant, b.Variant);
    }
    inline bool SameIdentity(const AssetCookedReference& a, const AssetCookedReference& b)
    {
        return SameKey(a, b) && a.SourceHash == b.SourceHash && EqualName(a.Format, b.Format) &&
               EqualName(a.CookedPackage, b.CookedPackage) && EqualName(a.EntryName, b.EntryName) &&
               a.EntryType == b.EntryType && a.CookedVersion == b.CookedVersion;
    }
    inline bool SameReference(const AssetCookedReference& a, const AssetCookedReference& b)
    {
        if (!SameIdentity(a, b) || a.CookedHash != b.CookedHash || a.bHasSkeletalMetadata != b.bHasSkeletalMetadata)
        {
            return false;
        }
        if (!a.bHasSkeletalMetadata)
        {
            return true;
        }
        const auto& x = a.SkeletalMetadata;
        const auto& y = b.SkeletalMetadata;
        return x.VertexCount == y.VertexCount && x.IndexCount == y.IndexCount && x.JointCount == y.JointCount &&
               x.ClipCount == y.ClipCount && x.bHasSubmeshCounts == y.bHasSubmeshCounts &&
               (!x.bHasSubmeshCounts ||
                (x.SubmeshCount == y.SubmeshCount && x.MaterialSlotCount == y.MaterialSlotCount));
    }
} // namespace NorvesLib::Tools::AssetCook::Detail::CookReferenceValues
