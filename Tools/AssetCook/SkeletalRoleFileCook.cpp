#include "SkeletalRoleFileCook.h"
#include "SkeletalRoleFileCookTestAccess.h"
#include "SkeletalRoleFileInput.h"
#include "SkeletalRoleFileReport.h"
#include "CookOutputSetGuard.h"
#include "CookReferenceValues.h"
#include "AssetCookOutput.h"
#include "Resource/ImportSettingsFile.h"
#include <fstream>
#include <type_traits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace C = Core::Container;
        namespace A = Core::Asset;
        namespace R = Detail::CookReferenceValues;
        bool Fail(C::AnsiString& error, const char* text)
        {
            error = C::AnsiString("role_file: ") + text;
            return false;
        }
        bool ReadManifest(const std::filesystem::path& path, A::AssetManifest& manifest, bool& bPresent,
                          C::AnsiString& error)
        {
            std::error_code code;
            bPresent = std::filesystem::exists(path, code);
            if (code)
            {
                return Fail(error, "manifest_status_failed");
            }
            if (!bPresent)
            {
                return true;
            }
            if (!std::filesystem::is_regular_file(path, code) || code)
            {
                return Fail(error, "manifest_not_regular");
            }
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (!file || size < 0 || static_cast<uintmax_t>(size) > 1024u * 1024u)
            {
                return Fail(error, "manifest_size_or_open");
            }
            C::VariableArray<uint8_t> bytes(static_cast<size_t>(size));
            file.seekg(0);
            if (!bytes.empty())
            {
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            if (!file || file.peek() != std::char_traits<char>::eof() || file.bad())
            {
                return Fail(error, "manifest_read");
            }
            C::String text;
            if (!DecodeSkeletalRoleUtf8Text({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, text) ||
                !manifest.LoadFromJsonText(text))
            {
                return Fail(error, "existing_manifest_invalid");
            }
            return true;
        }
        bool Existing(const CookPreparedPlan& plan, bool bOwnPackageWritten, C::AnsiString& error)
        {
            A::AssetManifest manifest;
            bool bPresent = false;
            if (!ReadManifest(plan.Context.Request.ManifestPath, manifest, bPresent, error))
            {
                return false;
            }
            if (!bPresent)
            {
                std::error_code code;
                const bool bPackageExists = std::filesystem::exists(plan.Context.Request.PackagePath, code);
                if (code || (bPackageExists && !bOwnPackageWritten))
                {
                    return Fail(error, "unclaimed_package_or_status");
                }
                return true;
            }
            if (manifest.GetReferenceCount() != 1)
            {
                return Fail(error, "existing_manifest_requires_one_same_key");
            }
            auto identity = manifest.GetReference(0);
            identity.SourceHash = plan.Outputs[0].ExpectedIdentity.SourceHash;
            if (!R::SameIdentity(identity, plan.Outputs[0].ExpectedIdentity))
            {
                return Fail(error, "existing_manifest_other_identity");
            }
            return true;
        }
        bool MatchesRead(const CookPreparedPlan& plan, CookDependencyRole role, C::Span<const uint8_t> bytes,
                         C::AnsiString& error)
        {
            size_t matches = 0;
            for (const auto& file : plan.Context.Dependencies.Files)
            {
                if (file.Role != role)
                {
                    continue;
                }
                ++matches;
                if (!file.bPresent || file.Size != bytes.size() ||
                    file.ContentHash != A::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size()))
                {
                    return Fail(error, "read_bytes_changed_before_adapter");
                }
            }
            return matches == 1 || Fail(error, "read_dependency_role_count");
        }
        bool Same(const CookPreparedPlan& a, const CookPreparedPlan& b)
        {
            if (a.Context.Dependencies.SchemaVersion != b.Context.Dependencies.SchemaVersion ||
                a.Context.Dependencies.CookerRevision != b.Context.Dependencies.CookerRevision ||
                a.Context.Dependencies.Fingerprint != b.Context.Dependencies.Fingerprint ||
                a.Outputs.size() != b.Outputs.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.Outputs.size(); ++i)
            {
                if (a.Outputs[i].TargetPath != b.Outputs[i].TargetPath ||
                    !R::SameIdentity(a.Outputs[i].ExpectedIdentity, b.Outputs[i].ExpectedIdentity))
                {
                    return false;
                }
            }
            return true;
        }
        bool Fresh(const CookPreparedPlan& before, bool bOwnPackageWritten, C::AnsiString& error)
        {
            CookPreparedPlan current;
            if (!PrepareCookOutputPlan(before.Context.Request, SkeletalRoleFileCookerRevision, nullptr, current, error))
            {
                return false;
            }
            if (!Same(before, current))
            {
                return Fail(error, "dependencies_or_inventory_changed_before_write");
            }
            return ValidateCookOutputSet({&current, 1}, {}, error) && Existing(current, bOwnPackageWritten, error);
        }
    } // namespace
    bool Detail::CookSkeletalRoleFileWithProbe(const SingleAssetCookRequest& request, SkeletalRoleFileCookResult& out,
                                               C::AnsiString& error, SkeletalRoleFileProbe probe, void* context)
    {
        try
        {
            if (!ValidateSkeletalRoleFileRequest(request, error))
            {
                return false;
            }
            CookPreparedPlan plan;
            if (!PrepareCookOutputPlan(request, SkeletalRoleFileCookerRevision, nullptr, plan, error) ||
                plan.Outputs.size() != 1 || !ValidateCookOutputSet({&plan, 1}, {}, error) ||
                !Existing(plan, false, error))
            {
                return false;
            }
            const auto& r = plan.Context.Request;
            C::VariableArray<uint8_t> source;
            std::string legacyError;
            if (!ReadSkeletalBinaryFile(r.InputPath, source, legacyError))
            {
                error = legacyError.c_str();
                return false;
            }
            SkeletalRoleFileInputs inputs;
            if (!LoadSkeletalRoleFileInputs(r.RoleProfile, inputs, error))
            {
                return false;
            }
            if (!MatchesRead(plan, CookDependencyRole::Source, {source.data(), source.size()}, error) ||
                !MatchesRead(plan, CookDependencyRole::Bvh, {inputs.BvhBytes.data(), inputs.BvhBytes.size()}, error) ||
                !MatchesRead(plan, CookDependencyRole::RoleProfile,
                             {inputs.ProfileBytes.data(), inputs.ProfileBytes.size()}, error))
            {
                return false;
            }
            const auto typed = MakeSkeletalRoleBytesRequest(r.RoleProfile, inputs);
            Core::AssetImport::ImportSettingsFileOptions settings;
            settings.OverridePath = r.ImportSettingsOverridePath;
            settings.bDisabled = r.bNoSidecar;
            settings.bRequired = r.bRequireSidecar;
            SkeletalRoleFileCookResult candidate;
            if (!CookGltfWithRoleProfileToNvskelNativePath(source.data(), source.size(), r.Format, r.InputPath, typed,
                                                           candidate.Value, error, &settings, &r.SkeletalDecode))
            {
                return false;
            }
            const auto& cooked = candidate.Value.Cooked.Cook;
            if (cooked.SourceHash != plan.Outputs[0].ExpectedIdentity.SourceHash)
            {
                return Fail(error, "cooked_source_hash_changed");
            }
            C::VariableArray<uint8_t> package;
            uint64_t packageHash = 0;
            if (!BuildSingleSkeletalEntryPackage(r.EntryName, A::MakeAssetPackageFourCC('S', 'k', 'l', '0'),
                                                 cooked.NvskelBytes, package, packageHash, legacyError))
            {
                error = legacyError.c_str();
                return false;
            }
            const SkeletalManifestMetadata metadata{cooked.VertexCount, cooked.IndexCount,   cooked.JointCount,
                                                    cooked.ClipCount,   cooked.SubmeshCount, cooked.MaterialSlotCount};
            C::AnsiString manifestJson;
            if (!BuildSkeletalManifestJson(r.LogicalPath, cooked.SourceHash, r.Variant, r.Format,
                                           plan.Outputs[0].ExpectedIdentity.CookedPackage, r.EntryName, metadata,
                                           packageHash, manifestJson, legacyError))
            {
                error = legacyError.c_str();
                return false;
            }
            C::String manifestText;
            A::AssetManifest manifest;
            if (!DecodeSkeletalRoleUtf8Text(manifestJson, manifestText) || !manifest.LoadFromJsonText(manifestText) ||
                manifest.GetReferenceCount() != 1)
            {
                return Fail(error, "generated_manifest_invalid");
            }
            CookOutputPackageFingerprint packageFingerprint;
            if (!ValidateCookOutputPackage(manifest.GetReference(0), {package.data(), package.size()},
                                           packageFingerprint, error))
            {
                return false;
            }
            candidate.DependencyFingerprint = plan.Context.Dependencies.Fingerprint;
            if (!BuildSkeletalRoleFileReport(r, candidate.Value, candidate.DependencyFingerprint, candidate.ReportJson,
                                             error))
            {
                return false;
            }
            if (probe)
            {
                probe(SkeletalRoleFilePoint::BeforeFirstWrite, context);
            }
            if (!Fresh(plan, false, error))
            {
                return false;
            }
            if (!WriteSkeletalBinaryFile(r.PackagePath, package, legacyError))
            {
                error = legacyError.c_str();
                return false;
            }
            if (probe)
            {
                probe(SkeletalRoleFilePoint::BeforeManifestWrite, context);
            }
            if (!Fresh(plan, true, error))
            {
                return false;
            }
            if (!WriteSkeletalTextFile(r.ManifestPath, manifestJson, legacyError) ||
                !ValidateCookedSkeletalPackageOutput(r.PackagePath, r.EntryName,
                                                     A::MakeAssetPackageFourCC('S', 'k', 'l', '0'), cooked.NvskelBytes,
                                                     legacyError) ||
                !ValidateSkeletalManifestOutput(r.ManifestPath, manifestJson, legacyError) ||
                !ValidateSkeletalAssetSystemOutput(r.ManifestPath, manifestJson, r.LogicalPath, r.Variant,
                                                   cooked.NvskelBytes, legacyError))
            {
                error = legacyError.c_str();
                return false;
            }
            if (probe)
            {
                probe(SkeletalRoleFilePoint::BeforeRecord, context);
            }
            if (!CaptureCookOutputRecord(plan.Context, manifest, candidate.Record, error))
            {
                return false;
            }
            static_assert(std::is_nothrow_move_assignable_v<SkeletalRoleFileCookResult>);
            out = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "execution_exception");
        }
    }
    bool CookSkeletalRoleFile(const SingleAssetCookRequest& request, SkeletalRoleFileCookResult& out,
                              C::AnsiString& error)
    {
        return Detail::CookSkeletalRoleFileWithProbe(request, out, error, nullptr, nullptr);
    }
} // namespace NorvesLib::Tools::AssetCook
