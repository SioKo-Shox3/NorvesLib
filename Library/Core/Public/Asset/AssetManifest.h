#pragma once

#include "Asset/AssetPackageFormat.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class JsonDocument;
}

namespace NorvesLib::Core::Asset
{
    enum class AssetKind : uint8_t
    {
        Unknown,
        Texture,
        Model,
        Raw
    };

    enum class AssetManifestParseStatus : uint8_t
    {
        Success,
        ParseError,
        RootNotObject,
        UnsupportedVersion,
        AssetsNotArray,
        RequiredFieldMissing,
        InvalidField,
        DuplicateAsset
    };

    enum class AssetManifestResolveStatus : uint8_t
    {
        CookedReferenceFound,
        LooseFallbackManifestMissing,
        LooseFallbackVariantMissing,
        InvalidRequest,
        InvalidManifest
    };

    enum class AssetFallbackMode : uint8_t
    {
        FailOnCookedFailure,
        DebugAllowLooseFallback
    };

    enum class AssetCookedFailureKind : uint8_t
    {
        Unknown,
        PackageMissing,
        PackageReadFailed,
        PackageParseFailed,
        EntryMissing,
        EntryHashMismatch
    };

    enum class AssetFallbackAction : uint8_t
    {
        UseCooked,
        UseLoose,
        Fail
    };

    struct AssetCookedReference
    {
        Container::AnsiString LogicalPath;
        AssetKind Kind = AssetKind::Unknown;
        uint64_t SourceHash = 0;
        Container::AnsiString SourceHashHex;
        Container::AnsiString Variant;
        Container::AnsiString Format;
        Container::AnsiString CookedPackage;
        Container::AnsiString EntryName;
        AssetPackageFourCC EntryType = 0;
        Container::AnsiString EntryTypeText;
        uint64_t CookedHash = 0;
        Container::AnsiString CookedHashHex;
        uint32_t CookedVersion = 0;
    };

    struct AssetManifestResolveResult
    {
        AssetManifestResolveStatus Status = AssetManifestResolveStatus::InvalidRequest;
        AssetCookedReference Reference;

        [[nodiscard]] bool ShouldUseCooked() const noexcept
        {
            return Status == AssetManifestResolveStatus::CookedReferenceFound;
        }

        [[nodiscard]] bool ShouldUseLooseFallback() const noexcept
        {
            return Status == AssetManifestResolveStatus::LooseFallbackManifestMissing ||
                   Status == AssetManifestResolveStatus::LooseFallbackVariantMissing;
        }
    };

    struct AssetFallbackDecision
    {
        AssetFallbackAction Action = AssetFallbackAction::Fail;
        AssetCookedFailureKind FailureKind = AssetCookedFailureKind::Unknown;
        bool bRequiresExplicitLog = false;
        Container::AnsiString LogicalPath;
        Container::AnsiString CookedPackage;
        Container::AnsiString EntryName;
        Container::AnsiString Reason;

        [[nodiscard]] bool ShouldUseLoose() const noexcept
        {
            return Action == AssetFallbackAction::UseLoose;
        }
    };

    [[nodiscard]] bool TryParseAssetKind(Container::AnsiStringView text, AssetKind &outKind);
    [[nodiscard]] Container::AnsiString GetAssetKindName(AssetKind kind);
    [[nodiscard]] bool TryParseAssetHashHex(Container::AnsiStringView text, uint64_t &outHash);
    [[nodiscard]] Container::AnsiString FormatAssetHashHex(uint64_t hash);
    [[nodiscard]] bool TryParseAssetPackageFourCCText(Container::AnsiStringView text, AssetPackageFourCC &outFourCC);
    [[nodiscard]] Container::AnsiString FormatAssetPackageFourCCText(AssetPackageFourCC fourCC);

    class AssetManifest
    {
    public:
        static constexpr const char *DefaultVariant = "default";

        AssetManifest() = default;

        void Reset();

        [[nodiscard]] bool LoadFromJsonText(const Container::String &jsonText, Container::AnsiStringView sourceName = {});
        [[nodiscard]] AssetManifestParseStatus GetParseStatus() const noexcept { return m_ParseStatus; }
        [[nodiscard]] const Container::AnsiString &GetParseError() const noexcept { return m_ParseError; }
        [[nodiscard]] bool IsLoaded() const noexcept { return m_bLoaded; }
        [[nodiscard]] size_t GetReferenceCount() const noexcept { return m_References.size(); }
        [[nodiscard]] const AssetCookedReference &GetReference(size_t index) const noexcept { return m_References[index]; }

        [[nodiscard]] AssetManifestResolveResult Resolve(Container::AnsiStringView logicalPath,
                                                         AssetKind kind,
                                                         Container::AnsiStringView variant = DefaultVariant) const;

        [[nodiscard]] static AssetManifestResolveResult MakeManifestMissingFallback(Container::AnsiStringView logicalPath,
                                                                                    AssetKind kind,
                                                                                    Container::AnsiStringView variant = DefaultVariant);

        [[nodiscard]] static AssetFallbackDecision DecideCookedFailureFallback(const AssetCookedReference &reference,
                                                                               AssetCookedFailureKind failureKind,
                                                                               AssetFallbackMode fallbackMode,
                                                                               Container::AnsiStringView reason = {});

    private:
        bool LoadFromDocument(const JsonDocument &document);
        bool AddReference(const AssetCookedReference &reference);
        void SetParseFailure(AssetManifestParseStatus status, Container::AnsiStringView message);

        AssetManifestParseStatus m_ParseStatus = AssetManifestParseStatus::ParseError;
        Container::AnsiString m_ParseError;
        bool m_bLoadAttempted = false;
        bool m_bLoaded = false;
        Container::VariableArray<AssetCookedReference> m_References;
    };
}
