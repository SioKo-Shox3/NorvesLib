#include "RigSplitCook.h"
#include "MeshMaterialV1Plan.h"
#include "AssetCookOutput.h"
#include "GeometryInspection.h"
#include "Resource/SkeletalImportPolicy.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include <cstdio>
namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace I = Core::AssetImport;
    namespace A = Core::Asset;
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
            Numeric(json, "cooked_version", 1);
            json += " ,\"metadata\":{";
            const auto& m = r.RigSplitMetadata;
            StringField(json, "skeleton_id", A::FormatAssetHashHex(m.SkeletonId), true);
            Numeric(json, "profile", 1);
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
            if (!source.data() || source.empty() || !S::IsValidRigV1Limits(request.Limits) ||
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
            RigSplitCookResult candidate;
            if (I::LoadImportSettingsDocument(request.SourcePath, request.ImportOptions, candidate.Import).Result !=
                I::SettingsFileResult::Success)
            {
                return fail("split_import_settings");
            }
            I::LoadedImportSettings geometrySettings;
            geometrySettings.Settings = candidate.Import.Settings.Geometry;
            geometrySettings.Path = candidate.Import.Path;
            geometrySettings.bPresent = candidate.Import.bPresent;
            S::RigAuthoringCpu rig;
            S::RigGltfImportCapture capture;
            // absent設定もnon-nullで渡し、decoderからsidecarを読み直させない。
            if (!S::DecodeRigAuthoringNativePath(source, request.SourcePath, rig, report, request.Limits,
                                                 &geometrySettings, &request.DecodeOptions, &capture))
            {
                return fail("split_author_import");
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
            if (I::ReadSourceMaterialCatalog(root, catalog) != I::SettingsResult::Success ||
                !I::ResolveMaterialImportPlan(catalog, candidate.Import.Settings, request.AssetSetEmission, resolved)
                     .Succeeded())
            {
                return fail("split_material_selector_or_settings");
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
            Hash hash;
            hash.String("NorvesLib.RigSplit.v1");
            hash.Number(1);
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
                const auto& p = plans[size_t(found - keys.begin())];
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
            candidate.SourceHash = hash.Value;
            S::SkeletonV1 skeleton;
            S::SkinMeshV1 mesh;
            S::ClipBankV1 bank;
            if (!S::BuildSkeletonV1(rig, skeleton, report, request.Limits) ||
                !S::BuildSkinMeshV1(rig, skeleton, request.SkeletonPath, slots, mesh, report, request.Limits) ||
                !S::BuildClipBankV1({&rig, 1}, bank, report, request.Limits) ||
                !S::WriteSkeletonV1(skeleton, candidate.Skeleton.Payload, report, request.Limits) ||
                !S::WriteSkinMeshV1(mesh, candidate.Mesh.Payload, report, request.Limits) ||
                !S::WriteClipBankV1(bank, candidate.Bank.Payload, report, request.Limits))
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
                ref.RigSplitMetadata.Profile = 1;
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
} // namespace NorvesLib::Tools::AssetCook
