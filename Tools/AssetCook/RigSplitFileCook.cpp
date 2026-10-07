#include "RigSplitFileCook.h"
#include "AssetCookOutput.h"
#include "TextureAssetSetOutput.h"
#include "Animation/RigSplitBinding.h"
#include "Asset/CookedTextureFormat.h"
#include <chrono>
#include <fstream>
#include <cstdio>
namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace A = Core::Asset;
    namespace S = Core::Skeletal;
    namespace
    {
        struct StageDirectory
        {
            std::filesystem::path Path;
            ~StageDirectory()
            {
                if (!Path.empty())
                {
                    std::error_code ec;
                    std::filesystem::remove_all(Path, ec);
                }
            }
        };
        bool ReadSource(const std::filesystem::path& path, uint64_t maximum, C::VariableArray<uint8_t>& out)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                return false;
            }
            const auto size = input.tellg();
            if (size <= 0 || uint64_t(size) > maximum)
            {
                return false;
            }
            C::VariableArray<uint8_t> bytes(size_t(size), 0);
            input.seekg(0);
            input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!input || input.peek() != std::char_traits<char>::eof())
            {
                return false;
            }
            out = std::move(bytes);
            return true;
        }
    } // namespace
    bool CookRigSplitFile(const RigSplitCookRequest& request, const std::filesystem::path& destination,
                          RigSplitFileCookResult& out, C::AnsiString& error)
    {
        const auto fail = [&](const char* message)
        {
            error = message;
            return false;
        };
        try
        {
            std::error_code ec;
            if (destination.empty())
            {
                return fail("rig_split: missing output directory");
            }
            auto target = std::filesystem::absolute(destination, ec).lexically_normal();
            while (target != target.root_path() && !target.has_filename())
            {
                target = target.parent_path();
            }
            if (ec || std::filesystem::exists(target, ec) || ec)
            {
                return fail("rig_split: output already exists or is inaccessible");
            }
            auto selected = request;
            selected.SourcePath = std::filesystem::absolute(request.SourcePath, ec);
            if (ec)
            {
                return fail("rig_split: invalid source path");
            }
            C::VariableArray<uint8_t> source;
            if (!ReadSource(selected.SourcePath, selected.Limits.MaxSourceBytes, source))
            {
                return fail("rig_split: bounded source read failed");
            }
            RigSplitCookResult cooked;
            S::RigV1Report report;
            if (!CookRigSplitV1NativePath(source, selected, cooked, report, error))
            {
                return false;
            }
            C::VariableArray<RigSplitCookEntry> entries;
            entries.push_back(std::move(cooked.Skeleton));
            entries.push_back(std::move(cooked.Mesh));
            entries.push_back(std::move(cooked.Bank));
            // 既存package codecのstd error境界だけを使う。所有データは独自型のまま。
            std::string legacyError;
            for (const auto& image : cooked.TexturePlans)
            {
                TextureCookResult texture;
                if (image.Payload == MeshImagePayload::RawRgba8)
                {
                    if (!CookRgba8ToNvtex(image.GetBytes(), image.Width, image.Height, image.Format, texture, error))
                    {
                        return false;
                    }
                }
                else
                {
                    DecodedTextureRgba8 decoded;
                    if (!DecodeRigSplitImage(image.GetBytes(), selected.ImageLimits, decoded, error) ||
                        !CookRgba8ToNvtex(decoded.Pixels, decoded.Width, decoded.Height, image.Format, texture, error))
                    {
                        return false;
                    }
                }
                RigSplitCookEntry entry;
                entry.Payload = std::move(texture.NvtexBytes);
                auto& r = entry.Reference;
                r.LogicalPath = image.LogicalPath;
                r.Kind = A::AssetKind::Texture;
                r.SourceHash = image.SourceHash;
                r.SourceHashHex = A::FormatAssetHashHex(r.SourceHash);
                r.Variant = "default";
                r.Format = image.Format;
                r.EntryName = "__texture__";
                r.EntryType = A::MakeAssetPackageFourCC('T', 'e', 'x', '0');
                r.EntryTypeText = A::FormatAssetPackageFourCCText(r.EntryType);
                r.CookedVersion = 0;
                if (!Detail::BuildSingleSkeletalEntryPackage(r.EntryName, r.EntryType, entry.Payload, entry.Package,
                                                             r.CookedHash, legacyError))
                {
                    return fail(legacyError.c_str());
                }
                r.CookedHashHex = A::FormatAssetHashHex(r.CookedHash);
                r.CookedPackage = "Cooked/Textures/" + r.SourceHashHex + "-" + r.CookedHashHex + ".nvpk";
                entries.push_back(std::move(entry));
            }
            C::VariableArray<A::AssetCookedReference> references;
            for (const auto& entry : entries)
            {
                references.push_back(entry.Reference);
            }
            RigSplitFileCookResult candidate;
            candidate.SourceHash = cooked.SourceHash;
            candidate.JointCount = entries[0].Reference.RigSplitMetadata.JointCount;
            candidate.ClipCount = entries[2].Reference.RigSplitMetadata.ClipCount;
            candidate.TextureCount = uint32_t(entries.size() - 3);
            if (!SerializeRigSplitManifest(references, candidate.ManifestJson, error))
            {
                return false;
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec)
            {
                return fail("rig_split: cannot create output parent");
            }
            StageDirectory stage;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (unsigned attempt = 0; attempt < 64 && stage.Path.empty(); ++attempt)
            {
                char suffix[96];
                std::snprintf(suffix, sizeof(suffix), ".rig-stage-%llu-%u", static_cast<unsigned long long>(stamp),
                              attempt);
                auto path = target.parent_path() / suffix;
                if (std::filesystem::create_directory(path, ec))
                {
                    stage.Path = std::move(path);
                }
                else if (ec)
                {
                    return fail("rig_split: cannot create staging directory");
                }
            }
            if (stage.Path.empty())
            {
                return fail("rig_split: staging name collision");
            }
            for (const auto& entry : entries)
            {
                const auto path = stage.Path / std::filesystem::path(entry.Reference.CookedPackage.c_str());
                std::filesystem::create_directories(path.parent_path(), ec);
                if (ec || !Detail::WriteSkeletalBinaryFile(path, entry.Package, legacyError))
                {
                    return fail("rig_split: package write failed");
                }
            }
            if (!Detail::WriteSkeletalTextFile(stage.Path / "manifest.json", candidate.ManifestJson, legacyError))
            {
                return fail("rig_split: manifest write failed");
            }
            // AssetFileReaderのANSI root経路へ戻さず、native pathで実出力を読み直す。
            for (const auto& entry : entries)
            {
                C::VariableArray<uint8_t> actual;
                if (!ReadSource(stage.Path / std::filesystem::path(entry.Reference.CookedPackage.c_str()),
                                entry.Package.size(), actual) ||
                    actual != entry.Package)
                {
                    return fail("rig_split: cooked package verification failed");
                }
            }
            C::VariableArray<uint8_t> manifestBytes;
            if (!ReadSource(stage.Path / "manifest.json", candidate.ManifestJson.size(), manifestBytes) ||
                manifestBytes.size() != candidate.ManifestJson.size() ||
                !std::equal(manifestBytes.begin(), manifestBytes.end(), candidate.ManifestJson.begin()))
            {
                return fail("rig_split: manifest verification failed");
            }
            S::SkeletonV1 skeleton;
            S::SkinMeshV1 mesh;
            S::ClipBankV1 bank;
            S::CookedRigSplitCpuAsset cpu;
            S::RigSplitReport binding;
            if (!S::ParseSkeletonV1(entries[0].Payload, skeleton, report, selected.Limits, selected.Profile) ||
                !S::ParseSkinMeshV1(entries[1].Payload, mesh, report, selected.Limits, selected.Profile) ||
                !S::ParseClipBankV1(entries[2].Payload, bank, report, selected.Limits, selected.Profile) ||
                !S::BindRigSplitV1(skeleton, mesh, {&bank, 1}, {}, cpu, binding, selected.Limits, selected.Profile))
            {
                return fail("rig_split: three-asset binding validation failed");
            }
            // 既存のno-replace directory公開を共有する。既存出力へ部分上書きしない。
            if (!Detail::PublishNewTextureAssetSet(stage.Path, target, error))
            {
                return false;
            }
            stage.Path.clear();
            out = std::move(candidate);
            error.clear();
            return true;
        }
        catch (...)
        {
            return fail("rig_split: file operation exception");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
