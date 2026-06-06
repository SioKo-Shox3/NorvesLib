#include "Asset/AssetManifest.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <tchar.h>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Asset;

namespace
{
    NorvesLib::Core::Container::String T(const TCHAR *text)
    {
        return NorvesLib::Core::Container::String(text);
    }

    NorvesLib::Core::Container::String ValidManifest()
    {
        return T(_T("{")
                 _T("\"version\":1,")
                 _T("\"assets\":[")
                 _T("{")
                 _T("\"logical_path\":\"Textures\\\\Silver.png\",")
                 _T("\"kind\":\"texture\",")
                 _T("\"source_hash\":\"0123456789abcdef\",")
                 _T("\"variant\":\"default\",")
                 _T("\"format\":\"nvtex.v0.rgba8.srgb\",")
                 _T("\"cooked_package\":\"Cooked/Textures.nvpkg\",")
                 _T("\"entry_name\":\"Textures/Silver.nvtex\",")
                 _T("\"entry_type\":\"Tex0\",")
                 _T("\"cooked_hash\":\"fedcba9876543210\",")
                 _T("\"cooked_version\":0")
                 _T("}")
                 _T("]")
                 _T("}"));
    }

    bool LoadManifestText(const NorvesLib::Core::Container::String &text, AssetManifest &manifest)
    {
        return manifest.LoadFromJsonText(text, "test.manifest.json");
    }

    NorvesLib::Core::Container::String Replace(const NorvesLib::Core::Container::String &source,
                                               const TCHAR *from,
                                               const TCHAR *to)
    {
        std::basic_string<TCHAR> result(source.data(), source.size());
        const std::basic_string<TCHAR> fromText(from);
        const size_t position = result.find(fromText);
        assert(position != std::basic_string<TCHAR>::npos);
        result.replace(position, fromText.size(), std::basic_string<TCHAR>(to));
        return NorvesLib::Core::Container::String(result);
    }

    AssetManifestParseStatus LoadInvalidField(const TCHAR *from, const TCHAR *to)
    {
        AssetManifest manifest;
        const bool loaded = LoadManifestText(Replace(ValidManifest(), from, to), manifest);
        assert(!loaded);
        return manifest.GetParseStatus();
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "AssetManifestTest start\n";

    {
        uint64_t hash = 0;
        assert(TryParseAssetHashHex("0123456789abcdef", hash));
        assert(hash == 0x0123456789abcdefull);
        assert(FormatAssetHashHex(hash) == "0123456789abcdef");
        assert(!TryParseAssetHashHex("0123456789ABCDEF", hash));
        assert(!TryParseAssetHashHex("1234", hash));
        assert(!TryParseAssetHashHex("0123456789abcdeg", hash));

        AssetPackageFourCC fourCC = 0;
        assert(TryParseAssetPackageFourCCText("Tex0", fourCC));
        assert(fourCC == MakeAssetPackageFourCC('T', 'e', 'x', '0'));
        assert(TryParseAssetPackageFourCCText("Raw ", fourCC));
        assert(fourCC == MakeAssetPackageFourCC('R', 'a', 'w', ' '));
        assert(!TryParseAssetPackageFourCCText("Tex", fourCC));
        const char nonAsciiFourCC[] = {'T', 'e', 'x', static_cast<char>(0x80), '\0'};
        assert(!TryParseAssetPackageFourCCText(nonAsciiFourCC, fourCC));
    }

    {
        AssetManifest manifest;
        assert(LoadManifestText(ValidManifest(), manifest));
        assert(manifest.IsLoaded());
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::Success);
        assert(manifest.GetReferenceCount() == 1);

        const AssetCookedReference &reference = manifest.GetReference(0);
        assert(reference.LogicalPath == "Textures/Silver.png");
        assert(reference.Kind == AssetKind::Texture);
        assert(reference.SourceHash == 0x0123456789abcdefull);
        assert(reference.CookedHash == 0xfedcba9876543210ull);
        assert(reference.EntryType == MakeAssetPackageFourCC('T', 'e', 'x', '0'));

        const AssetManifestResolveResult result = manifest.Resolve("Textures/Silver.png", AssetKind::Texture);
        assert(result.Status == AssetManifestResolveStatus::CookedReferenceFound);
        assert(result.ShouldUseCooked());
        assert(result.Reference.CookedPackage == "Cooked/Textures.nvpkg");
    }

    {
        AssetManifest manifest;
        assert(!LoadManifestText(T(_T("{")), manifest));
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::ParseError);
        const AssetManifestResolveResult result = manifest.Resolve("Textures/Silver.png", AssetKind::Texture);
        assert(result.Status == AssetManifestResolveStatus::InvalidManifest);
    }

    {
        AssetManifest manifest;
        assert(!LoadManifestText(T(_T("[]")), manifest));
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::RootNotObject);
    }

    {
        AssetManifest manifest;
        assert(!LoadManifestText(Replace(ValidManifest(), _T("\"version\":1"), _T("\"version\":2")), manifest));
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::UnsupportedVersion);
        const AssetManifestResolveResult result = manifest.Resolve("Textures/Silver.png", AssetKind::Texture);
        assert(result.Status == AssetManifestResolveStatus::InvalidManifest);
    }

    {
        AssetManifest manifest;
        assert(!LoadManifestText(Replace(ValidManifest(), _T("\"version\":1"), _T("\"version\":1.5")), manifest));
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::UnsupportedVersion);
    }

    {
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\","), _T("")) == AssetManifestParseStatus::RequiredFieldMissing);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"C:/Textures/Silver.png\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"//server/share/Silver.png\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"C:Textures/Silver.png\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"../Silver.png\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"Textures\\nSilver.png\"")) == AssetManifestParseStatus::RequiredFieldMissing);
        assert(LoadInvalidField(_T("\"logical_path\":\"Textures\\\\Silver.png\""), _T("\"logical_path\":\"Textures\\u007fSilver.png\"")) == AssetManifestParseStatus::RequiredFieldMissing);
        assert(LoadInvalidField(_T("\"cooked_package\":\"Cooked/Textures.nvpkg\""), _T("\"cooked_package\":\"C:/Cooked/Textures.nvpkg\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"entry_name\":\"Textures/Silver.nvtex\""), _T("\"entry_name\":\"../Silver.nvtex\"")) == AssetManifestParseStatus::InvalidField);
    }

    {
        assert(LoadInvalidField(_T("\"kind\":\"texture\""), _T("\"kind\":\"unknown\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"source_hash\":\"0123456789abcdef\""), _T("\"source_hash\":\"0123456789ABCDEF\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"cooked_hash\":\"fedcba9876543210\""), _T("\"cooked_hash\":\"fedcba\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"source_hash\":\"0123456789abcdef\""), _T("\"source_hash\":\"0123456789abcdeg\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"entry_type\":\"Tex0\""), _T("\"entry_type\":\"Tex\"")) == AssetManifestParseStatus::InvalidField);
        assert(LoadInvalidField(_T("\"format\":\"nvtex.v0.rgba8.srgb\""), _T("\"format\":\"\"")) == AssetManifestParseStatus::InvalidField);
    }

    {
        AssetManifest manifest;
        const NorvesLib::Core::Container::String duplicated =
            T(_T("{")
              _T("\"version\":1,")
              _T("\"assets\":[")
              _T("{\"logical_path\":\"Textures/a.png\",\"kind\":\"texture\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"nvtex\",\"cooked_package\":\"Cooked/a.nvpkg\",\"entry_name\":\"Textures/a.nvtex\",\"entry_type\":\"Tex0\",\"cooked_hash\":\"0000000000000002\",\"cooked_version\":0},")
              _T("{\"logical_path\":\"Textures\\\\a.png\",\"kind\":\"texture\",\"source_hash\":\"0000000000000003\",\"variant\":\"default\",\"format\":\"nvtex\",\"cooked_package\":\"Cooked/b.nvpkg\",\"entry_name\":\"Textures/b.nvtex\",\"entry_type\":\"Tex0\",\"cooked_hash\":\"0000000000000004\",\"cooked_version\":0}")
              _T("]")
              _T("}"));
        assert(!LoadManifestText(duplicated, manifest));
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::DuplicateAsset);
    }

    {
        AssetManifest manifest;
        const AssetManifestResolveResult missing = manifest.Resolve("Textures/Silver.png", AssetKind::Texture);
        assert(missing.Status == AssetManifestResolveStatus::LooseFallbackManifestMissing);
        assert(missing.ShouldUseLooseFallback());
    }

    {
        AssetManifest manifest;
        assert(LoadManifestText(ValidManifest(), manifest));
        const AssetManifestResolveResult missingVariant = manifest.Resolve("Textures/Silver.png", AssetKind::Texture, "debug");
        assert(missingVariant.Status == AssetManifestResolveStatus::LooseFallbackVariantMissing);
        assert(missingVariant.ShouldUseLooseFallback());
    }

    {
        AssetManifest manifest;
        assert(LoadManifestText(ValidManifest(), manifest));
        const AssetCookedReference &reference = manifest.GetReference(0);

        const AssetFallbackDecision defaultDecision =
            AssetManifest::DecideCookedFailureFallback(reference,
                                                       AssetCookedFailureKind::PackageParseFailed,
                                                       AssetFallbackMode::FailOnCookedFailure,
                                                       "parse failed");
        assert(defaultDecision.Action == AssetFallbackAction::Fail);
        assert(!defaultDecision.bRequiresExplicitLog);
        assert(defaultDecision.LogicalPath == "Textures/Silver.png");
        assert(defaultDecision.CookedPackage == "Cooked/Textures.nvpkg");
        assert(defaultDecision.EntryName == "Textures/Silver.nvtex");
        assert(defaultDecision.FailureKind == AssetCookedFailureKind::PackageParseFailed);
        assert(defaultDecision.Reason == "parse failed");

        const AssetFallbackDecision debugDecision =
            AssetManifest::DecideCookedFailureFallback(reference,
                                                       AssetCookedFailureKind::EntryHashMismatch,
                                                       AssetFallbackMode::DebugAllowLooseFallback,
                                                       "hash mismatch");
        assert(debugDecision.Action == AssetFallbackAction::UseLoose);
        assert(debugDecision.bRequiresExplicitLog);
        assert(debugDecision.ShouldUseLoose());
        assert(debugDecision.LogicalPath == "Textures/Silver.png");
        assert(debugDecision.CookedPackage == "Cooked/Textures.nvpkg");
        assert(debugDecision.EntryName == "Textures/Silver.nvtex");
        assert(debugDecision.FailureKind == AssetCookedFailureKind::EntryHashMismatch);
        assert(debugDecision.Reason == "hash mismatch");
    }

    std::cout << "AssetManifestTest passed\n";
    return 0;
}
