#include "RigRetargetCook.h"
#include "RigClipBankCook.h"
#include "SkeletalRoleFileInput.h"
#include "NativeCookPath.h"
#include "Resource/RigGltfImportCapture.h"
#include "Resource/ImportSettingsFile.h"
#include "Asset/AssetPackageFormat.h"
#include <fstream>
#include <cstdio>
namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace A = Core::Animation;
    namespace I = Core::AssetImport;
    namespace
    {
        bool Fail(C::AnsiString& error, const char* reason)
        {
            error = reason;
            return false;
        }
        bool Matches(const CookDependencySnapshot& snapshot, const std::filesystem::path& path, CookDependencyRole role,
                     C::Span<const uint8_t> bytes, bool bPresent = true)
        {
            const auto canonical = std::filesystem::weakly_canonical(path);
            for (const auto& file : snapshot.Files)
            {
                if (file.Path == canonical && file.Role == role && file.bPresent == bPresent &&
                    (!bPresent ||
                     (file.Size == bytes.size() &&
                      file.ContentHash == Core::Asset::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size()))))
                {
                    return true;
                }
            }
            return false;
        }
        bool Read(const std::filesystem::path& path, uint64_t maximum, C::VariableArray<uint8_t>& bytes)
        {
            if (!std::filesystem::is_regular_file(path))
            {
                return false;
            }
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            const auto size = stream.tellg();
            if (!stream || size <= 0 || uint64_t(size) > maximum)
            {
                return false;
            }
            bytes.resize(size_t(size));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            return bool(stream) && stream.peek() == std::char_traits<char>::eof();
        }
        bool Import(const SingleAssetCookRequest& request, const std::filesystem::path& path,
                    C::Span<const uint8_t> bytes, bool bTarget, const CookDependencySnapshot& dependencies,
                    S::RigAuthoringCpu& rig, C::AnsiString& error)
        {
            I::ImportSettingsFileOptions options;
            if (!bTarget)
            {
                options = {request.ImportSettingsOverridePath, request.bNoSidecar, request.bRequireSidecar};
            }
            I::LoadedImportSettingsDocument document;
            if (I::LoadImportSettingsDocument(path, options, document).Result != I::SettingsFileResult::Success)
            {
                return Fail(error, "retarget_sidecar");
            }
            if (!document.Path.empty() && !Matches(dependencies, document.Path, CookDependencyRole::Sidecar,
                                                   document.RawSourceBytes, document.bPresent))
            {
                return Fail(error, "retarget_sidecar_snapshot");
            }
            I::LoadedImportSettings geometry;
            geometry.Settings = document.Settings.Geometry;
            geometry.Path = document.Path;
            geometry.bPresent = document.bPresent;
            S::RigV1Limits limits;
            limits.MaxJoints = 256;
            S::RigClipSourceSelection selection;
            if (!bTarget)
            {
                selection.JointNodes = request.ClipJointNodes;
            }
            selection.bAllowEmptyAnimations = bTarget;
            S::RigGltfImportCapture capture;
            S::RigV1Report report;
            const auto* chosen =
                geometry.Settings.Fit == I::FitAxis::None && (geometry.Settings.Origin == I::OriginMode::Keep ||
                                                              geometry.Settings.Origin == I::OriginMode::Custom)
                    ? &selection
                    : nullptr;
            if (!S::DecodeRigAuthoringWithProfileNativePath(bytes, path, S::RigImportProfile::StaticRootFrame256, rig,
                                                            report, limits, &geometry, &request.SkeletalDecode,
                                                            &capture, chosen))
            {
                return Fail(error, "retarget_author_import");
            }
            for (size_t i = 0; i < capture.Buffers.GetCount(); ++i)
            {
                if (capture.Buffers.GetSourceKind(i) == Core::Gltf::BufferStorageKind::ExternalFile &&
                    (i >= capture.SourceCanonicalFiles.size() ||
                     !Matches(dependencies, capture.SourceCanonicalFiles[i], CookDependencyRole::ExternalBuffer,
                              capture.Buffers.GetSourceBytes(i))))
                {
                    return Fail(error, "retarget_buffer_snapshot");
                }
            }
            return true;
        }
    } // namespace
    bool CookRigRetargetBank(const SingleAssetCookRequest& request, C::Span<const uint8_t> source,
                             const CookDependencySnapshot& dependencies, C::VariableArray<uint8_t>& payload,
                             C::AnsiString& error)
    {
        C::VariableArray<uint8_t> targetBytes, profileBytes;
        if (!Matches(dependencies, request.InputPath, CookDependencyRole::Source, source) ||
            !Read(request.RetargetSkeletonPath, S::RigV1Limits{}.MaxSourceBytes, targetBytes) ||
            !Matches(dependencies, request.RetargetSkeletonPath, CookDependencyRole::TargetSkeleton, targetBytes) ||
            !Read(request.RetargetProfilePath, 1024 * 1024, profileBytes) ||
            !Matches(dependencies, request.RetargetProfilePath, CookDependencyRole::RoleProfile, profileBytes))
        {
            return Fail(error, "retarget_input_snapshot");
        }
        A::SkeletalRoleProfile profile;
        const auto parsed = A::ParseSkeletalRoleProfile(profileBytes, {}, profile);
        if (!parsed.Succeeded())
        {
            error = "retarget_profile: " + parsed.Field;
            return false;
        }
        S::RigAuthoringCpu target;
        if (!Import(request, request.RetargetSkeletonPath, targetBytes, true, dependencies, target, error))
        {
            return false;
        }
        A::SkeletalClipRetargetSettings settings;
        settings.RestMode = profile.RestMode;
        settings.UpHint = profile.RestUpHint;
        settings.MaximumRestErrorRadians = profile.MaximumRestErrorRadians;
        settings.RootScale = profile.RootScale;
        settings.bAutoRootHeight = profile.bAutoRootHeight;
        settings.Processing = profile.Processing;
        settings.LoopExcludedRoles = profile.LoopExcludedRoles;
        C::VariableArray<S::SkeletalAnimationClip> clips;
        double sourceFps = 0;
        const auto convert = [&](const A::SkeletalRetargetClipSource& input)
        {
            S::SkeletalAnimationClip clip;
            A::SkeletalClipRetargetReport report;
            if (!A::RetargetSkeletalClip(input, target, profile, settings, clip, report, error))
            {
                return false;
            }
            // 取り込みの測定値だけを出す。未計測のkey/連続曲線誤差を0という合格値にしない。
            std::fprintf(
                stderr,
                "retarget_clip samples=%u loop_detected=%u range=%u period_seconds=%.9g seam_before_radians=%.9g seam_after_radians=%.9g seam_velocity_radians_per_second=%.9g planar_meters=%.9g speed_m_s=%.9g root_scale=%.9g ignored_translation_channels=%u key_error=unmeasured\n",
                report.Processing.OutputSamples, unsigned(report.Processing.bLoopDetected),
                unsigned(report.Processing.bRangeSelected), report.Processing.PeriodSeconds,
                report.Processing.SeamBeforeRadians, report.Processing.SeamAfterRadians,
                report.Processing.SeamVelocityDifferenceRadiansPerSecond, report.Processing.PlanarDistanceMeters,
                report.Processing.AverageSpeedMetersPerSecond, report.RootScale, report.IgnoredTranslationChannels);
            for (const auto& correction : report.Corrections)
            {
                std::fprintf(
                    stderr,
                    "retarget_rest source=%u target=%u before_radians=%.9g after_radians=%.9g direction_missing=%u\n",
                    correction.SourceJoint, correction.TargetJoint, correction.BeforeRadians, correction.AfterRadians,
                    unsigned(correction.bDirectionMissing));
            }
            clips.push_back(std::move(clip));
            return true;
        };
        if (IsBvhRetargetSource(request))
        {
            Core::Bvh::BvhDocument document;
            if (!Core::Bvh::DecodeBvh(source, {}, document).Succeeded())
            {
                return Fail(error, "retarget_bvh_decode");
            }
            if (!request.RetargetSourceClip.empty())
            {
                return Fail(error, "bvh_has_no_source_clip_selection");
            }
            C::AnsiString name = request.RetargetClipName;
            if (name.empty() && !EncodeCookPathUtf8(request.InputPath.stem(), name))
            {
                return Fail(error, "retarget_clip_name");
            }
            C::String nativeName;
            if (!DecodeSkeletalRoleUtf8Text(name, nativeName))
            {
                return Fail(error, "retarget_clip_name");
            }
            A::SkeletalRetargetClipSource input;
            if (!A::MakeBvhRetargetClipSource(document, profile.Settings, nativeName, input, error) || !convert(input))
            {
                return false;
            }
            sourceFps = 1 / input.IntervalSeconds;
        }
        else
        {
            // glTFの時刻は既に秒。BVH Frame Time上書きを黙って適用/無視しない。
            if (profile.Settings.TimeMode != A::SkeletalBvhClipTimeMode::HeaderFrameTime)
            {
                return Fail(error, "gltf_retarget_uses_authored_seconds");
            }
            S::RigAuthoringCpu author;
            if (!Import(request, request.InputPath, source, false, dependencies, author, error))
            {
                return false;
            }
            C::String selected;
            if (!request.RetargetSourceClip.empty() &&
                !DecodeSkeletalRoleUtf8Text(request.RetargetSourceClip, selected))
            {
                return Fail(error, "retarget_source_clip_name");
            }
            for (size_t i = 0; i < author.GetData()->Geometry.Clips.size(); ++i)
            {
                if (!selected.empty() && author.GetData()->Geometry.Clips[i].Name != selected)
                {
                    continue;
                }
                A::SkeletalRetargetClipSource input;
                if (!A::MakeGltfRetargetClipSource(author, i, input, error) || !convert(input))
                {
                    return false;
                }
            }
            if (clips.empty())
            {
                return Fail(error, "retarget_source_clip_missing");
            }
        }
        if (!request.RetargetClipName.empty())
        {
            if (clips.size() != 1)
            {
                return Fail(error, "retarget_clip_name_requires_one_clip");
            }
            if (!DecodeSkeletalRoleUtf8Text(request.RetargetClipName, clips[0].Name))
            {
                return Fail(error, "retarget_clip_name");
            }
        }
        S::RigV1Limits limits;
        limits.MaxJoints = 256;
        S::RigV1Report report;
        S::ClipBankV1 bank;
        S::RigClipAnalysisOptions analysis;
        if (settings.Processing.Loop == A::SkeletalLoopSelection::None)
        {
            analysis.Loop = S::RigClipLoopMode::Once;
        }
        else if (settings.Processing.Loop == A::SkeletalLoopSelection::Range)
        {
            analysis.Loop = S::RigClipLoopMode::Loop;
        }
        if (sourceFps > 0)
        {
            analysis.SourceFps = sourceFps;
            analysis.AuthoredFps = sourceFps;
        }
        if (!S::BuildClipBankV1({&target, 1}, bank, report, limits, S::RigImportProfile::StaticRootFrame256, &analysis,
                                clips) ||
            !S::WriteClipBankV1(bank, payload, report, limits, S::RigImportProfile::StaticRootFrame256))
        {
            return Fail(error, "retarget_bank_write");
        }
        CookDependencySnapshot after;
        if (!CaptureCookDependencySnapshot(request, 1, after, error) || after.Fingerprint != dependencies.Fingerprint)
        {
            return Fail(error, "retarget_dependencies_changed");
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
