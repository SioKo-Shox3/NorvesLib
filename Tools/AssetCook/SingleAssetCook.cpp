// 再利用可能な単体cook。入力bufferを出力完了まで同じ呼出し内で保持する。
#include "SingleAssetCook.h"
#include "RigSingleCook.h"
#include "SkeletalRoleFileInput.h"
#include "SkeletalRoleFileCook.h"
#include "MeshMaterialV1Plan.h"
#include "CookOutputSetGuard.h"
#include "NativeCookPath.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "AssetCookLegacyOptions.h"
#include "AssetCookOutput.h"

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using namespace Detail;
        bool CookRawAsset(const CookOptions &options, std::string &error)
        {
            std::filesystem::path inputPath;
            std::filesystem::path packagePath;
            std::filesystem::path manifestPath;
            if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
                !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
                !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
            {
                return false;
            }

            std::vector<uint8_t> inputBytes;
            if (!ReadBinaryFile(inputPath, inputBytes, error))
            {
                return false;
            }

            std::string logicalPath;
            std::string entryName;
            if (!NormalizeManifestPathField("logical_path", options.LogicalPath, logicalPath, error) ||
                !NormalizeManifestPathField("entry_name", options.EntryName, entryName, error) ||
                !ValidateAsciiJsonField("variant", options.Variant, error) ||
                !ValidateAsciiJsonField("format", options.Format, error))
            {
                return false;
            }

            AssetPackageFourCC entryType = 0;
            std::string entryTypeText;
            if (!ParseEntryType(options.EntryTypeText, entryType, entryTypeText, error))
            {
                return false;
            }

            const std::filesystem::path manifestParent = manifestPath.parent_path();
            std::string cookedPackagePath;
            if (!MakeCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
            {
                return false;
            }

            uint64_t payloadHash = 0;
            std::vector<uint8_t> packageBytes;
            if (!BuildSingleEntryPackage(entryName, entryType, inputBytes, packageBytes, payloadHash, error))
            {
                return false;
            }

            std::string manifestJson;
            if (!BuildManifestJson(logicalPath,
                                   options.Kind,
                                   payloadHash,
                                   options.Variant,
                                   options.Format,
                                   cookedPackagePath,
                                   entryName,
                                   entryTypeText,
                                   payloadHash,
                                   manifestJson,
                                   error))
            {
                return false;
            }

            if (!WriteBinaryFile(packagePath, packageBytes, error) ||
                !WriteTextFile(manifestPath, manifestJson, error))
            {
                return false;
            }

            if (!ValidatePackageOutput(packagePath, entryName, entryType, inputBytes, error) ||
                !ValidateManifestOutput(manifestPath, manifestJson, error) ||
                !ValidateAssetSystemOutput(manifestPath, manifestJson, logicalPath, AssetKind::Raw, options.Variant, inputBytes, error))
            {
                return false;
            }

            std::cerr << "AssetCook wrote package=\"" << packagePath.generic_string()
                      << "\" manifest=\"" << manifestPath.generic_string()
                      << "\" bytes=" << inputBytes.size() << "\n";
            return true;
        }

        bool CookTextureAsset(const CookOptions &options, std::string &error)
        {
            std::filesystem::path inputPath;
            std::filesystem::path packagePath;
            std::filesystem::path manifestPath;
            if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
                !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
                !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
            {
                return false;
            }

            std::vector<uint8_t> inputBytes;
            if (!ReadBinaryFile(inputPath, inputBytes, error))
            {
                return false;
            }

            std::string logicalPath;
            std::string entryName;
            if (!NormalizeManifestPathField("logical_path", options.LogicalPath, logicalPath, error) ||
                !NormalizeManifestPathField("entry_name", options.EntryName, entryName, error) ||
                !ValidateAsciiJsonField("variant", options.Variant, error) ||
                !ValidateAsciiJsonField("format", options.Format, error))
            {
                return false;
            }

            AssetPackageFourCC entryType = 0;
            std::string entryTypeText;
            if (!ParseEntryType(options.EntryTypeText, entryType, entryTypeText, error))
            {
                return false;
            }

            if (entryTypeText != "Tex0")
            {
                error = "--kind texture requires --entry-type Tex0";
                return false;
            }

            // source名は診断用でもACPへ縮約せず、native pathからUTF8へ明示変換する。
            NorvesLib::Core::Container::AnsiString sourceNameUtf8;
            if (!EncodeCookPathUtf8(inputPath, sourceNameUtf8))
            {
                error = "texture source path cannot be encoded as UTF-8";
                return false;
            }
            NorvesLib::Tools::AssetCook::TextureCookResult textureResult;
            if (!NorvesLib::Tools::AssetCook::CookTextureToNvtex(inputBytes.data(),
                                                                 inputBytes.size(),
                                                                 options.Format,
                                                                 std::string_view(sourceNameUtf8.data(),sourceNameUtf8.size()),
                                                                 textureResult,
                                                                 error))
            {
                return false;
            }

            if (!ValidateCookedTexturePayload(textureResult.NvtexBytes, error))
            {
                return false;
            }

            const std::filesystem::path manifestParent = manifestPath.parent_path();
            std::string cookedPackagePath;
            if (!MakeCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
            {
                return false;
            }

            uint64_t cookedHash = 0;
            std::vector<uint8_t> packageBytes;
            if (!BuildSingleEntryPackage(entryName, entryType, textureResult.NvtexBytes, packageBytes, cookedHash, error))
            {
                return false;
            }

            const uint64_t sourceHash = ComputeAssetPackagePayloadHash(inputBytes.data(), inputBytes.size());
            std::string manifestJson;
            if (!BuildManifestJson(logicalPath,
                                   options.Kind,
                                   sourceHash,
                                   options.Variant,
                                   options.Format,
                                   cookedPackagePath,
                                   entryName,
                                   entryTypeText,
                                   cookedHash,
                                   manifestJson,
                                   error))
            {
                return false;
            }

            if (!WriteBinaryFile(packagePath, packageBytes, error) ||
                !WriteTextFile(manifestPath, manifestJson, error))
            {
                return false;
            }

            if (!ValidateCookedTexturePackageOutput(packagePath, entryName, entryType, textureResult.NvtexBytes, error) ||
                !ValidateManifestOutput(manifestPath, manifestJson, error) ||
                !ValidateAssetSystemOutput(manifestPath,
                                           manifestJson,
                                           logicalPath,
                                           AssetKind::Texture,
                                           options.Variant,
                                           textureResult.NvtexBytes,
                                           error))
            {
                return false;
            }

            std::cerr << "AssetCook wrote texture package=\"" << packagePath.generic_string()
                      << "\" manifest=\"" << manifestPath.generic_string()
                      << "\" source_bytes=" << inputBytes.size()
                      << " nvtex_bytes=" << textureResult.NvtexBytes.size()
                      << " width=" << textureResult.Width
                      << " height=" << textureResult.Height
                      << " mips=" << textureResult.MipCount
                      << " bytes_per_pixel=" << textureResult.BytesPerPixel
                      << "\n";
            return true;
        }

        bool SameCookOutputPath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
    #if defined(_WIN32)
            // Windowsの通常path比較は大文字小文字を区別しない。未作成fileのaliasも拒否する。
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    #else
            return left == right;
    #endif
        }

        bool GuardImportSettingsOutput(const std::filesystem::path& output,
                                       const std::filesystem::path& sidecar, std::string& error)
        {
            std::error_code sidecarError, outputError, equivalentError;
            const auto canonicalSidecar = std::filesystem::weakly_canonical(sidecar, sidecarError);
            const auto canonicalOutput = std::filesystem::weakly_canonical(output, outputError);
            if (sidecar.empty() || sidecarError || outputError)
            {
                error = "failed to canonicalize import settings/output";
                return false;
            }
            const bool bSameFile = std::filesystem::equivalent(output, sidecar, equivalentError);
            if (SameCookOutputPath(canonicalOutput, canonicalSidecar) || (!equivalentError && bSameFile))
            {
                error = "model/texture output must not alias import settings";
                return false;
            }
            return true;
        }

        bool CookEmbeddedModelAssets(const CookOptions& options,
                                     const std::filesystem::path& inputPath,
                                     const std::filesystem::path& packagePath,
                                     const std::filesystem::path& manifestPath,
                                     NorvesLib::Core::Container::AnsiStringView logicalPath,
                                     NorvesLib::Core::Container::AnsiStringView entryName,
                                     const NorvesLib::Tools::AssetCook::MeshCookResult& mesh,
                                     std::string& error)
        {
            using NorvesLib::Core::Container::AnsiString;
            using NorvesLib::Core::Container::VariableArray;
            using NorvesLib::Core::Asset::AssetCookedReference;
            struct PendingPackage
            {
                std::filesystem::path Path;
                std::filesystem::path CanonicalPath;
                AssetCookedReference Reference;
                VariableArray<uint8_t> Payload;
                VariableArray<uint8_t> PackageBytes;
            };
            VariableArray<PendingPackage> pending;
            pending.reserve(mesh.EmbeddedImages.size() + 1);
            PendingPackage model;
            model.Path = packagePath;
            model.Payload = mesh.NvmeshBytes;
            model.Reference.LogicalPath = AnsiString(logicalPath);
            model.Reference.Kind = AssetKind::Model;
            model.Reference.SourceHash = mesh.SourceHash;
            model.Reference.Variant = options.Variant.c_str();
            model.Reference.Format = options.Format.c_str();
            model.Reference.EntryName = AnsiString(entryName);
            model.Reference.EntryType = MakeAssetPackageFourCC('M', 's', 'h', '0');
            model.Reference.CookedVersion = mesh.VersionMajor;
            pending.push_back(std::move(model));

            // 全textureを変換してから書く。encodedと明示rawを混同しない。
            for (const auto& image : mesh.EmbeddedImages)
            {
                const auto bytes = image.GetBytes();
                NorvesLib::Tools::AssetCook::TextureCookResult texture;
                if (image.Payload == MeshImagePayload::RawRgba8)
                {
                    AnsiString reason;
                    if (!CookRgba8ToNvtex(bytes, image.Width, image.Height, image.Format, texture, reason))
                    {
                        error.assign(reason.data(), reason.size());
                        return false;
                    }
                }
                else
                {
                    const auto mime = NorvesLib::Core::Gltf::ProbeEmbeddedImageMime(bytes);
                    if (image.Payload != MeshImagePayload::Encoded ||
                        (mime != NorvesLib::Core::Gltf::DataUriMime::Png &&
                         mime != NorvesLib::Core::Gltf::DataUriMime::Jpeg))
                    {
                        error = "embedded image must contain PNG or JPEG bytes";
                        return false;
                    }
                    if (!NorvesLib::Tools::AssetCook::CookTextureToNvtex(bytes.data(), bytes.size(),
                                                                         image.Format.c_str(),
                                                                         image.LogicalPath.c_str(), texture, error))
                    {
                        return false;
                    }
                }
                PendingPackage item;
                char indexText[32] = {};
                const auto converted = std::to_chars(indexText, indexText + sizeof(indexText), image.ImageIndex);
                if (converted.ec != std::errc{})
                {
                    error = "embedded image index formatting failed";
                    return false;
                }
                AnsiString filename(packagePath.filename().generic_string().c_str());
                filename += ".img";
                filename.append(indexText, static_cast<size_t>(converted.ptr - indexText));
                filename += ".nvpkg";
                item.Path = packagePath.parent_path() / std::filesystem::path(filename.begin(), filename.end());
                item.Payload.assign(texture.NvtexBytes.begin(), texture.NvtexBytes.end());
                item.Reference.LogicalPath = image.LogicalPath;
                item.Reference.Kind = AssetKind::Texture;
                item.Reference.SourceHash = image.SourceHash;
                // NVMESH材質はpathのみを持ち、TextureAssetResolverがdefault variantを解決する。
                item.Reference.Variant = "default";
                item.Reference.Format = image.Format;
                item.Reference.EntryName = "__texture__";
                item.Reference.EntryType = MakeAssetPackageFourCC('T', 'e', 'x', '0');
                pending.push_back(std::move(item));
            }

            // 安定したfilesystemを前提に、既存symlinkと未作成fileのWindows case aliasも検査する。
            std::error_code pathError;
            const auto canonicalInput = std::filesystem::weakly_canonical(inputPath, pathError);
            if (pathError)
            {
                error = "failed to canonicalize embedded model input";
                return false;
            }
            const auto canonicalManifest = std::filesystem::weakly_canonical(manifestPath, pathError);
            if (pathError)
            {
                error = "failed to canonicalize embedded model manifest";
                return false;
            }
            for (size_t index = 0; index < pending.size(); ++index)
            {
                auto& item = pending[index];
                if (mesh.bHasImportSettings && !GuardImportSettingsOutput(item.Path,
                        mesh.ImportSettingsPath, error))
                {
                    return false;
                }
                item.CanonicalPath = std::filesystem::weakly_canonical(item.Path, pathError);
                if (pathError)
                {
                    error = "failed to canonicalize model/texture output";
                    return false;
                }
                std::error_code inputError;
                std::error_code manifestError;
                const bool bSameInput = std::filesystem::equivalent(item.Path, inputPath, inputError);
                const bool bSameManifest = std::filesystem::equivalent(item.Path, manifestPath, manifestError);
                if (SameCookOutputPath(item.CanonicalPath, canonicalInput) || SameCookOutputPath(item.CanonicalPath, canonicalManifest) ||
                    (!inputError && bSameInput) || (!manifestError && bSameManifest))
                {
                    error = "model/texture output must not alias source or manifest";
                    return false;
                }
                for (size_t previous = 0; previous < index; ++previous)
                {
                    std::error_code equivalentError;
                    const bool bSameFile = std::filesystem::equivalent(pending[previous].Path, item.Path, equivalentError);
                    if (SameCookOutputPath(pending[previous].CanonicalPath, item.CanonicalPath) || (!equivalentError && bSameFile))
                    {
                        error = "model/texture output paths must be unique";
                        return false;
                    }
                }
            }
            VariableArray<AssetCookedReference> references;
            references.reserve(pending.size());
            for (auto& item : pending)
            {
                auto& reference = item.Reference;
                reference.SourceHashHex = FormatAssetHashHex(reference.SourceHash);
                reference.EntryTypeText = FormatAssetPackageFourCCText(reference.EntryType);
                if (!MakeSkeletalCookedPackageManifestPath(item.Path, manifestPath.parent_path(), reference.CookedPackage, error) ||
                    !BuildSingleSkeletalEntryPackage(reference.EntryName, reference.EntryType, item.Payload,
                        item.PackageBytes, reference.CookedHash, error))
                {
                    return false;
                }
                reference.CookedHashHex = FormatAssetHashHex(reference.CookedHash);
                NorvesLib::FileStream::Package package;
                NorvesLib::FileStream::PackageEntry entry;
                if (!package.LoadFromMemory({item.PackageBytes.data(), item.PackageBytes.size()}) ||
                    !package.FindEntry(reference.EntryName, reference.EntryType, entry) || entry.PayloadHash != reference.CookedHash)
                {
                    error = "self-validation failed: generated model/texture package is invalid";
                    return false;
                }
                const auto blob = package.OpenEntry(entry);
                if (!blob.IsValid() || !CompareSkeletalBytes(blob.GetData(), blob.GetSize(), item.Payload) ||
                    (reference.Kind == AssetKind::Model ? !ParseCookedMesh(blob).Succeeded() : !ParseCookedTexture(blob).Succeeded()))
                {
                    error = "self-validation failed: generated model/texture payload is invalid";
                    return false;
                }
                references.push_back(reference);
            }
            AnsiString manifestJson;
            if (!BuildMergedManifestJson(manifestPath, {references.data(), references.size()}, manifestJson, error))
            {
                return false;
            }
            AssetSystem system{AnsiString(manifestPath.parent_path().generic_string().c_str())};
            const AnsiString sourceName(manifestPath.generic_string().c_str());
            if (!system.LoadManifestFromJsonText(ToCoreString(manifestJson), sourceName))
            {
                error = "self-validation failed: generated model/texture manifest is invalid";
                return false;
            }
            // 保持した別keyのbacking packageを単一entry出力で上書きしない。
            for (size_t index = 0; index < system.GetAssetCount(); ++index)
            {
                const auto& retained = system.GetAssetReference(index);
                const bool bIncoming = std::any_of(references.begin(), references.end(), [&](const auto& reference)
                {
                    return HasSameManifestKey(retained, reference);
                });
                if (bIncoming)
                {
                    continue;
                }
                const auto retainedPath = manifestPath.parent_path() /
                    std::filesystem::path(retained.CookedPackage.begin(), retained.CookedPackage.end());
                const auto canonicalRetained = std::filesystem::weakly_canonical(retainedPath, pathError);
                if (pathError)
                {
                    error = "failed to canonicalize retained asset package";
                    return false;
                }
                for (const auto& item : pending)
                {
                    std::error_code equivalentError;
                    const bool bSameFile = std::filesystem::equivalent(retainedPath, item.Path, equivalentError);
                    if (SameCookOutputPath(canonicalRetained, item.CanonicalPath) || (!equivalentError && bSameFile))
                    {
                        error = "model/texture output package is referenced by another asset key";
                        return false;
                    }
                }
            }
            for (const auto& item : pending)
            {
                if (!WriteSkeletalBinaryFile(item.Path, item.PackageBytes, error))
                {
                    return false;
                }
            }
            for (const auto& item : pending)
            {
                const auto& reference = item.Reference;
                const auto resolved = system.ResolveAsset(reference.LogicalPath, reference.Kind, reference.Variant);
                if (!resolved.Succeeded() || resolved.Status != AssetResolveStatus::SuccessCooked || !resolved.UsedCooked() ||
                    !CompareSkeletalBytes(resolved.Blob.GetData(), resolved.Blob.GetSize(), item.Payload))
                {
                    error = "self-validation failed: written model/texture asset could not be resolved";
                    return false;
                }
            }
            // package群を実際に解決できてからmanifestを1回だけ書く。multi-file transactionではない。
            if (!WriteSkeletalTextFile(manifestPath, manifestJson, error))
            {
                return false;
            }
            std::cerr << "AssetCook wrote embedded model package=\"" << packagePath.generic_string()
                      << "\" manifest=\"" << manifestPath.generic_string()
                      << "\" textures=" << mesh.EmbeddedImages.size()
                      << " vertices=" << mesh.VertexCount << " indices=" << mesh.IndexCount << "\n";
            return true;
        }

        void PrintSkeletalImportReport(const NorvesLib::Tools::AssetCook::SkeletalImportReportInput& input)
        {
            const auto json = NorvesLib::Tools::AssetCook::BuildSkeletalImportReport(input);
            if (json.bValid)
            {
                std::cerr << "import_report=";
                std::cerr.write(json.Bytes, static_cast<std::streamsize>(json.Size));
                std::cerr << "\n";
            }
            else std::cerr << "\xe9\xaa\xa8\xe6\xa0\xbc\x49\x6d\x70\x6f\x72\x74\x52\x65\x70\x6f\x72\x74\xe3\x81\xae\xe6\xb8\xac\xe5\xae\x9a\xe5\x80\xa4\xe3\x81\x8c\xe4\xb8\x8d\xe6\xad\xa3\xe3\x81\xa7\xe3\x81\x99\x0a";
        }

        bool TrySkipModelCook(const CookOptions& options, const std::filesystem::path& inputPath,
            const std::filesystem::path& packagePath, const std::filesystem::path& manifestPath,
            NorvesLib::Core::Container::Span<const uint8_t> source,
            NorvesLib::Core::Container::AnsiStringView logicalPath,
            NorvesLib::Core::Container::AnsiStringView entryName, bool& bSkipped, std::string& error)
        {
            bSkipped=false;
            if (!options.bSkipIfUnchanged)
            {
                return true;
            }
            NorvesLib::Tools::AssetCook::ModelCookFingerprint fingerprint;
            NorvesLib::Core::Container::AnsiString fingerprintError;
            if (!NorvesLib::Tools::AssetCook::FingerprintModelCookSourceNativePath(source.data(),source.size(),
                options.Format.c_str(),inputPath,logicalPath,fingerprint,fingerprintError,&options.ImportSettings,
                NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(options.Format) ? &options.SkeletalImport.Decode : nullptr))
            {
                error.assign(fingerprintError.data(),fingerprintError.size());
                return false;
            }
            if (fingerprint.bHasImportSettings)
            {
                const auto& sidecar = fingerprint.ImportSettingsPath;
                if (!GuardImportSettingsOutput(packagePath,sidecar,error) || !GuardImportSettingsOutput(manifestPath,sidecar,error))
                {
                    return false;
                }
            }
            bSkipped=NorvesLib::Tools::AssetCook::IsModelCookCacheCurrent(manifestPath,packagePath,
                logicalPath,options.Variant.c_str(),options.Format.c_str(),entryName,fingerprint);
            if (bSkipped)
            {
                WarnDuplicateMeshMaterials(logicalPath, fingerprint.DuplicateMaterialNameGroups,
                                           fingerprint.FirstDuplicateMaterialIndex,
                                           fingerprint.SecondDuplicateMaterialIndex);
                std::cout << "sidecar: " << (fingerprint.bHasImportSettings ? DescribeCookPath(fingerprint.ImportSettingsPath) : "none")
                    << " settings_hash=" << ToStdString(FormatAssetHashHex(fingerprint.ImportSettingsHash)) << "\n";
                if (NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(options.Format))
                {
                    NorvesLib::Tools::AssetCook::SkeletalImportReportInput report;
                    report.Outcome = NorvesLib::Tools::AssetCook::SkeletalImportOutcome::CacheHit;
                    report.SourceHash = fingerprint.SourceHash;
                    report.Options = options.SkeletalImport.Decode;
                    PrintSkeletalImportReport(report);
                }
                std::cout << "AssetCook skipped unchanged model source_hash=" << ToStdString(FormatAssetHashHex(fingerprint.SourceHash)) << "\n";
            }
            return true;
        }

        bool CookModelAsset(const CookOptions& options, std::string& error, const CookPreparedPlan* guardedV1 = nullptr)
        {
            std::filesystem::path inputPath;
            std::filesystem::path packagePath;
            std::filesystem::path manifestPath;
            if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
                !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
                !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
            {
                return false;
            }

            std::vector<uint8_t> inputBytes;
            if (!ReadBinaryFile(inputPath, inputBytes, error))
            {
                return false;
            }

            std::string logicalPath;
            std::string entryName;
            if (!NormalizeManifestPathField("logical_path", options.LogicalPath, logicalPath, error) ||
                !NormalizeManifestPathField("entry_name", options.EntryName, entryName, error) ||
                !ValidateAsciiJsonField("variant", options.Variant, error) ||
                !ValidateAsciiJsonField("format", options.Format, error))
            {
                return false;
            }

            AssetPackageFourCC entryType = 0;
            std::string entryTypeText;
            if (!ParseEntryType(options.EntryTypeText, entryType, entryTypeText, error))
            {
                return false;
            }
            if (entryTypeText != "Msh0")
            {
                error = "--kind model requires --entry-type Msh0";
                return false;
            }

            bool bSkipped=false;
            if (!TrySkipModelCook(options,inputPath,packagePath,manifestPath,{inputBytes.data(),inputBytes.size()},
                logicalPath,entryName,bSkipped,error))
            {
                return false;
            }
            if (bSkipped)
            {
                return true;
            }

            NorvesLib::Tools::AssetCook::MeshCookResult meshResult;
            NorvesLib::Core::Container::AnsiString meshError;
            if (!NorvesLib::Tools::AssetCook::CookGltfToNvmeshNativePath(inputBytes.data(),
                                                               inputBytes.size(),
                                                               options.Format,
                                                               inputPath,
                                                               logicalPath,
                                                               meshResult,
                                                               meshError,
                                                               &options.ImportSettings))
            {
                error = ToStdString(meshError);
                return false;
            }

            if (guardedV1)
            {
                Core::Container::AnsiString reason;
                if (!ValidateCookOutputSet({guardedV1, 1}, {}, reason) || guardedV1->Outputs.empty() ||
                    meshResult.SourceHash != guardedV1->Outputs[0].ExpectedIdentity.SourceHash ||
                    meshResult.EmbeddedImages.size() + 1 != guardedV1->Outputs.size())
                {
                    error = "NVMESH v1 source/inventory changed before output: ";
                    error.append(reason.data(), reason.size());
                    return false;
                }
                for (size_t i = 0; i < meshResult.EmbeddedImages.size(); ++i)
                {
                    const auto& actual = meshResult.EmbeddedImages[i];
                    const auto& expected = guardedV1->Outputs[i + 1].ExpectedIdentity;
                    if (actual.LogicalPath != expected.LogicalPath || actual.Format != expected.Format ||
                        actual.SourceHash != expected.SourceHash)
                    {
                        error = "NVMESH v1 derived inventory changed before output";
                        return false;
                    }
                }
            }
            WarnDuplicateMeshMaterials(logicalPath, meshResult.DuplicateMaterialNameGroups,
                                       meshResult.FirstDuplicateMaterialIndex, meshResult.SecondDuplicateMaterialIndex);
            if (meshResult.bHasImportSettings)
            {
                const auto& sidecar = meshResult.ImportSettingsPath;
                if (!GuardImportSettingsOutput(packagePath, sidecar, error) ||
                    !GuardImportSettingsOutput(manifestPath, sidecar, error))
                {
                    return false;
                }
            }

            std::cout << "sidecar: " << (meshResult.bHasImportSettings ? DescribeCookPath(meshResult.ImportSettingsPath) : "none")
                      << " settings_hash=" << ToStdString(FormatAssetHashHex(meshResult.ImportSettingsHash)) << "\n";

            if (!meshResult.EmbeddedImages.empty())
            {
                return CookEmbeddedModelAssets(options, inputPath, packagePath, manifestPath, logicalPath, entryName, meshResult, error);
            }

            // Single conversion at the package boundary: MeshCooker exposes NorvesLib containers,
            // the package/manifest writers below still take std::vector payloads.
            const std::vector<uint8_t> nvmeshBytes(meshResult.NvmeshBytes.begin(), meshResult.NvmeshBytes.end());
            if (!ValidateCookedMeshPayload(nvmeshBytes, error))
            {
                return false;
            }

            const std::filesystem::path manifestParent = manifestPath.parent_path();
            std::string cookedPackagePath;
            if (!MakeCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
            {
                return false;
            }

            uint64_t cookedHash = 0;
            std::vector<uint8_t> packageBytes;
            if (!BuildSingleEntryPackage(entryName,
                                         entryType,
                                         nvmeshBytes,
                                         packageBytes,
                                         cookedHash,
                                         error))
            {
                return false;
            }

            // The mesh source hash covers the glTF JSON and every external buffer it loaded,
            // not just the JSON bytes handed to this process.
            const uint64_t sourceHash = meshResult.SourceHash;
            std::string manifestJson;
            if (!BuildManifestJson(logicalPath,
                                   options.Kind,
                                   sourceHash,
                                   options.Variant,
                                   options.Format,
                                   cookedPackagePath,
                                   entryName,
                                   entryTypeText,
                                   cookedHash,
                                   manifestJson,
                                   error))
            {
                return false;
            }

            if (!WriteBinaryFile(packagePath, packageBytes, error) ||
                !WriteTextFile(manifestPath, manifestJson, error))
            {
                return false;
            }

            if (!ValidateCookedMeshPackageOutput(packagePath,
                                                 entryName,
                                                 entryType,
                                                 nvmeshBytes,
                                                 error) ||
                !ValidateManifestOutput(manifestPath, manifestJson, error) ||
                !ValidateAssetSystemOutput(manifestPath,
                                           manifestJson,
                                           logicalPath,
                                           AssetKind::Model,
                                           options.Variant,
                                           nvmeshBytes,
                                           error))
            {
                return false;
            }

            std::cerr << "AssetCook wrote model package=\"" << packagePath.generic_string()
                      << "\" manifest=\"" << manifestPath.generic_string()
                      << "\" source_bytes=" << inputBytes.size()
                      << " nvmesh_bytes=" << meshResult.NvmeshBytes.size()
                      << " vertices=" << meshResult.VertexCount
                      << " indices=" << meshResult.IndexCount
                      << " clusters=" << meshResult.ClusterCount
                      << "\n";
            return true;
        }

        bool CookSkeletalModelAsset(const CookOptions& options, std::string& error)
        {
            std::filesystem::path inputPath;
            std::filesystem::path packagePath;
            std::filesystem::path manifestPath;
            if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
                !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
                !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
            {
                return false;
            }

            NorvesLib::Core::Container::VariableArray<uint8_t> inputBytes;
            if (!ReadSkeletalBinaryFile(inputPath, inputBytes, error))
            {
                return false;
            }

            const NorvesLib::Core::Container::AnsiString sourceLogicalPath(options.LogicalPath.c_str());
            const NorvesLib::Core::Container::AnsiString sourceEntryName(options.EntryName.c_str());
            const NorvesLib::Core::Container::AnsiString variant(options.Variant.c_str());
            const NorvesLib::Core::Container::AnsiString format(options.Format.c_str());
            NorvesLib::Core::Container::AnsiString logicalPath;
            NorvesLib::Core::Container::AnsiString entryName;
            if (!NormalizeSkeletalManifestPath(sourceLogicalPath, logicalPath, error) ||
                !NormalizeSkeletalManifestPath(sourceEntryName, entryName, error) ||
                !ValidateSkeletalAsciiField(variant, error) ||
                !ValidateSkeletalAsciiField(format, error))
            {
                return false;
            }

            if (options.EntryTypeText != "Skl0")
            {
                error = "--kind model with skeletal --format requires --entry-type Skl0";
                return false;
            }
            const AssetPackageFourCC entryType = NorvesLib::Core::Asset::CookedSkeletalFormatV0::EntryType;

            bool bSkipped=false;
            if (!TrySkipModelCook(options,inputPath,packagePath,manifestPath,{inputBytes.data(),inputBytes.size()},
                logicalPath,entryName,bSkipped,error))
            {
                return false;
            }
            if (bSkipped)
            {
                return true;
            }

            NorvesLib::Tools::AssetCook::SkeletalCookResult skeletalResult;
            NorvesLib::Core::Container::AnsiString skeletalError;
            NorvesLib::Tools::AssetCook::SkeletalCookDiagnostics diagnostics;
            const bool cooked = NorvesLib::Tools::AssetCook::CookGltfToNvskelNativePath(inputBytes.data(),
                                                               inputBytes.size(),
                                                               format,
                                                               inputPath,
                                                               skeletalResult,
                                                               skeletalError,
                                                               &options.ImportSettings,
                                                               &options.SkeletalImport.Decode, &diagnostics);
            NorvesLib::Tools::AssetCook::SkeletalImportReportInput report;
            report.Outcome = cooked ? NorvesLib::Tools::AssetCook::SkeletalImportOutcome::PayloadReady :
                NorvesLib::Tools::AssetCook::SkeletalImportOutcome::Failed;
            report.SourceHash = cooked ? skeletalResult.SourceHash : 0;
            report.Options = options.SkeletalImport.Decode;
            report.Diagnostics = diagnostics;
            PrintSkeletalImportReport(report);
            if (!cooked)
            {
                error.assign(skeletalError.data(), skeletalError.size());
                return false;
            }

            if (skeletalResult.bHasImportSettings)
            {
                const auto& sidecar = skeletalResult.ImportSettingsPath;
                if (!GuardImportSettingsOutput(packagePath, sidecar, error) ||
                    !GuardImportSettingsOutput(manifestPath, sidecar, error))
                {
                    return false;
                }
            }
            std::cout << "sidecar: " << (skeletalResult.bHasImportSettings ? DescribeCookPath(skeletalResult.ImportSettingsPath) : "none")
                      << " settings_hash=" << ToStdString(FormatAssetHashHex(skeletalResult.ImportSettingsHash)) << "\n";

            if (!ValidateCookedSkeletalPayload(skeletalResult.NvskelBytes, error))
            {
                return false;
            }

            const std::filesystem::path manifestParent = manifestPath.parent_path();
            NorvesLib::Core::Container::AnsiString cookedPackagePath;
            if (!MakeSkeletalCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
            {
                return false;
            }

            uint64_t cookedHash = 0;
            NorvesLib::Core::Container::VariableArray<uint8_t> packageBytes;
            if (!BuildSingleSkeletalEntryPackage(entryName,
                                                 entryType,
                                                 skeletalResult.NvskelBytes,
                                                 packageBytes,
                                                 cookedHash,
                                                 error))
            {
                return false;
            }

            const SkeletalManifestMetadata metadata{
                .VertexCount = skeletalResult.VertexCount,
                .IndexCount = skeletalResult.IndexCount,
                .JointCount = skeletalResult.JointCount,
                .ClipCount = skeletalResult.ClipCount,
                .SubmeshCount = skeletalResult.SubmeshCount,
                .MaterialSlotCount = skeletalResult.MaterialSlotCount,
            };
            NorvesLib::Core::Container::AnsiString manifestJson;
            if (!BuildSkeletalManifestJson(logicalPath,
                                   skeletalResult.SourceHash,
                                   variant,
                                   format,
                                   cookedPackagePath,
                                   entryName,
                                   metadata,
                                   cookedHash,
                                   manifestJson,
                                   error))
            {
                return false;
            }

            if (!WriteSkeletalBinaryFile(packagePath, packageBytes, error) ||
                !WriteSkeletalTextFile(manifestPath, manifestJson, error))
            {
                return false;
            }

            if (!ValidateCookedSkeletalPackageOutput(packagePath,
                                                     entryName,
                                                     entryType,
                                                     skeletalResult.NvskelBytes,
                                                     error) ||
                !ValidateSkeletalManifestOutput(manifestPath, manifestJson, error) ||
                !ValidateSkeletalAssetSystemOutput(manifestPath,
                                                   manifestJson,
                                                   logicalPath,
                                                   variant,
                                                   skeletalResult.NvskelBytes,
                                                   error))
            {
                return false;
            }

            if (options.SkeletalImport.Decode.InfluencePolicy == NorvesLib::Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour)
            {
                const auto& report = skeletalResult.DecodeReport;
                std::cerr << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << "skin_influences=reduce processed_vertices=" << report.ProcessedVertexCount
                    << " reduced_vertices=" << report.ReducedVertexCount
                    << " merged_joint_vertices=" << report.MergedJointVertexCount
                    << " renormalized_vertices=" << report.RenormalizedVertexCount
                    << " warning_vertices=" << report.WarningVertexCount
                    << " max_dropped_weight=" << report.MaximumDroppedWeight
                    << " mean_dropped_weight=" << report.MeanDroppedWeight << "\n";
                if (report.WarningVertexCount != 0)
                    std::cerr << "\xe8\xad\xa6\xe5\x91\x8a\x3a\x20\xe8\x84\xb1\xe8\x90\xbd\x77\x65\x69\x67\x68\x74\xe3\x81\x8c\xe6\x8c\x87\xe5\xae\x9a\xe3\x81\xae\xe8\xad\xa6\xe5\x91\x8a\xe9\x96\xbe\xe5\x80\xa4\xe3\x82\x92\xe8\xb6\x85\xe3\x81\x88\xe3\x81\x9f\xe9\xa0\x82\xe7\x82\xb9\xe3\x81\x8c\xe3\x81\x82\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x99\x0a";
            }
            if (options.SkeletalImport.Decode.CubicSplinePolicy == NorvesLib::Core::Skeletal::SkeletalCubicSplinePolicy::Bake)
            {
                const auto& report = skeletalResult.DecodeReport;
                std::cerr << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << "cubicspline=bake baked_channels=" << report.BakedCubicChannelCount
                    << " input_keys=" << report.CubicInputKeyCount << " output_keys=" << report.CubicOutputKeyCount;
                if (report.BakedCubicTranslationChannelCount != 0)
                {
                    std::cerr << " max_translation_error_m=" << report.MaximumCubicTranslationErrorMeters;
                }
                if (report.BakedCubicRotationChannelCount != 0)
                {
                    std::cerr << " max_rotation_error_rad=" << report.MaximumCubicRotationErrorRadians;
                }
                if (report.BakedCubicScaleChannelCount != 0)
                {
                    std::cerr << " max_scale_error=" << report.MaximumCubicScaleError;
                }
                std::cerr << "\n";
                if (report.BakedCubicChannelCount != 0)
                {
                    std::cerr << "\xe8\xad\xa6\xe5\x91\x8a\x3a\x20\x43\x55\x42\x49\x43\x53\x50\x4c\x49\x4e\x45\xe3\x82\x92\xe8\xa8\xb1\xe5\xae\xb9\xe8\xaa\xa4\xe5\xb7\xae\xe5\x86\x85\xe3\x81\xae\x4c\x49\x4e\x45\x41\x52\xe5\x88\x97\xe3\x81\xb8\xe5\xa4\x89\xe6\x8f\x9b\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\x0a";
                }
            }
            if (options.SkeletalImport.Decode.MorphPolicy == NorvesLib::Core::Skeletal::SkeletalMorphPolicy::Drop)
            {
                const auto& report = skeletalResult.DecodeReport;
                std::cerr << "morph=drop dropped_targets=" << report.DroppedMorphTargetCount
                    << " mesh_target_width=" << report.MorphTargetWidth
                    << " mesh_weight_values=" << report.DroppedMorphMeshWeightCount
                    << " node_weight_values=" << report.DroppedMorphNodeWeightCount
                    << " animation_channels=" << report.DroppedMorphAnimationChannelCount << "\n";
                if (report.DroppedMorphTargetCount != 0 || report.DroppedMorphMeshWeightCount != 0 ||
                    report.DroppedMorphNodeWeightCount != 0 || report.DroppedMorphAnimationChannelCount != 0)
                {
                    std::cerr << "\xe8\xad\xa6\xe5\x91\x8a\x3a\x20\x6d\x6f\x72\x70\x68\xe3\x82\x92\xe9\x99\xa4\xe5\x8e\xbb\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82\xe5\x85\x83\xe3\x83\xa1\xe3\x83\x83\xe3\x82\xb7\xe3\x83\xa5\xe3\x81\xb8\xe3\x81\xae\xe7\x84\xbc\xe4\xbb\x98\xe3\x81\x91\xe3\x81\xaf\xe8\xa1\x8c\xe3\x81\xa3\xe3\x81\xa6\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x9b\xe3\x82\x93\x0a";
                }
            }
            std::cerr << "AssetCook wrote skeletal model package=\"" << packagePath.generic_string()
                      << "\" manifest=\"" << manifestPath.generic_string()
                      << "\" source_bytes=" << inputBytes.size()
                      << " nvskel_bytes=" << skeletalResult.NvskelBytes.size()
                      << " vertices=" << skeletalResult.VertexCount
                      << " indices=" << skeletalResult.IndexCount
                      << " joints=" << skeletalResult.JointCount
                      << " clips=" << skeletalResult.ClipCount
                      << " submeshes=" << skeletalResult.SubmeshCount
                      << " material_slots=" << skeletalResult.MaterialSlotCount
                      << "\n";
            return true;
        }

        bool CookAudioAsset(const CookOptions& options, std::string& error)
        {
            std::filesystem::path inputPath;
            std::filesystem::path packagePath;
            std::filesystem::path manifestPath;
            if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
                !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
                !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
            {
                return false;
            }

            NorvesLib::Core::Container::VariableArray<uint8_t> inputBytes;
            if (!ReadSkeletalBinaryFile(inputPath, inputBytes, error))
            {
                return false;
            }

            const NorvesLib::Core::Container::AnsiString sourceLogicalPath(options.LogicalPath.c_str());
            const NorvesLib::Core::Container::AnsiString sourceEntryName(options.EntryName.c_str());
            const NorvesLib::Core::Container::AnsiString variant(options.Variant.c_str());
            const NorvesLib::Core::Container::AnsiString format(options.Format.c_str());
            NorvesLib::Core::Container::AnsiString logicalPath;
            NorvesLib::Core::Container::AnsiString entryName;
            if (!NormalizeSkeletalManifestPath(sourceLogicalPath, logicalPath, error) ||
                !NormalizeSkeletalManifestPath(sourceEntryName, entryName, error) ||
                !ValidateSkeletalAsciiField(variant, error) ||
                !ValidateSkeletalAsciiField(format, error))
            {
                return false;
            }

            NorvesLib::Tools::AssetCook::AudioCookResult audioResult;
            NorvesLib::Core::Container::AnsiString audioError;
            if (!NorvesLib::Tools::AssetCook::CookWaveToNvaud(
                    inputBytes.data(), inputBytes.size(), format, audioResult, audioError))
            {
                error.assign(audioError.data(), audioError.size());
                return false;
            }

            const std::filesystem::path manifestParent = manifestPath.parent_path();
            NorvesLib::Core::Container::AnsiString cookedPackagePath;
            if (!MakeSkeletalCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
            {
                return false;
            }

            uint64_t cookedHash = 0;
            NorvesLib::Core::Container::VariableArray<uint8_t> packageBytes;
            if (!BuildSingleSkeletalEntryPackage(
                    entryName,
                    NorvesLib::Core::Asset::CookedAudioFormatV0::EntryType,
                    audioResult.NvaudBytes,
                    packageBytes,
                    cookedHash,
                    error))
            {
                return false;
            }

            NorvesLib::Core::Asset::AssetCookedReference reference;
            reference.LogicalPath = logicalPath;
            reference.Kind = AssetKind::Audio;
            reference.SourceHash = audioResult.SourceHash;
            reference.SourceHashHex = FormatAssetHashHex(reference.SourceHash);
            reference.Variant = variant;
            reference.Format = format;
            reference.CookedPackage = cookedPackagePath;
            reference.EntryName = entryName;
            reference.EntryType = NorvesLib::Core::Asset::CookedAudioFormatV0::EntryType;
            reference.EntryTypeText = "Aud0";
            reference.CookedHash = cookedHash;
            reference.CookedHashHex = FormatAssetHashHex(cookedHash);
            reference.CookedVersion = 0;

            NorvesLib::Core::Container::AnsiString manifestJson;
            if (!BuildMergedManifestJson(manifestPath, {&reference, 1}, manifestJson, error))
            {
                return false;
            }
            if (!WriteSkeletalBinaryFile(packagePath, packageBytes, error) ||
                !WriteSkeletalTextFile(manifestPath, manifestJson, error))
            {
                return false;
            }
            if (!ValidateSkeletalManifestOutput(manifestPath, manifestJson, error) ||
                !ValidateAudioAssetSystemOutput(
                    manifestPath, manifestJson, logicalPath, variant, audioResult.NvaudBytes, error))
            {
                return false;
            }

            std::cerr << "AssetCook wrote audio package=\"" << packagePath.generic_string()
                      << "\" manifest=\"" << manifestPath.generic_string()
                      << "\" source_bytes=" << inputBytes.size()
                      << " nvaud_bytes=" << audioResult.NvaudBytes.size()
                      << " sample_rate=" << audioResult.SampleRate
                      << " channels=" << audioResult.ChannelCount
                      << " frames=" << audioResult.FrameCount
                      << "\n";
            return true;
        }
    }
    namespace Detail
    {
        bool MakeCachePackagePath(const std::filesystem::path& package, const std::filesystem::path& parent,
                                  Core::Container::AnsiString& relative, Core::Container::AnsiString& outError)
        {
            std::string value, error;
            if (!MakeCookedPackageManifestPath(package, parent, value, error))
            {
                outError = Core::Container::AnsiString(Core::Container::AnsiStringView(error.data(), error.size()));
                return false;
            }
            relative = Core::Container::AnsiString(Core::Container::AnsiStringView(value.data(), value.size()));
            return true;
        }
        CookOptions MakeLegacyCookOptions(const SingleAssetCookRequest& request)
        {
            CookOptions options;
            options.InputPath = request.InputPath;
            options.PackagePath = request.PackagePath;
            options.ManifestPath = request.ManifestPath;
            options.LogicalPath = ToStdString(request.LogicalPath);
            options.Kind = ToStdString(request.Kind);
            options.EntryName = ToStdString(request.EntryName);
            options.EntryTypeText = ToStdString(request.EntryTypeText);
            options.Format = ToStdString(request.Format);
            options.Variant = ToStdString(request.Variant);
            options.ImportSettings.OverridePath = request.ImportSettingsOverridePath;
            options.ImportSettings.bDisabled = request.bNoSidecar;
            options.ImportSettings.AssetSetEmission = request.AssetSetEmission;
            options.ImportSettings.bRequired = request.bRequireSidecar;
            options.bSkipIfUnchanged = request.bSkipIfUnchanged;
            options.SkeletalImport.Decode = request.SkeletalDecode;
            options.RoleProfile = request.RoleProfile;
            return options;
        }
        bool NormalizeCacheCookRequest(const SingleAssetCookRequest& request, SingleAssetCookRequest& out,
                                       Core::Container::AnsiString& outError)
        {
            if (IsRigSingleFormat(request.Format))
            {
                return NormalizeRigSingleRequest(request, out, outError);
            }
            if (HasSkeletalRoleFileRequest(request.RoleProfile) && !ValidateSkeletalRoleFileRequest(request, outError))
            {
                return false;
            }
            auto options = MakeLegacyCookOptions(request);
            options.bSkipIfUnchanged = false;
            std::string error, logical, entry, type;
            AssetPackageFourCC parsedType = 0;
            const auto finish = [&](bool bSuccess)
            {
                outError = Core::Container::AnsiString(Core::Container::AnsiStringView(error.data(), error.size()));
                return bSuccess;
            };
            if (!ValidateCookOptions(options, error) ||
                !MakeAbsolutePath(options.InputPath, options.InputPath, error) ||
                !MakeAbsolutePath(options.PackagePath, options.PackagePath, error) ||
                !MakeAbsolutePath(options.ManifestPath, options.ManifestPath, error) ||
                (!options.ImportSettings.OverridePath.empty() &&
                 !MakeAbsolutePath(options.ImportSettings.OverridePath, options.ImportSettings.OverridePath, error)) ||
                !NormalizeManifestPathField("logical_path", options.LogicalPath, logical, error) ||
                !NormalizeManifestPathField("entry_name", options.EntryName, entry, error) ||
                !ValidateAsciiJsonField("variant", options.Variant, error) ||
                !ValidateAsciiJsonField("format", options.Format, error) ||
                !ParseEntryType(options.EntryTypeText, parsedType, type, error))
            {
                return finish(false);
            }
            options.LogicalPath = std::move(logical);
            options.EntryName = std::move(entry);
            options.EntryTypeText = std::move(type);
            // 非骨格で使われない設定は正規化時に除く。単体CLIの挙動は変更しない。
            if (!IsSupportedSkeletalCookFormat(options.Format))
            {
                options.SkeletalImport.Decode = {};
            }
            if (options.RoleProfile.bEnabled)
            {
                if (!MakeAbsolutePath(options.RoleProfile.BvhPath, options.RoleProfile.BvhPath, error) ||
                    !MakeAbsolutePath(options.RoleProfile.ProfilePath, options.RoleProfile.ProfilePath, error))
                {
                    return finish(false);
                }
            }
            out = MakeSingleCookRequest(options);
            out.AssetSetEmission = request.AssetSetEmission;
            out.ClipJointNodes = request.ClipJointNodes;
            return finish(true);
        }
        bool ValidateCookOptions(const CookOptions& outOptions, std::string& error)
        {
            if (outOptions.InputPath.empty() ||
                outOptions.PackagePath.empty() ||
                outOptions.ManifestPath.empty() ||
                outOptions.LogicalPath.empty() ||
                outOptions.Kind.empty() ||
                outOptions.EntryName.empty() ||
                outOptions.EntryTypeText.empty() ||
                outOptions.Format.empty() ||
                outOptions.Variant.empty())
            {
                error = "missing required arguments";
                return false;
            }

            const char* skeletalError = nullptr;
            if (!NorvesLib::Tools::AssetCook::ValidateSkeletalArguments(outOptions.SkeletalImport,
                outOptions.Kind == "model" && NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(outOptions.Format), skeletalError))
            {
                error = skeletalError;
                return false;
            }

            if (outOptions.Kind != "model" && (outOptions.bSkipIfUnchanged || NorvesLib::Tools::AssetCook::HasImportArguments(outOptions.ImportSettings)))
            {
                error = "import settings options require --kind model";
                return false;
            }

            if (outOptions.Kind == "raw")
            {
                if (outOptions.Format != "raw.v0")
                {
                    error = "--kind raw requires --format raw.v0";
                    return false;
                }
            }
            else if (outOptions.Kind == "texture")
            {
                if (!NorvesLib::Tools::AssetCook::IsSupportedTextureCookFormat(outOptions.Format))
                {
                    error = "unsupported texture --format";
                    return false;
                }

                if (outOptions.EntryTypeText != "Tex0")
                {
                    error = "--kind texture requires --entry-type Tex0";
                    return false;
                }
            }
            else if (outOptions.Kind == "model")
            {
                const bool bStaticMeshFormat = NorvesLib::Tools::AssetCook::IsSupportedMeshCookFormat(outOptions.Format);
                const bool bSkeletalFormat = NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(outOptions.Format);
                if (!bStaticMeshFormat && !bSkeletalFormat)
                {
                    error = "--kind model requires a supported nvmesh or nvskel --format";
                    return false;
                }

                if (bStaticMeshFormat && outOptions.EntryTypeText != "Msh0")
                {
                    error = "--kind model requires --entry-type Msh0";
                    return false;
                }

                if (bSkeletalFormat && outOptions.EntryTypeText != "Skl0")
                {
                    error = "--kind model with skeletal --format requires --entry-type Skl0";
                    return false;
                }
            }
            else if (outOptions.Kind == "audio")
            {
                if (!NorvesLib::Tools::AssetCook::IsSupportedAudioCookFormat(outOptions.Format))
                {
                    error = "unsupported audio --format";
                    return false;
                }
                if (outOptions.EntryTypeText != "Aud0")
                {
                    error = "--kind audio requires --entry-type Aud0";
                    return false;
                }
            }
            else
            {
                error = "--kind must be raw, texture, model, or audio";
                return false;
            }

            return true;
        }
        SingleAssetCookRequest MakeSingleCookRequest(const CookOptions& options)
        {
            SingleAssetCookRequest request;
            request.InputPath = options.InputPath;
            request.PackagePath = options.PackagePath;
            request.ManifestPath = options.ManifestPath;
            const auto copy = [](const std::string& value)
            {
                return Core::Container::AnsiString(Core::Container::AnsiStringView(value.data(), value.size()));
            };
            request.LogicalPath = copy(options.LogicalPath);
            request.Kind = copy(options.Kind);
            request.EntryName = copy(options.EntryName);
            request.EntryTypeText = copy(options.EntryTypeText);
            request.Format = copy(options.Format);
            request.Variant = copy(options.Variant);
            request.ImportSettingsOverridePath = options.ImportSettings.OverridePath;
            request.bNoSidecar = options.ImportSettings.bDisabled;
            request.AssetSetEmission = options.ImportSettings.AssetSetEmission;
            request.bRequireSidecar = options.ImportSettings.bRequired;
            request.bSkipIfUnchanged = options.bSkipIfUnchanged;
            request.SkeletalDecode = options.SkeletalImport.Decode;
            request.RoleProfile = options.RoleProfile;
            return request;
        }

    }
    bool CookSingleAsset(const SingleAssetCookRequest& request, Core::Container::AnsiString& outError)
    {
        if (Detail::IsRigSingleFormat(request.Format))
        {
            return Detail::CookRigSingleAsset(request, outError);
        }
        if (HasSkeletalRoleFileRequest(request.RoleProfile))
        {
            SkeletalRoleFileCookResult result;
            return CookSkeletalRoleFile(request, result, outError);
        }
        auto options=Detail::MakeLegacyCookOptions(request);
        std::string error;
        if (!Detail::ValidateCookOptions(options, error))
        {
            outError = Core::Container::AnsiString(Core::Container::AnsiStringView(error.data(), error.size()));
            return false;
        }
        CookPreparedPlan guardedV1;
        const bool bV1 = options.Format == "nvmesh.v1.mesh3d.pnt.u32.clustered";
        if (bV1 && (!PrepareCookOutputPlan(request, 1, nullptr, guardedV1, outError) ||
                    !ValidateCookOutputSet({&guardedV1, 1}, {}, outError)))
        {
            return false;
        }
        bool bSucceeded = false;
        if (options.Kind == "raw")
        {
            bSucceeded = CookRawAsset(options, error);
        }
        else if (options.Kind == "texture")
        {
            bSucceeded = CookTextureAsset(options, error);
        }
        else if (options.Kind == "model")
        {
            if (NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(options.Format))
            {
                bSucceeded = CookSkeletalModelAsset(options, error);
            }
            else
            {
                bSucceeded = CookModelAsset(options, error, bV1 ? &guardedV1 : nullptr);
            }
        }
        else if (options.Kind == "audio")
        {
            bSucceeded = CookAudioAsset(options, error);
        }
        outError = Core::Container::AnsiString(Core::Container::AnsiStringView(error.data(), error.size()));
        return bSucceeded;
    }
}
