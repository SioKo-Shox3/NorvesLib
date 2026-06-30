#include "Asset/AssetSystem.h"

#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetPath.h"
#include "FileStream/Package.h"

#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        Container::AnsiString ToAnsiString(Container::AnsiStringView view)
        {
            return Container::AnsiString(view);
        }

        const char *GetReadStatusName(AssetReadStatus status)
        {
            switch (status)
            {
            case AssetReadStatus::Success:
                return "Success";
            case AssetReadStatus::InvalidRequest:
                return "InvalidRequest";
            case AssetReadStatus::InvalidAssetRoot:
                return "InvalidAssetRoot";
            case AssetReadStatus::InvalidPath:
                return "InvalidPath";
            case AssetReadStatus::FileNotFound:
                return "FileNotFound";
            case AssetReadStatus::OpenFailed:
                return "OpenFailed";
            case AssetReadStatus::SizeQueryFailed:
                return "SizeQueryFailed";
            case AssetReadStatus::SizeTooLarge:
                return "SizeTooLarge";
            case AssetReadStatus::ReadFailed:
                return "ReadFailed";
            default:
                return "Unknown";
            }
        }

        Container::AnsiString MakeReadFailureReason(const char *prefix,
                                                    Container::AnsiStringView path,
                                                    AssetReadStatus status)
        {
            Container::AnsiString reason(prefix);
            reason += " path=\"";
            reason += ToAnsiString(path);
            reason += "\" status=";
            reason += GetReadStatusName(status);
            return reason;
        }

        bool TryNormalizeRequest(const AssetResolveRequest &request, Container::AnsiString &outLogicalPath)
        {
            if (request.Kind == AssetKind::Unknown ||
                request.LogicalPath.empty() ||
                request.Variant.empty())
            {
                return false;
            }

            const AssetPath path = AssetPath::Normalize(request.LogicalPath);
            if (!path.IsValid() || path.IsAbsolute() || !path.HasLogicalPath())
            {
                return false;
            }

            outLogicalPath = path.GetLogicalPath();
            return !outLogicalPath.empty();
        }

        AssetManifestResolveResult MakeInvalidManifestResolveResult()
        {
            AssetManifestResolveResult result;
            result.Status = AssetManifestResolveStatus::InvalidRequest;
            return result;
        }

        AssetReadResult ReadLooseAsset(const AssetFileReader &reader, Container::AnsiStringView logicalPath)
        {
            AssetReadRequest readRequest;
            readRequest.InputPath = ToAnsiString(logicalPath);
            readRequest.AssetRoot = {};
            readRequest.bAllowAbsolutePath = false;
            return reader.Read(readRequest);
        }

        AssetResolveResult ResolveLooseInto(AssetResolveResult result,
                                            const AssetFileReader &reader,
                                            Container::AnsiStringView normalizedLogicalPath,
                                            AssetResolveSource source)
        {
            const AssetReadResult looseRead = ReadLooseAsset(reader, normalizedLogicalPath);
            result.Source = source;
            result.LoosePath = looseRead.Path;
            result.LooseReadStatus = looseRead.Status;

            if (looseRead.Succeeded())
            {
                result.Status = AssetResolveStatus::SuccessLoose;
                result.Blob = looseRead.Blob;
                result.Reason = {};
                return result;
            }

            result.Status = AssetResolveStatus::LooseReadFailed;
            result.Blob = AssetBlob::Invalid();
            result.Reason = MakeReadFailureReason("loose asset read failed", normalizedLogicalPath, looseRead.Status);
            return result;
        }

        AssetResolveResult ResolveCookedFailure(AssetResolveResult result,
                                                const AssetFileReader &reader,
                                                const AssetResolveRequest &request,
                                                AssetCookedFailureKind failureKind,
                                                AssetResolveStatus failureStatus,
                                                Container::AnsiStringView failureReason)
        {
            result.Status = failureStatus;
            result.Source = AssetResolveSource::None;
            result.Blob = AssetBlob::Invalid();
            result.Reason = ToAnsiString(failureReason);
            result.FallbackDecision = AssetManifest::DecideCookedFailureFallback(
                result.CookedReference,
                failureKind,
                request.FallbackMode,
                failureReason);

            if (!result.FallbackDecision.ShouldUseLoose())
            {
                return result;
            }

            const Container::AnsiString normalizedLogicalPath = result.NormalizedLogicalPath;
            return ResolveLooseInto(std::move(result),
                                    reader,
                                    normalizedLogicalPath,
                                    AssetResolveSource::DebugLooseFallback);
        }

        AssetCookedFailureKind GetPackageReadFailureKind(AssetReadStatus status)
        {
            return status == AssetReadStatus::FileNotFound
                       ? AssetCookedFailureKind::PackageMissing
                       : AssetCookedFailureKind::PackageReadFailed;
        }
    }

    AssetSystem::AssetSystem()
        : m_FileReader()
    {
    }

    AssetSystem::AssetSystem(const Container::AnsiString &assetRoot)
        : m_FileReader(assetRoot)
    {
    }

    void AssetSystem::ResetManifest()
    {
        m_Manifest.Reset();
    }

    void AssetSystem::SetManifest(const AssetManifest &manifest)
    {
        m_Manifest = manifest;
    }

    bool AssetSystem::LoadManifestFromJsonText(const Container::String &jsonText, Container::AnsiStringView sourceName)
    {
        return m_Manifest.LoadFromJsonText(jsonText, sourceName);
    }

    AssetManifestResolveResult AssetSystem::FindCookedVariant(Container::AnsiStringView logicalPath,
                                                              AssetKind kind,
                                                              Container::AnsiStringView variant) const
    {
        AssetResolveRequest request;
        request.LogicalPath = ToAnsiString(logicalPath);
        request.Kind = kind;
        request.Variant = ToAnsiString(variant);

        Container::AnsiString normalizedLogicalPath;
        if (!TryNormalizeRequest(request, normalizedLogicalPath))
        {
            return MakeInvalidManifestResolveResult();
        }

        return m_Manifest.Resolve(normalizedLogicalPath, kind, variant);
    }

    AssetResolveResult AssetSystem::ResolveAsset(const AssetResolveRequest &request) const
    {
        AssetResolveResult result;
        result.ManifestStatus = AssetManifestResolveStatus::InvalidRequest;
        result.LooseReadStatus = AssetReadStatus::InvalidRequest;
        result.PackageReadStatus = AssetReadStatus::InvalidRequest;

        Container::AnsiString normalizedLogicalPath;
        if (!TryNormalizeRequest(request, normalizedLogicalPath))
        {
            result.Status = AssetResolveStatus::InvalidRequest;
            result.Reason = "asset resolve request is invalid";
            return result;
        }

        result.NormalizedLogicalPath = normalizedLogicalPath;

        const AssetManifestResolveResult manifestResult = m_Manifest.Resolve(normalizedLogicalPath,
                                                                             request.Kind,
                                                                             request.Variant);
        result.ManifestStatus = manifestResult.Status;
        if (manifestResult.Status == AssetManifestResolveStatus::InvalidRequest)
        {
            result.Status = AssetResolveStatus::InvalidRequest;
            result.Reason = "asset manifest resolve request is invalid";
            return result;
        }

        if (manifestResult.Status == AssetManifestResolveStatus::InvalidManifest)
        {
            result.Status = AssetResolveStatus::InvalidManifest;
            result.Reason = "asset manifest is invalid";
            return result;
        }

        if (manifestResult.ShouldUseLooseFallback())
        {
            return ResolveLooseInto(std::move(result), m_FileReader, normalizedLogicalPath, AssetResolveSource::Loose);
        }

        if (!manifestResult.ShouldUseCooked())
        {
            result.Status = AssetResolveStatus::InvalidManifest;
            result.Reason = "asset manifest resolve returned an unsupported status";
            return result;
        }

        result.CookedReference = manifestResult.Reference;

        AssetReadRequest packageReadRequest;
        packageReadRequest.InputPath = result.CookedReference.CookedPackage;
        packageReadRequest.AssetRoot = {};
        packageReadRequest.bAllowAbsolutePath = false;
        const AssetReadResult packageRead = m_FileReader.Read(packageReadRequest);
        result.PackageReadStatus = packageRead.Status;
        if (!packageRead.Succeeded())
        {
            const Container::AnsiString reason = MakeReadFailureReason(
                "cooked package read failed",
                result.CookedReference.CookedPackage,
                packageRead.Status);
            return ResolveCookedFailure(std::move(result),
                                        m_FileReader,
                                        request,
                                        GetPackageReadFailureKind(packageRead.Status),
                                        AssetResolveStatus::CookedPackageReadFailed,
                                        reason);
        }

        NorvesLib::FileStream::Package package;
        if (!package.LoadFromMemory(packageRead.Blob.GetSpan()))
        {
            Container::AnsiString reason("cooked package parse failed path=\"");
            reason += result.CookedReference.CookedPackage;
            reason += "\"";
            return ResolveCookedFailure(std::move(result),
                                        m_FileReader,
                                        request,
                                        AssetCookedFailureKind::PackageParseFailed,
                                        AssetResolveStatus::CookedPackageParseFailed,
                                        reason);
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(result.CookedReference.EntryName, result.CookedReference.EntryType, entry))
        {
            Container::AnsiString reason("cooked package entry missing package=\"");
            reason += result.CookedReference.CookedPackage;
            reason += "\" entry=\"";
            reason += result.CookedReference.EntryName;
            reason += "\"";
            return ResolveCookedFailure(std::move(result),
                                        m_FileReader,
                                        request,
                                        AssetCookedFailureKind::EntryMissing,
                                        AssetResolveStatus::CookedEntryMissing,
                                        reason);
        }

        AssetBlob cookedBlob = package.OpenEntry(entry);
        if (!cookedBlob.IsValid())
        {
            Container::AnsiString reason("cooked package entry open failed package=\"");
            reason += result.CookedReference.CookedPackage;
            reason += "\" entry=\"";
            reason += result.CookedReference.EntryName;
            reason += "\"";
            return ResolveCookedFailure(std::move(result),
                                        m_FileReader,
                                        request,
                                        AssetCookedFailureKind::EntryMissing,
                                        AssetResolveStatus::CookedEntryMissing,
                                        reason);
        }

        const uint64_t cookedHash = ComputeAssetPackagePayloadHash(cookedBlob.GetData(), cookedBlob.GetSize());
        if (cookedHash != result.CookedReference.CookedHash)
        {
            Container::AnsiString reason("cooked entry hash mismatch package=\"");
            reason += result.CookedReference.CookedPackage;
            reason += "\" entry=\"";
            reason += result.CookedReference.EntryName;
            reason += "\"";
            return ResolveCookedFailure(std::move(result),
                                        m_FileReader,
                                        request,
                                        AssetCookedFailureKind::EntryHashMismatch,
                                        AssetResolveStatus::CookedEntryHashMismatch,
                                        reason);
        }

        result.Status = AssetResolveStatus::SuccessCooked;
        result.Source = AssetResolveSource::Cooked;
        result.Entry = entry;
        result.Blob = cookedBlob;
        result.Reason = {};
        return result;
    }

    AssetResolveResult AssetSystem::ResolveAsset(Container::AnsiStringView logicalPath,
                                                 AssetKind kind,
                                                 Container::AnsiStringView variant,
                                                 AssetFallbackMode fallbackMode) const
    {
        AssetResolveRequest request;
        request.LogicalPath = ToAnsiString(logicalPath);
        request.Kind = kind;
        request.Variant = ToAnsiString(variant);
        request.FallbackMode = fallbackMode;
        return ResolveAsset(request);
    }

    size_t AssetSystem::GetAssetCount() const noexcept
    {
        return m_Manifest.GetReferenceCount();
    }

    const AssetCookedReference &AssetSystem::GetAssetReference(size_t index) const noexcept
    {
        return m_Manifest.GetReference(index);
    }
}
