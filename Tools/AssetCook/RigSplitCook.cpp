#include "RigSplitCook.h"
#include "SurfaceSampleTableBuilder.h"
#include "Animation/RigRootFrame.h"
#include "Animation/SkeletalBindRowMath.h"
#include "MeshMaterialV1Plan.h"
#include "AssetCookOutput.h"
#include "GeometryInspection.h"
#include "Resource/SkeletalImportPolicy.h"
#include "Resource/MaterialImportSettings.h"
#include "Resource/MaterialImportPolicy.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include <bit>
#include <cstdio>
namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace I = Core::AssetImport;
    namespace A = Core::Asset;
    static_assert(I::MaximumSkinnedLodTargets == S::MaximumSkinLodCount);
    static_assert(I::MaximumSkinnedLodParts == S::MaximumSkinPartCount);
    namespace
    {
        using Text = C::AnsiString;
        using View = C::AnsiStringView;
        using Bytes = C::VariableArray<uint8_t>;
        struct Hash
        {
            uint64_t Value = 14695981039346656037ull;
            void Byte(uint8_t b)
            {
                Value = (Value ^ b) * 1099511628211ull;
            }
            void Number(uint64_t n)
            {
                for (unsigned i = 0; i < 8; ++i)
                {
                    Byte(uint8_t(n >> (i * 8)));
                }
            }
            void Data(C::Span<const uint8_t> bytes)
            {
                Number(bytes.size());
                for (auto b : bytes)
                {
                    Byte(b);
                }
            }
            void String(View text)
            {
                Data({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
            }
        };
        Text Number(uint64_t n)
        {
            char bytes[32]{};
            std::snprintf(bytes, sizeof(bytes), "%llu", static_cast<unsigned long long>(n));
            return Text(bytes);
        }
        void Quote(Text& out, View text)
        {
            out.push_back('"');
            for (char c : text)
            {
                if (c == '"' || c == '\\')
                {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            out.push_back('"');
        }
        void StringField(Text& out, const char* key, View value, bool bFirst = false)
        {
            if (!bFirst)
            {
                out.push_back(',');
            }
            Quote(out, key);
            out.push_back(':');
            Quote(out, value);
        }
        void Numeric(Text& out, const char* key, uint64_t value)
        {
            out.push_back(',');
            Quote(out, key);
            out.push_back(':');
            out += Number(value);
        }
        void ManifestEntry(Text& json, const A::AssetCookedReference& r)
        {
            json.push_back('{');
            StringField(json, "logical_path", r.LogicalPath, true);
            StringField(json, "kind", A::GetAssetKindName(r.Kind));
            StringField(json, "source_hash", r.SourceHashHex);
            StringField(json, "variant", r.Variant);
            StringField(json, "format", r.Format);
            StringField(json, "cooked_package", r.CookedPackage);
            StringField(json, "entry_name", r.EntryName);
            StringField(json, "entry_type", r.EntryTypeText);
            StringField(json, "cooked_hash", r.CookedHashHex);
            Numeric(json, "cooked_version", r.CookedVersion);
            if (!r.bHasRigSplitMetadata)
            {
                json.push_back('}');
                return;
            }
            json += " ,\"metadata\":{";
            const auto& m = r.RigSplitMetadata;
            StringField(json, "skeleton_id", A::FormatAssetHashHex(m.SkeletonId), true);
            Numeric(json, "profile", r.RigSplitMetadata.Profile);
            Numeric(json, "joint_count", m.JointCount);
            if (m.Role == 2)
            {
                Numeric(json, "vertex_count", m.VertexCount);
                Numeric(json, "index_count", m.IndexCount);
                Numeric(json, "submesh_count", m.SubmeshCount);
                Numeric(json, "material_slot_count", m.MaterialSlotCount);
                Numeric(json, "material_count", m.MaterialCount);
            }
            if (m.Role == 3)
            {
                Numeric(json, "clip_count", m.ClipCount);
                Numeric(json, "snapshot_count", m.SnapshotCount);
                Numeric(json, "channel_count", m.ChannelCount);
                Numeric(json, "sample_count", m.SampleCount);
            }
            json += "}}";
        }
        bool ImageRead(uint32_t index, MeshEmbeddedImage& encoded, DecodedTextureRgba8& decoded,
                       Core::Gltf::DataUriMime& mime, void* context, Text& error)
        {
            return static_cast<RigSplitImageInputs*>(context)->Read(index, encoded, decoded, mime, error);
        }
        bool Derived(uint64_t bytes, uint32_t width, uint32_t height, void* context, Text& error)
        {
            auto& input = *static_cast<RigSplitImageInputs*>(context);
            return input.ReserveDerived(bytes, width, height, error);
        }
        bool MergeImage(C::VariableArray<MeshEmbeddedImage>& images, MeshEmbeddedImage&& value, Text& error)
        {
            for (auto& old : images)
            {
                if (old.ImageIndex != value.ImageIndex)
                {
                    continue;
                }
                const auto a = old.GetBytes(), b = value.GetBytes();
                if (old.Format != value.Format || old.LogicalPath != value.LogicalPath ||
                    old.Payload != value.Payload || old.Width != value.Width || old.Height != value.Height ||
                    a.size() != b.size() || !std::equal(a.begin(), a.end(), b.begin()))
                {
                    error = "split_image_role_conflict";
                    return false;
                }
                old.Roles |= value.Roles;
                return true;
            }
            images.push_back(std::move(value));
            return true;
        }
        bool Closure(const S::SkeletalGltfData& geometry, GeometryClosureInspection& out)
        {
            C::VariableArray<InspectionVertex> vertices(geometry.Vertices.size());
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                const auto& v = geometry.Vertices[i];
                vertices[i].Position[0] = v.Position.X;
                vertices[i].Position[1] = v.Position.Y;
                vertices[i].Position[2] = v.Position.Z;
                vertices[i].Normal[0] = v.Normal.X;
                vertices[i].Normal[1] = v.Normal.Y;
                vertices[i].Normal[2] = v.Normal.Z;
            }
            C::VariableArray<uint32_t> order(vertices.size()), representatives(vertices.size()),
                parents(vertices.size());
            C::VariableArray<GeometryClosureEdge> edges(geometry.Indices.size());
            return InspectGeometryClosure(vertices, geometry.Indices, order, representatives, parents, edges, {}, out);
        }
        bool Package(const RigSplitCookRequest& request, RigSplitCookEntry& entry, uint32_t role, uint64_t sourceHash,
                     Text& error)
        {
            auto& ref = entry.Reference;
            ref.SourceHash = sourceHash;
            ref.SourceHashHex = A::FormatAssetHashHex(sourceHash);
            ref.Variant = request.Variant;
            ref.CookedVersion = 1;
            ref.EntryName = ref.LogicalPath;
            ref.EntryTypeText = A::FormatAssetPackageFourCCText(ref.EntryType);
            // 既存package codecの境界だけstd errorを受け、公開結果は独自文字列へ戻す。
            std::string legacyError;
            if (!Detail::BuildSingleSkeletalEntryPackage(ref.EntryName, ref.EntryType, entry.Payload, entry.Package,
                                                         ref.CookedHash, legacyError))
            {
                error = legacyError.c_str();
                return false;
            }
            ref.CookedHashHex = A::FormatAssetHashHex(ref.CookedHash);
            ref.CookedPackage = request.PackageDirectory + "/" + ref.SourceHashHex + "-" + ref.CookedHashHex + "-" +
                                Number(role) + ".nvpk";
            if (!S::IsSplitLogicalPath(ref.CookedPackage))
            {
                error = "split_package_path_limit";
                return false;
            }
            return true;
        }
    } // namespace
    bool ApplyClipImportSettings(const I::ClipImportSettings& sidecar, uint32_t explicitKeys,
                                 bool& bAnalyzeClips, S::RigClipAnalysisOptions& analysis,
                                 Text& rootJoint, Text& error)
    {
        uint32_t callerKeys = explicitKeys;
        if (!rootJoint.empty())
        {
            callerKeys |= I::ClipKeyRootJoint;
        }
        if (analysis.Loop != S::RigClipLoopMode::Auto)
        {
            callerKeys |= I::ClipKeyLoop;
        }
        if (analysis.LoopThreshold != 0.01)
        {
            callerKeys |= I::ClipKeyLoopThreshold;
        }
        if (analysis.AuthoredFps != 0)
        {
            callerKeys |= I::ClipKeyAuthoredFps;
        }
        if (analysis.SourceFps != 0)
        {
            callerKeys |= I::ClipKeySourceFps;
        }
        if (analysis.TimeScale != 1)
        {
            callerKeys |= I::ClipKeyTimeScale;
        }

        bool bMergedAnalysis = bAnalyzeClips;
        auto merged = analysis;
        Text mergedRoot(rootJoint.size(), '\0');
        if (!rootJoint.empty())
        {
            std::copy(rootJoint.begin(), rootJoint.end(), mergedRoot.begin());
        }
        const auto fail = [&](const char* reason)
        {
            error = reason;
            return false;
        };
        if (sidecar.Keys & I::ClipKeyRootJoint)
        {
            if ((callerKeys & I::ClipKeyRootJoint) &&
                (rootJoint.size() != sidecar.RootJoint.size() ||
                 (!rootJoint.empty() && !std::equal(rootJoint.begin(), rootJoint.end(), sidecar.RootJoint.begin()))))
            {
                return fail("clip_conflict_root_joint");
            }
            mergedRoot = Text(sidecar.RootJoint.size(), '\0');
            if (!sidecar.RootJoint.empty())
            {
                std::copy(sidecar.RootJoint.begin(), sidecar.RootJoint.end(), mergedRoot.begin());
            }
        }
        if (sidecar.Keys & I::ClipKeyLoop)
        {
            S::RigClipLoopMode loop;
            switch (sidecar.Loop)
            {
            case I::ClipLoopSetting::Auto:
                loop = S::RigClipLoopMode::Auto;
                break;
            case I::ClipLoopSetting::Loop:
                loop = S::RigClipLoopMode::Loop;
                break;
            case I::ClipLoopSetting::Once:
                loop = S::RigClipLoopMode::Once;
                break;
            default:
                return fail("clip_conflict_loop");
            }
            if ((callerKeys & I::ClipKeyLoop) && merged.Loop != loop)
            {
                return fail("clip_conflict_loop");
            }
            merged.Loop = loop;
        }
        const auto applyNumber = [&](uint32_t key, double value, double& target, const char* reason)
        {
            if (sidecar.Keys & key)
            {
                if ((callerKeys & key) && !(target == value))
                {
                    return fail(reason);
                }
                target = value;
            }
            return true;
        };
        if (!applyNumber(I::ClipKeyLoopThreshold, sidecar.LoopThreshold, merged.LoopThreshold,
                         "clip_conflict_loop_threshold") ||
            !applyNumber(I::ClipKeyAuthoredFps, sidecar.AuthoredFps, merged.AuthoredFps,
                         "clip_conflict_authored_fps") ||
            !applyNumber(I::ClipKeySourceFps, sidecar.SourceFps, merged.SourceFps,
                         "clip_conflict_source_fps") ||
            !applyNumber(I::ClipKeyTimeScale, sidecar.TimeScale, merged.TimeScale,
                         "clip_conflict_time_scale"))
        {
            return false;
        }
        if (sidecar.Keys & I::ClipKeyAnalysis)
        {
            if ((callerKeys & I::ClipKeyAnalysis) && bMergedAnalysis != sidecar.bAnalysis)
            {
                return fail("clip_conflict_analysis");
            }
            bMergedAnalysis = sidecar.bAnalysis;
        }
        constexpr uint32_t analysisKeys = I::ClipKeyRootJoint | I::ClipKeyLoop | I::ClipKeyLoopThreshold |
                                          I::ClipKeyAuthoredFps | I::ClipKeySourceFps | I::ClipKeyTimeScale;
        if (!bMergedAnalysis && ((sidecar.Keys | callerKeys) & analysisKeys))
        {
            return fail("clip_analysis_disabled");
        }
        if ((merged.AuthoredFps == 0) != (merged.SourceFps == 0))
        {
            return fail("clip_fps_pair");
        }
        bAnalyzeClips = bMergedAnalysis;
        analysis = merged;
        rootJoint = std::move(mergedRoot);
        return true;
    }
    bool SerializeRigSplitManifest(C::Span<const A::AssetCookedReference> references, Text& out, Text& error)
    {
        Text candidate = "{\"version\":1,\"assets\":[";
        for (size_t i = 0; i < references.size(); ++i)
        {
            if (i)
            {
                candidate.push_back(',');
            }
            ManifestEntry(candidate, references[i]);
        }
        candidate += "]}";
        A::AssetManifest verify;
        if (!verify.LoadFromJsonText(C::String(candidate.c_str())))
        {
            error = verify.GetParseError();
            return false;
        }
        out = std::move(candidate);
        return true;
    }
    bool CookRigSplitV1NativePath(C::Span<const uint8_t> source, const RigSplitCookRequest& request,
                                  RigSplitCookResult& out, S::RigV1Report& report, Text& error)
    {
        report = {};
        error.clear();
        bool bSucceeded = false;
        struct FailureStatus
        {
            S::RigV1Report& Report;
            bool& bSucceeded;
            ~FailureStatus()
            {
                if (!bSucceeded && Report.Status == S::RigV1Status::Success)
                {
                    Report.Status = S::RigV1Status::InvalidInput;
                }
            }
        } failureStatus{report, bSucceeded};
        try
        {
            const auto fail = [&](const char* why)
            {
                error = why;
                return false;
            };
            if (!source.data() || source.empty() || !S::IsSupportedRigImportProfile(request.Profile) ||
                !S::IsValidRigProfileLimits(request.Profile, request.Limits) ||
                source.size() > request.Limits.MaxSourceBytes || !IsValidRigSplitImageLimits(request.ImageLimits) ||
                !S::IsValidSkeletalGltfDecodeOptions(request.DecodeOptions) ||
                !S::IsSplitLogicalPath(request.SkeletonPath) || !S::IsSplitLogicalPath(request.MeshPath) ||
                !S::IsSplitLogicalPath(request.BankPath) || !S::IsSplitLogicalPath(request.Variant) ||
                !S::IsSplitLogicalPath(request.PackageDirectory) || request.PackageDirectory.size() > 4055 ||
                request.SkeletonPath == request.MeshPath || request.SkeletonPath == request.BankPath ||
                request.MeshPath == request.BankPath)
            {
                return fail("split_cook_request");
            }
            std::string clipOptimizeError;
            if (!ValidateClipOptimizeOptions(request.ClipOptimize, clipOptimizeError))
            {
                return fail(clipOptimizeError.c_str());
            }
            RigSplitCookResult candidate;
            if (I::LoadImportSettingsDocument(request.SourcePath, request.ImportOptions, candidate.Import).Result !=
                I::SettingsFileResult::Success)
            {
                return fail("split_import_settings");
            }
            bool bAnalyzeClips = request.bAnalyzeClips;
            auto analysisOptions = request.ClipAnalysis;
            Text rootJoint(request.ClipRootJoint.size(), '\0');
            if (!request.ClipRootJoint.empty())
            {
                std::copy(request.ClipRootJoint.begin(), request.ClipRootJoint.end(), rootJoint.begin());
            }
            Text clipError;
            if (!ApplyClipImportSettings(candidate.Import.Settings.Clip, request.ClipExplicitKeys, bAnalyzeClips,
                                         analysisOptions, rootJoint, clipError))
            {
                return fail(clipError.c_str());
            }
            I::LoadedImportSettings geometrySettings;
            geometrySettings.Settings = candidate.Import.Settings.Geometry;
            geometrySettings.Path = candidate.Import.Path;
            geometrySettings.bPresent = candidate.Import.bPresent;
            S::RigAuthoringCpu rig;
            S::RigGltfImportCapture capture;
            // absent設定もnon-nullで渡し、decoderからsidecarを読み直させない。
            if (!S::DecodeRigAuthoringWithProfileNativePath(source, request.SourcePath, request.Profile, rig, report,
                                                            request.Limits, &geometrySettings, &request.DecodeOptions,
                                                            &capture))
            {
                return fail("split_author_import");
            }
            if (!rootJoint.empty())
            {
                if (!bAnalyzeClips || analysisOptions.RootJoint != UINT32_MAX)
                {
                    return fail("split_clip_root_ambiguous");
                }
                const auto& topology = rig.GetData()->Topology;
                bool found = false;
                for (size_t i = 0; i < topology.Joints.size(); ++i)
                {
                    if (topology.Joints[i].Name == rootJoint)
                    {
                        analysisOptions.RootJoint = topology.CanonicalToSource[i];
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return fail("split_clip_root_not_found");
                }
            }
            const auto root = capture.Document.GetRoot();
            const auto materials = root.FindMember("materials");
            if (materials.GetArraySize() > 256 ||
                root.FindMember("images").GetArraySize() > request.ImageLimits.MaxSourceImages ||
                root.FindMember("textures").GetArraySize() > 1024)
            {
                return fail("split_material_input_limit");
            }
            uint64_t nameBytes = 0;
            for (size_t i = 0; i < materials.GetArraySize(); ++i)
            {
                const auto& name = materials.GetArrayElement(i).FindMember("name").AsString();
                if (name.empty())
                {
                    continue;
                }
                const auto measured =
                    A::MeasureSkeletalNameEncoding(2, C::Span<const C::String::value_type>{name.data(), name.size()});
                if (!measured.Succeeded() || measured.ByteCount > request.Limits.MaxNameBytes)
                {
                    return fail("split_material_name_limit");
                }
                nameBytes += measured.ByteCount;
                if (nameBytes > request.Limits.MaxStringBytes)
                {
                    return fail("split_material_name_limit");
                }
            }
            I::SourceMaterialCatalog catalog;
            I::ResolvedMaterialImportPlan resolved;
            if (I::ReadSourceMaterialCatalog(root, catalog) != I::SettingsResult::Success)
            {
                return fail("split_material_selector_or_settings");
            }
            const auto outcome =
                I::ResolveMaterialImportPlan(catalog, candidate.Import.Settings, request.AssetSetEmission, resolved);
            if (!outcome.Succeeded())
            {
                return fail(outcome.Status == I::MaterialImportPlanStatus::SettingsFailed
                                ? I::MaterialSettingsErrorKey(outcome.Settings)
                                : "split_material_selector_or_settings");
            }
            // 保存先(GR81)ができるまで surface を受け取って捨てない。
            for (const auto& entry : resolved.Materials)
            {
                if (entry.bSurfacePresent)
                {
                    return fail("surface_requires_GR81");
                }
            }
            candidate.DecodeReport = rig.GetData()->DecodeReport;
            candidate.DuplicateMaterialNameGroups = resolved.DuplicateNameGroups;
            if (resolved.DuplicateNameGroups)
            {
                candidate.Warnings.push_back(
                    Text("asset=") + request.MeshPath +
                    " 同名材質の入れ替わりは判別できません。作成元で材質名を一意に付け直してください。indices=" +
                    Number(resolved.FirstDuplicateMaterialIndex) + "," + Number(resolved.SecondDuplicateMaterialIndex));
            }
            candidate.SlotSourceMaterials = capture.SlotSourceMaterialIndices;
            if (candidate.SlotSourceMaterials.size() != rig.GetData()->Geometry.MaterialSlots.size() ||
                candidate.SlotSourceMaterials.empty())
            {
                return fail("split_slot_source_mapping");
            }
            auto keys = candidate.SlotSourceMaterials;
            std::sort(keys.begin(), keys.end());
            RigSplitImageInputs images;
            images.Capture = &capture;
            images.SourcePath = request.SourcePath;
            images.Limits = request.ImageLimits;
            LoadedMeshMaterialV1Input loaded;
            loaded.Import = &candidate.Import;
            loaded.Catalog = &catalog;
            loaded.Resolved = &resolved;
            loaded.ImageContext = &images;
            loaded.ReadImage = ImageRead;
            loaded.ReserveDerived = Derived;
            C::VariableArray<MeshMaterialV1Plan> plans;
            plans.reserve(keys.size());
            // Entries の再確保をまたぐので、全材質を読むまでは画像の ID だけを持つ。
            struct PendingSurfaceAlbedo
            {
                uint32_t ImageIndex = UINT32_MAX;
                float Factor[3] = {1, 1, 1};
            };
            const bool bSurfaceSamples = candidate.Import.Settings.SurfaceSamples.bRequested;
            C::VariableArray<PendingSurfaceAlbedo> sourceAlbedos;
            if (bSurfaceSamples)
            {
                sourceAlbedos.reserve(keys.size());
            }
            Hash hash;
            hash.String(request.Profile == S::RigImportProfile::DirectTrs128 ? "NorvesLib.RigSplit.v1"
                        : request.Profile == S::RigImportProfile::StaticRootFrame128
                            ? "NorvesLib.RigSplit.StaticRootFrame128.v1"
                            : "NorvesLib.RigSplit.StaticRootFrame256.v1");
            hash.Number(1);
            if (request.Profile != S::RigImportProfile::DirectTrs128)
            {
                hash.Number(uint32_t(request.Profile));
            }
            if (bAnalyzeClips)
            {
                hash.String("RigClipAnalysis.v1");
                const auto& a = analysisOptions;
                hash.Number(uint32_t(a.Loop));
                hash.Number(a.RootJoint);
                hash.Number(a.MaximumSamples);
                for (double value :
                     {a.LoopThreshold, a.ReferenceLengthMeters, a.SampleRate, a.TimeScale, a.AuthoredFps, a.SourceFps})
                {
                    hash.Number(std::bit_cast<uint64_t>(value));
                }
            }
            hash.Value = I::AppendClipImportSettingsHash(hash.Value, candidate.Import.Settings.Clip);
            hash.Number(GeometryClosureAlgorithmRevision);
            hash.Data(source);
            hash.Number(capture.Buffers.GetCount());
            for (size_t i = 0; i < capture.Buffers.GetCount(); ++i)
            {
                hash.Number(uint64_t(capture.Buffers.GetSourceKind(i)));
                hash.Data(capture.Buffers.GetSourceBytes(i));
            }
            hash.Number(candidate.Import.bPresent);
            hash.Data(candidate.Import.RawSourceBytes);
            hash.String(request.SkeletonPath);
            hash.String(request.MeshPath);
            hash.String(request.BankPath);
            hash.String(request.Variant);
            const auto policy = S::AppendSkeletalImportPolicyHash(hash.Value, request.DecodeOptions);
            if (!policy.bValid)
            {
                return fail("split_import_policy");
            }
            hash.Value = policy.Value;
            for (const auto key : keys)
            {
                MeshMaterialV1Plan plan;
                if (!PrepareMeshMaterialV1(root, capture.Buffers, request.SourcePath, request.MeshPath,
                                           key != UINT64_MAX, uint32_t(key), 0, nullptr, plan, error, &loaded))
                {
                    return false;
                }
                hash.Number(key);
                hash.Number(plan.SourceHash);
                hash.Number(plan.SettingsHash);
                if (bSurfaceSamples)
                {
                    PendingSurfaceAlbedo sourceAlbedo;
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        sourceAlbedo.Factor[channel] = plan.Material.BaseColor[channel];
                    }
                    if (!plan.Textures[0].empty())
                    {
                        const auto albedo = std::find_if(plan.Images.begin(), plan.Images.end(),
                            [&](const auto& image) { return image.LogicalPath == plan.Textures[0]; });
                        if (albedo == plan.Images.end())
                        {
                            return fail("split_surface_samples_albedo");
                        }
                        const auto sourceImage = std::find_if(images.Entries.begin(), images.Entries.end(),
                            [&](const auto& entry) { return entry.Index == albedo->ImageIndex; });
                        if (sourceImage == images.Entries.end())
                        {
                            return fail("split_surface_samples_albedo");
                        }
                        sourceAlbedo.ImageIndex = sourceImage->Index;
                    }
                    sourceAlbedos.push_back(sourceAlbedo);
                }
                if (plan.Material.BaseColor[0] != 1.0f || plan.Material.BaseColor[1] != 1.0f ||
                    plan.Material.BaseColor[2] != 1.0f)
                {
                    const float factor[] = {plan.Material.BaseColor[0], plan.Material.BaseColor[1],
                                            plan.Material.BaseColor[2]};
                    const bool bSolid = plan.Textures[0].empty();
                    auto albedo = plan.Images.end();
                    const DecodedTextureRgba8* decoded = nullptr;
                    uint32_t width = 4, height = 4;
                    if (!bSolid)
                    {
                        albedo = std::find_if(plan.Images.begin(), plan.Images.end(),
                                              [&](const auto& image) { return image.LogicalPath == plan.Textures[0]; });
                        if (albedo == plan.Images.end())
                        {
                            return fail("split_albedo_bake");
                        }
                        const auto sourceImage = std::find_if(images.Entries.begin(), images.Entries.end(),
                                                              [&](const auto& entry)
                                                              { return entry.Index == albedo->ImageIndex; });
                        if (sourceImage == images.Entries.end())
                        {
                            return fail("split_albedo_bake");
                        }
                        decoded = &sourceImage->Decoded;
                        width = decoded->Width;
                        height = decoded->Height;
                    }
                    const uint64_t byteCount = uint64_t(width) * height * 4;
                    if (!images.ReserveDerived(byteCount, width, height, error))
                    {
                        error = "split_albedo_output_limit";
                        return false;
                    }
                    uint8_t table[3][256]{};
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        for (unsigned value = 0; value < 256; ++value)
                        {
                            if (I::BakeBaseColorByte(static_cast<uint8_t>(value), factor[channel],
                                                     table[channel][value]) != I::MaterialPolicyStatus::Success)
                            {
                                return fail("split_albedo_bake");
                            }
                        }
                    }
                    if (decoded && decoded->Pixels.size() != byteCount)
                    {
                        return fail("split_albedo_bake");
                    }
                    Bytes pixels(static_cast<size_t>(byteCount));
                    for (size_t offset = 0; offset < pixels.size(); offset += 4)
                    {
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            pixels[offset + channel] = table[channel][decoded ? decoded->Pixels[offset + channel] : 255];
                        }
                        pixels[offset + 3] = decoded ? decoded->Pixels[offset + 3] : 255;
                    }
                    MeshEmbeddedImage baked;
                    if (!baked.SetRawRgba8(pixels, width, height))
                    {
                        return fail("split_albedo_bake");
                    }
                    baked.ImageIndex = key == UINT64_MAX ? (uint64_t{3} << 33) : (uint64_t{1} << 34) + key;
                    baked.Roles = static_cast<uint8_t>(MeshImageRole::Albedo);
                    baked.Format = "nvtex.v0.rgba8.srgb";
                    baked.LogicalPath = request.MeshPath + (key == UINT64_MAX ? Text(".matdefault.albedo.rgba8")
                                                                               : Text(".mat") + Number(key) + ".albedo.rgba8");
                    Hash raw;
                    raw.String("NorvesLib.RigSplit.baked-albedo.v1");
                    raw.Number(static_cast<uint8_t>(baked.Payload));
                    raw.Number(baked.Width);
                    raw.Number(baked.Height);
                    raw.String(baked.Format);
                    raw.Data(baked.GetBytes());
                    baked.SourceHash = raw.Value;
                    if (!bSolid)
                    {
                        albedo->Roles &= static_cast<uint8_t>(~static_cast<uint8_t>(MeshImageRole::Albedo));
                        if (albedo->Roles == 0)
                        {
                            plan.Images.erase(albedo);
                        }
                    }
                    plan.Textures[0] = baked.LogicalPath;
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        plan.Material.BaseColor[channel] = 1.0f;
                    }
                    plan.Images.push_back(std::move(baked));
                    hash.String("NorvesLib.RigSplit.base-color-bake.v1");
                    for (float value : factor)
                    {
                        hash.Number(std::bit_cast<uint32_t>(value));
                    }
                    candidate.BaseColorBakes.push_back({key, {factor[0], factor[1], factor[2]}, bSolid});
                }
                for (auto& image : plan.Images)
                {
                    if (image.ImageIndex == SyntheticArmImageIndex)
                    {
                        image.ImageIndex = key == UINT64_MAX ? (uint64_t{1} << 33) : uint64_t(UINT32_MAX) + 1 + key;
                        image.LogicalPath =
                            request.MeshPath + (key == UINT64_MAX ? Text(".matdefault.arm.rgba8")
                                                                  : Text(".mat") + Number(key) + ".arm.rgba8");
                        plan.Textures[2] = image.LogicalPath;
                    }
                    if (!MergeImage(candidate.TexturePlans, std::move(image), error))
                    {
                        return false;
                    }
                }
                plans.push_back(std::move(plan));
            }
            GeometryClosureInspection closure;
            const bool needsClosure = std::any_of(plans.begin(), plans.end(), [](const auto& p)
                                                  { return p.Sidedness == I::DoubleSidedSetting::Auto; });
            if (needsClosure && !Closure(rig.GetData()->Geometry, closure))
            {
                return fail("split_material_closure");
            }
            // ここから画像の読み込みは増えず、images は標本の生成が終わるまで生存する。
            C::VariableArray<SurfaceSampleAlbedoSource> slotAlbedo;
            if (bSurfaceSamples)
            {
                slotAlbedo.reserve(candidate.SlotSourceMaterials.size());
            }
            C::VariableArray<S::SkinMaterialV1> slots;
            slots.reserve(candidate.SlotSourceMaterials.size());
            for (const auto key : candidate.SlotSourceMaterials)
            {
                hash.Number(key);
                const auto found = std::lower_bound(keys.begin(), keys.end(), key);
                if (found == keys.end() || *found != key)
                {
                    return fail("split_slot_source_mapping");
                }
                const size_t planIndex = size_t(found - keys.begin());
                const auto& p = plans[planIndex];
                if (bSurfaceSamples)
                {
                    const auto& pending = sourceAlbedos[planIndex];
                    SurfaceSampleAlbedoSource albedo;
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        albedo.Factor[channel] = pending.Factor[channel];
                    }
                    if (pending.ImageIndex != UINT32_MAX)
                    {
                        const auto image = std::find_if(images.Entries.begin(), images.Entries.end(),
                            [&](const auto& entry) { return entry.Index == pending.ImageIndex; });
                        if (image == images.Entries.end())
                        {
                            return fail("split_surface_samples_albedo");
                        }
                        albedo.Image = &image->Decoded;
                    }
                    slotAlbedo.push_back(albedo);
                }
                S::SkinMaterialV1 material;
                material.Record = p.Material;
                if (p.Sidedness == I::DoubleSidedSetting::Auto && !closure.bAlmostClosed)
                {
                    material.Record.Flags |= A::CookedMaterialFormatV1::DoubleSided;
                }
                for (size_t i = 0; i < 4; ++i)
                {
                    material.Textures[i] = p.Textures[i];
                }
                slots.push_back(std::move(material));
            }
            std::sort(candidate.TexturePlans.begin(), candidate.TexturePlans.end(),
                      [](const auto& a, const auto& b) { return a.ImageIndex < b.ImageIndex; });
            if (request.ClipOptimize.bEnabled)
            {
                hash.String("norves.clip-optimize.v1");
                const auto& settings = request.ClipOptimize.Settings;
                hash.Number(std::bit_cast<uint64_t>(settings.MaxVertexErrorMeters));
                hash.Number(std::bit_cast<uint64_t>(settings.MaxJointErrorMeters));
                hash.Number(std::bit_cast<uint64_t>(settings.MaxRotationErrorDegrees));
            }
            if (request.ClipOptimize.bQuantize)
            {
                hash.String("norves.clip-quantize.v1");
            }
            if (candidate.Import.Settings.SkinnedLod.bRequested)
            {
                const auto& settings = candidate.Import.Settings.SkinnedLod;
                hash.String("norves.skinned-lod.v1");
                hash.Number(settings.MinParts);
                hash.Number(settings.MaxParts);
                hash.Number(settings.Targets.size());
                for (double target : settings.Targets)
                {
                    hash.Number(std::bit_cast<uint64_t>(target));
                }
                hash.Number(std::bit_cast<uint64_t>(settings.ThetaRef));
            }
            if (bSurfaceSamples)
            {
                hash.String("norves.surface-samples.v1");
                hash.Number(candidate.Import.Settings.SurfaceSamples.Count);
            }
            candidate.SourceHash = hash.Value;
            if (request.bInventoryOnly)
            {
                out = std::move(candidate);
                report.Status = S::RigV1Status::Success;
                bSucceeded = true;
                return true;
            }
            C::VariableArray<S::SkeletalAnimationClip> optimizedClips;
            if (request.ClipOptimize.bEnabled)
            {
                const auto* data = rig.GetData();
                optimizedClips = data->Geometry.Clips;
                const Core::Animation::ClipOptimizeRig optimizeRig{
                    data->Geometry.Joints, data->LocalRest, data->Geometry.Vertices};
                if (!RunClipOptimizeStage(request.ClipOptimize, optimizeRig, optimizedClips, clipOptimizeError))
                {
                    return fail(clipOptimizeError.c_str());
                }
            }
            S::SkeletonV1 skeleton;
            S::SkinMeshV1 mesh;
            S::ClipBankV1 bank;
            if (!S::BuildSkeletonV1(rig, skeleton, report, request.Limits, request.Profile) ||
                !S::BuildSkinMeshV1(rig, skeleton, request.SkeletonPath, slots, mesh, report, request.Limits,
                                    request.Profile) ||
                !S::BuildClipBankV1({&rig, 1}, bank, report, request.Limits, request.Profile,
                                    bAnalyzeClips ? &analysisOptions : nullptr, optimizedClips))
            {
                return fail("split_role_payload");
            }
            if (candidate.Import.Settings.SkinnedLod.bRequested)
            {
                const auto& settings = candidate.Import.Settings.SkinnedLod;
                const auto& data = *mesh.GetData();
                C::VariableArray<SkinnedLodSubMeshRange> subMeshRanges;
                subMeshRanges.reserve(data.SubMeshes.size());
                for (const auto& subMesh : data.SubMeshes)
                {
                    subMeshRanges.push_back({subMesh.IndexStart, subMesh.IndexCount});
                }
                // 逆バインド行列の逆を行ベクトルとして読み、メッシュ空間の関節位置を取る。
                C::VariableArray<float> jointBindPositions;
                jointBindPositions.reserve(data.InverseBindMatrices.size() * 3);
                for (const auto& v : data.InverseBindMatrices)
                {
                    const Math::Matrix4x4 inverseBind(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
                                                       v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
                    Math::Matrix4x4 bind;
                    if (!Core::Animation::Detail::TryInverseMatrix(inverseBind, bind))
                    {
                        return fail("skinned_lod_build");
                    }
                    const auto position = bind.GetTranslationRow();
                    jointBindPositions.push_back(position.x);
                    jointBindPositions.push_back(position.y);
                    jointBindPositions.push_back(position.z);
                }
                const SkinnedLodInput input{
                    {data.Vertices.data(), data.Vertices.size()},
                    {data.Indices.data(), data.Indices.size()},
                    {subMeshRanges.data(), subMeshRanges.size()},
                    uint32_t(data.Topology.Joints.size()),
                    {jointBindPositions.data(), jointBindPositions.size()}};
                SkinnedLodParams params;
                params.MinParts = settings.MinParts;
                params.MaxParts = settings.MaxParts;
                params.MaxPartJoints = S::MaximumSkinPartJointCount;
                params.Targets.clear();
                for (double target : settings.Targets)
                {
                    params.Targets.push_back(float(target));
                }
                params.ThetaRef = float(settings.ThetaRef);
                SkinnedLodResult lodResult;
                Text lodError;
                if (!BuildSkinnedLods(input, params, lodResult, lodError))
                {
                    return fail(lodError.empty() ? "skinned_lod_build" : lodError.c_str());
                }
                S::SkinLodTablesV1 tables;
                tables.Lods.reserve(lodResult.Lods.size());
                for (const auto& lod : lodResult.Lods)
                {
                    tables.Lods.push_back({lod.TriangleCount, lod.MaxError});
                }
                tables.Parts.reserve(lodResult.Parts.size());
                for (const auto& sourcePart : lodResult.Parts)
                {
                    S::SkinPartV1 part;
                    part.SubMeshIndex = sourcePart.SubMeshIndex;
                    for (size_t i = 0; i < sourcePart.LodRanges.size(); ++i)
                    {
                        part.LodRanges[i] = {sourcePart.LodRanges[i].FirstIndex, sourcePart.LodRanges[i].IndexCount};
                    }
                    part.JointBounds.reserve(sourcePart.JointBounds.size());
                    for (const auto& bounds : sourcePart.JointBounds)
                    {
                        part.JointBounds.push_back({bounds.JointIndex, bounds.Min, bounds.Max});
                    }
                    tables.Parts.push_back(std::move(part));
                }
                auto subMeshes = data.SubMeshes;
                for (size_t i = 0; i < subMeshes.size(); ++i)
                {
                    subMeshes[i].IndexStart = lodResult.SubMeshes[i].IndexStart;
                    subMeshes[i].IndexCount = lodResult.SubMeshes[i].IndexCount;
                }
                S::SkinMeshV1 meshWithLods;
                if (!S::AttachSkinMeshLodsV1(mesh, std::move(lodResult.Indices), std::move(subMeshes),
                                             std::move(tables), meshWithLods, report, request.Limits, request.Profile))
                {
                    return fail("skinned_lod_attach");
                }
                mesh = std::move(meshWithLods);
                candidate.SkinnedLod.bBuilt = true;
                candidate.SkinnedLod.PartCount = uint32_t(lodResult.Parts.size());
                candidate.SkinnedLod.ZeroWeightVertexCount = lodResult.ZeroWeightVertexCount;
                candidate.SkinnedLod.DegenerateTriangleCount = lodResult.DegenerateTriangleCount;
                candidate.SkinnedLod.DuplicateTriangleCount = lodResult.DuplicateTriangleCount;
                candidate.SkinnedLod.Lods = std::move(lodResult.Lods);
            }
            if (bSurfaceSamples)
            {
                C::VariableArray<S::SurfaceSampleRecord> samples;
                if (!BuildSurfaceSampleTable(*mesh.GetData(), {slotAlbedo.data(), slotAlbedo.size()},
                                             candidate.Import.Settings.SurfaceSamples.Count, samples, error))
                {
                    return fail("split_surface_samples_build");
                }
                S::SkinMeshV1 meshWithSamples;
                if (!S::AttachSkinMeshSurfaceSamplesV1(mesh, std::move(samples), meshWithSamples, report,
                                                      request.Limits, request.Profile))
                {
                    return fail("split_surface_samples_attach");
                }
                mesh = std::move(meshWithSamples);
            }
            if (!S::WriteSkeletonV1(skeleton, candidate.Skeleton.Payload, report, request.Limits, request.Profile) ||
                !S::WriteSkinMeshV1(mesh, candidate.Mesh.Payload, report, request.Limits, request.Profile) ||
                !S::WriteClipBankV1(bank, candidate.Bank.Payload, report, request.Limits, request.Profile,
                                    S::ClipBankV1WriteOptions{.bQuantize = request.ClipOptimize.bQuantize}))
            {
                return fail("split_role_payload");
            }
            RigSplitCookEntry* entries[] = {&candidate.Skeleton, &candidate.Mesh, &candidate.Bank};
            const Text* paths[] = {&request.SkeletonPath, &request.MeshPath, &request.BankPath};
            const char* formats[] = {"nvskel.v1.skeleton", "nvskel.v1.skinmesh.pnujiw.u32", "nvskel.v1.clips"};
            const A::AssetKind kinds[] = {A::AssetKind::Skeleton, A::AssetKind::Model, A::AssetKind::Animation};
            const A::AssetPackageFourCC types[] = {A::MakeAssetPackageFourCC('S', 'k', 'e', '1'),
                                                   A::MakeAssetPackageFourCC('S', 'k', 'm', '1'),
                                                   A::MakeAssetPackageFourCC('A', 'n', 'm', '1')};
            for (size_t i = 0; i < 3; ++i)
            {
                auto& ref = entries[i]->Reference;
                ref.LogicalPath = *paths[i];
                ref.Format = formats[i];
                ref.Kind = kinds[i];
                ref.EntryType = types[i];
                ref.bHasRigSplitMetadata = true;
                ref.RigSplitMetadata.Role = uint32_t(i + 1);
                ref.RigSplitMetadata.Profile = uint32_t(request.Profile);
                ref.RigSplitMetadata.JointCount = uint32_t(skeleton.GetData()->Topology.Joints.size());
                ref.RigSplitMetadata.SkeletonId = skeleton.GetData()->Topology.SkeletonId;
            }
            auto& mm = candidate.Mesh.Reference.RigSplitMetadata;
            const auto& md = *mesh.GetData();
            mm.VertexCount = uint32_t(md.Vertices.size());
            mm.IndexCount = uint32_t(md.Indices.size());
            mm.SubmeshCount = uint32_t(md.SubMeshes.size());
            mm.MaterialSlotCount = uint32_t(md.Slots.size());
            mm.MaterialCount = uint32_t(md.Materials.size());
            auto& bm = candidate.Bank.Reference.RigSplitMetadata;
            const auto& bd = *bank.GetData();
            bm.ClipCount = uint32_t(bd.Clips.size());
            bm.SnapshotCount = uint32_t(bd.Snapshots.size());
            for (const auto& clip : bd.Clips)
            {
                bm.ChannelCount += uint32_t(clip.Channels.size());
                bm.SampleCount += uint32_t(clip.RootMotion.size());
                for (const auto& channel : clip.Channels)
                {
                    bm.SampleCount += uint32_t(channel.Samples.size());
                }
            }
            for (size_t i = 0; i < 3; ++i)
            {
                if (!Package(request, *entries[i], uint32_t(i + 1), candidate.SourceHash, error))
                {
                    return false;
                }
            }
            candidate.ManifestJson = "{\"version\":1,\"assets\":[";
            for (size_t i = 0; i < 3; ++i)
            {
                if (i)
                {
                    candidate.ManifestJson.push_back(',');
                }
                ManifestEntry(candidate.ManifestJson, entries[i]->Reference);
            }
            candidate.ManifestJson += "]}";
            A::AssetManifest verify;
            if (!verify.LoadFromJsonText(C::String(candidate.ManifestJson.c_str())))
            {
                error = verify.GetParseError();
                return false;
            }
            out = std::move(candidate);
            report.Status = S::RigV1Status::Success;
            bSucceeded = true;
            return true;
        }
        catch (...)
        {
            report.Status = S::RigV1Status::Exception;
            error = "split_cook_exception";
            return false;
        }
    }
    void PrintRigSplitBaseColorImportReport(const RigSplitCookRequest& request, const RigSplitCookResult& result)
    {
        if (result.SkinnedLod.bBuilt)
        {
            const auto& lodReport = result.SkinnedLod;
            std::fprintf(stderr, "skinned_lod_report={\"parts\":%u,\"zero_weight_vertices\":%u,"
                                 "\"degenerate_triangles\":%u,\"duplicate_triangles\":%u,\"lods\":[",
                         lodReport.PartCount, lodReport.ZeroWeightVertexCount, lodReport.DegenerateTriangleCount,
                         lodReport.DuplicateTriangleCount);
            for (size_t i = 0; i < lodReport.Lods.size(); ++i)
            {
                const auto& lod = lodReport.Lods[i];
                std::fprintf(stderr, "%s{\"triangles\":%u,\"target_triangles\":%u,\"max_error\":%.9g,"
                                     "\"position_error\":%.9g,\"skin_error\":%.9g}",
                             i ? "," : "", lod.TriangleCount, lod.TargetTriangleCount,
                             double(lod.MaxError), double(lod.PositionError), double(lod.SkinError));
            }
            std::fprintf(stderr, "]}\n");
        }
        if (result.BaseColorBakes.empty())
        {
            return;
        }
        SkeletalImportReportInput input;
        input.Outcome = SkeletalImportOutcome::PayloadReady;
        input.SourceHash = result.SourceHash;
        input.Options = request.DecodeOptions;
        input.Diagnostics.bDecodeAttempted = true;
        input.Diagnostics.DecodeStatus = 0;
        input.Diagnostics.Report = result.DecodeReport;
        input.BaseColorBakes = {result.BaseColorBakes.data(), result.BaseColorBakes.size()};
        const auto json = BuildSkeletalImportReport(input);
        if (json.bValid)
        {
            std::fprintf(stderr, "import_report=%.*s\n", int(json.Size), json.Bytes);
        }
        else
        {
            std::fprintf(stderr, "骨格ImportReportの測定値が不正です\n");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
