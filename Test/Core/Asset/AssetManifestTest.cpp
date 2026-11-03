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

    NorvesLib::Core::Container::String SkeletalManifest()
    {
        return T(_T("{")
                 _T("\"version\":1,")
                 _T("\"assets\":[{")
                 _T("\"logical_path\":\"Models/M9.glb\",")
                 _T("\"kind\":\"model\",")
                 _T("\"source_hash\":\"0000000000000001\",")
                 _T("\"variant\":\"default\",")
                 _T("\"format\":\"nvskel.v0\",")
                 _T("\"cooked_package\":\"Cooked/m9.nvpkg\",")
                 _T("\"entry_name\":\"m9.nvskel\",")
                 _T("\"entry_type\":\"Skl0\",")
                 _T("\"cooked_hash\":\"0000000000000002\",")
                 _T("\"metadata\":{")
                 _T("\"vertex_count\":24,")
                 _T("\"index_count\":36,")
                 _T("\"joint_count\":4,")
                 _T("\"clip_count\":2")
                 _T("},\"cooked_version\":0}]}"));
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
        static_assert(static_cast<uint8_t>(AssetKind::Skeleton) == 5);
        static_assert(static_cast<uint8_t>(AssetKind::Animation) == 6);
        static_assert(static_cast<uint8_t>(AssetKind::World) == 7);

        const auto json = T(_T("{")
                            _T("\"version\":1,")
                            _T("\"assets\":[{")
                            _T("\"logical_path\":\"Worlds/Valley.terrain\",")
                            _T("\"kind\":\"world\",")
                            _T("\"source_hash\":\"0000000000000001\",")
                            _T("\"variant\":\"terrain\",")
                            _T("\"format\":\"nvtrn.v0.height16\",")
                            _T("\"cooked_package\":\"Cooked/Worlds.nvpkg\",")
                            _T("\"entry_name\":\"Worlds/Valley.nvtrn\",")
                            _T("\"entry_type\":\"Trn0\",")
                            _T("\"cooked_hash\":\"0000000000000002\",")
                            _T("\"cooked_version\":0}]}"));
        AssetManifest manifest;
        assert(LoadManifestText(json, manifest));
        assert(manifest.GetReferenceCount() == 1);
        const AssetCookedReference &reference = manifest.GetReference(0);
        assert(reference.Kind == AssetKind::World);
        assert(reference.EntryType == MakeAssetPackageFourCC('T', 'r', 'n', '0'));
        assert(reference.Format == "nvtrn.v0.height16");
        assert(GetAssetKindName(AssetKind::World) == "world");
        AssetKind parsedKind = AssetKind::Unknown;
        assert(TryParseAssetKind("world", parsedKind));
        assert(parsedKind == AssetKind::World);

        const AssetManifestResolveResult result = manifest.Resolve("Worlds/Valley.terrain", AssetKind::World, "terrain");
        assert(result.Status == AssetManifestResolveStatus::CookedReferenceFound);
        assert(result.Reference.Kind == AssetKind::World);
        assert(result.Reference.EntryType == MakeAssetPackageFourCC('T', 'r', 'n', '0'));
        assert(result.Reference.Format == "nvtrn.v0.height16");
        const AssetManifestResolveResult otherKind = manifest.Resolve("Worlds/Valley.terrain", AssetKind::Model, "terrain");
        assert(otherKind.Status == AssetManifestResolveStatus::LooseFallbackVariantMissing);
        assert(!otherKind.ShouldUseCooked());
    }

    {
        // world が無い旧 manifest でも既存種別を保持し、異なる種別では解決しない。
        const auto json = T(_T("{")
                            _T("\"version\":1,")
                            _T("\"assets\":[{")
                            _T("\"logical_path\":\"Textures/Stone.png\",")
                            _T("\"kind\":\"texture\",")
                            _T("\"source_hash\":\"0000000000000001\",")
                            _T("\"variant\":\"default\",")
                            _T("\"format\":\"nvtex.v0.rgba8.srgb\",")
                            _T("\"cooked_package\":\"Cooked/Legacy.nvpkg\",")
                            _T("\"entry_name\":\"Textures/Stone.nvtex\",")
                            _T("\"entry_type\":\"Tex0\",")
                            _T("\"cooked_hash\":\"0000000000000002\",")
                            _T("\"cooked_version\":0},{")
                            _T("\"logical_path\":\"Models/Stone.glb\",")
                            _T("\"kind\":\"model\",")
                            _T("\"source_hash\":\"0000000000000003\",")
                            _T("\"variant\":\"default\",")
                            _T("\"format\":\"nvmesh.v0\",")
                            _T("\"cooked_package\":\"Cooked/Legacy.nvpkg\",")
                            _T("\"entry_name\":\"Models/Stone.nvmesh\",")
                            _T("\"entry_type\":\"Msh0\",")
                            _T("\"cooked_hash\":\"0000000000000004\",")
                            _T("\"cooked_version\":0},{")
                            _T("\"logical_path\":\"Skeletons/Rig\",")
                            _T("\"kind\":\"skeleton\",")
                            _T("\"source_hash\":\"0000000000000005\",")
                            _T("\"variant\":\"default\",")
                            _T("\"format\":\"nvskel.v1.skeleton\",")
                            _T("\"cooked_package\":\"Cooked/Legacy.nvpkg\",")
                            _T("\"entry_name\":\"Skeletons/Rig.nvskel\",")
                            _T("\"entry_type\":\"Ske1\",")
                            _T("\"cooked_hash\":\"0000000000000006\",")
                            _T("\"cooked_version\":1,")
                            _T("\"metadata\":{\"skeleton_id\":\"0000000000000001\",\"profile\":1,\"joint_count\":1}},{")
                            _T("\"logical_path\":\"Animations/Run\",")
                            _T("\"kind\":\"animation\",")
                            _T("\"source_hash\":\"0000000000000007\",")
                            _T("\"variant\":\"default\",")
                            _T("\"format\":\"nvskel.v1.clips\",")
                            _T("\"cooked_package\":\"Cooked/Legacy.nvpkg\",")
                            _T("\"entry_name\":\"Animations/Run.nvclip\",")
                            _T("\"entry_type\":\"Anm1\",")
                            _T("\"cooked_hash\":\"0000000000000008\",")
                            _T("\"cooked_version\":1,")
                            _T("\"metadata\":{\"skeleton_id\":\"0000000000000001\",\"profile\":1,\"joint_count\":1,")
                            _T("\"clip_count\":1,\"snapshot_count\":1,\"channel_count\":1,\"sample_count\":1}}]}"));

        AssetManifest manifest;
        assert(LoadManifestText(json, manifest));
        assert(manifest.GetReferenceCount() == 4);

        const AssetCookedReference &texture = manifest.GetReference(0);
        assert(texture.LogicalPath == "Textures/Stone.png");
        assert(texture.Kind == AssetKind::Texture);
        assert(texture.EntryType == MakeAssetPackageFourCC('T', 'e', 'x', '0'));
        const AssetCookedReference &model = manifest.GetReference(1);
        assert(model.LogicalPath == "Models/Stone.glb");
        assert(model.Kind == AssetKind::Model);
        assert(model.EntryType == MakeAssetPackageFourCC('M', 's', 'h', '0'));
        const AssetCookedReference &skeleton = manifest.GetReference(2);
        assert(skeleton.LogicalPath == "Skeletons/Rig");
        assert(skeleton.Kind == AssetKind::Skeleton);
        assert(skeleton.EntryType == MakeAssetPackageFourCC('S', 'k', 'e', '1'));
        const AssetCookedReference &animation = manifest.GetReference(3);
        assert(animation.LogicalPath == "Animations/Run");
        assert(animation.Kind == AssetKind::Animation);
        assert(animation.EntryType == MakeAssetPackageFourCC('A', 'n', 'm', '1'));

        const AssetManifestResolveResult textureResult = manifest.Resolve("Textures/Stone.png", AssetKind::Texture);
        assert(textureResult.Status == AssetManifestResolveStatus::CookedReferenceFound);
        const AssetManifestResolveResult worldResult = manifest.Resolve("Textures/Stone.png", AssetKind::World);
        assert(worldResult.Status == AssetManifestResolveStatus::LooseFallbackVariantMissing);
        assert(!worldResult.ShouldUseCooked());
    }

    {
        AssetManifest manifest;
        assert(LoadManifestText(SkeletalManifest(), manifest));
        const AssetCookedReference &reference = manifest.GetReference(0);
        assert(reference.bHasSkeletalMetadata);
        assert(reference.SkeletalMetadata.VertexCount == 24);
        assert(reference.SkeletalMetadata.IndexCount == 36);
        assert(reference.SkeletalMetadata.JointCount == 4);
        assert(reference.SkeletalMetadata.ClipCount == 2);
        assert(!reference.SkeletalMetadata.bHasSubmeshCounts && reference.SkeletalMetadata.SubmeshCount == 0 &&
            reference.SkeletalMetadata.MaterialSlotCount == 0);
    }

    for (const TCHAR* fields : {_T("\"clip_count\":2,\"submesh_count\":1,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":2,\"material_slot_count\":2"),
        _T("\"clip_count\":2,\"submesh_count\":8,\"material_slot_count\":8")})
    {
        AssetManifest manifest;
        assert(LoadManifestText(Replace(SkeletalManifest(),_T("\"clip_count\":2"),fields),manifest));
        const auto& metadata = manifest.GetReference(0).SkeletalMetadata;
        assert(metadata.bHasSubmeshCounts && metadata.SubmeshCount == metadata.MaterialSlotCount);
    }
    for (const TCHAR* fields : {_T("\"clip_count\":2,\"submesh_count\":1"),
        _T("\"clip_count\":2,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":0,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":9,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":1,\"material_slot_count\":9"),
        _T("\"clip_count\":2,\"submesh_count\":-1,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":1.5,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":null,\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":\"1\",\"material_slot_count\":1"),
        _T("\"clip_count\":2,\"submesh_count\":4294967296,\"material_slot_count\":1")})
    {
        AssetManifest manifest;
        assert(!LoadManifestText(Replace(SkeletalManifest(),_T("\"clip_count\":2"),fields),manifest));
        assert(manifest.GetParseStatus() == AssetManifestParseStatus::InvalidField);
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
