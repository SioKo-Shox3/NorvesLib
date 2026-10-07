// same-read材質/sidecar/BufferSetと三role packageの所有を反証する。
#include "RigSplitTestFixture.h"
#include "Tools/AssetCook/RigSplitFileCook.h"
#include "Tools/AssetCook/TextureAssetSetCook.h"
#include "Tools/AssetCook/RigSingleCook.h"
#include "Tools/AssetCook/RigRetargetCook.h"
#include "RigSplitWireTestFixture.h"
#include "Tools/AssetCook/MeshMaterialV1Plan.h"
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace X = NorvesLib::Tests::RigSplitFixture;
namespace Cook = NorvesLib::Tools::AssetCook;
namespace Core = NorvesLib::Core;
namespace S = NorvesLib::Core::Skeletal;
namespace C = NorvesLib::Core::Container;
namespace
{
    void RetargetBatch(F::Fixture& f)
    {
        f.SetBuffer(f.Binary);
        const auto sourceRoot = f.Root / "RigV1Fixture";
        const auto write = [&](const std::filesystem::path& path, const F::Text& text)
        { F::WriteBytes(path, F::Bytes(text.begin(), text.end())); };
        write(sourceRoot / "target.gltf", f.Json);
        const auto motion = F::Replace(F::RootTrs(f.Json, R"("rotation":[0,0.7071067811865475,0,0.7071067811865475],)"),
                                       R"(,{"sampler":1,"target":{"node":0,"path":"rotation"}})", "");
        write(sourceRoot / "motion.gltf", motion);
        write(sourceRoot / "run.bvh", R"bvh(HIERARCHY
ROOT Root { OFFSET 0 0 0 CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
 JOINT Child { OFFSET 0 1 0 CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation End Site { OFFSET 0 1 0 } }
}
MOTION
Frames: 3
Frame Time: 0.5
0 0 0 0 0 45 10 0 0 0 0 0
1 .25 0 0 0 60 20 0 0 30 0 0
2 0 0 0 0 75 30 0 0 0 0 0
)bvh");
        const F::Text profile =
            R"json({"version":1,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{"root":["Root"],"spine":["Child"]},"target_roles":{"root":[{"joint":"Root"}],"spine":[{"joint":"Child"}]},"rest_pose":{"mode":"match"},"processing":{"loop_mode":"none","output_fps":30}})json";
        namespace A = Core::Animation;
        auto target = f.Import(f.Json);
        auto gltfAuthor = f.Import(motion);
        A::SkeletalRoleProfile parsedProfile;
        RIG_CHECK(A::ParseSkeletalRoleProfile(F::View(profile), {}, parsedProfile).Succeeded());
        A::SkeletalRetargetClipSource input;
        F::Text conversionError;
        RIG_CHECK(A::MakeGltfRetargetClipSource(gltfAuthor, 0, input, conversionError));
        A::SkeletalClipRetargetSettings settings;
        settings.RestMode = A::SkeletalRestCorrectionMode::Match;
        settings.Processing.Loop = A::SkeletalLoopSelection::None;
        S::SkeletalAnimationClip converted;
        A::SkeletalClipRetargetReport convertedReport;
        RIG_CHECK(A::RetargetSkeletalClip(input, target, parsedProfile, settings, converted, convertedReport,
                                          conversionError));
        RIG_CHECK(!convertedReport.bKeyErrorMeasured && convertedReport.Corrections.size() == 2);
        Core::Bvh::BvhDocument bvh;
        const auto bvhBytes = F::ReadBytes(sourceRoot / "run.bvh");
        RIG_CHECK(Core::Bvh::DecodeBvh(bvhBytes, {}, bvh).Succeeded());
        A::SkeletalRetargetClipSource bvhSource;
        RIG_CHECK(
            A::MakeBvhRetargetClipSource(bvh, parsedProfile.Settings, C::String("Run"), bvhSource, conversionError));
        S::SkeletalAnimationClip bvhClip;
        A::SkeletalClipRetargetReport bvhReport;
        RIG_CHECK(
            A::RetargetSkeletalClip(bvhSource, target, parsedProfile, settings, bvhClip, bvhReport, conversionError));
        RIG_CHECK(bvhReport.IgnoredTranslationChannels == 1);

        // 元骨の方向を90度変えるとmatchは拒否。align_bonesは自動Cの角度を報告する。
        auto sideways = input;
        sideways.Joints[1].Rest.Translation = {1, 0, 0};
        const auto savedName = converted.Name;
        RIG_CHECK(!A::RetargetSkeletalClip(sideways, target, parsedProfile, settings, converted, convertedReport,
                                           conversionError));
        RIG_CHECK(conversionError == "rest_direction_mismatch" && converted.Name == savedName);
        settings.RestMode = A::SkeletalRestCorrectionMode::AlignBones;
        RIG_CHECK(A::RetargetSkeletalClip(sideways, target, parsedProfile, settings, converted, convertedReport,
                                          conversionError));
        for (const auto& correction : convertedReport.Corrections)
        {
            RIG_CHECK(correction.BeforeRadians > 1.5 && correction.AfterRadians < 1e-6);
        }
        settings.bAutoRootHeight = true;
        RIG_CHECK(!A::RetargetSkeletalClip(input, target, parsedProfile, settings, converted, convertedReport,
                                           conversionError));
        RIG_CHECK(conversionError == "root_height_requires_pelvis_and_paw");
        write(sourceRoot / "roles.json", profile);
        const F::Text spec =
            R"json({"version":2,"name":"retarget","package_root":"Cooked/Rig","assets":[{"kind":"skeletal","logical_path":"Models/Dog","source_path":"target.gltf","format":"nvskel.v1.skinmesh.pnujiw.u32","package_name":"dog.nvpk","entry_name":"dog"},{"kind":"animation","logical_path":"Animations/Run","source_path":"run.bvh","format":"nvskel.v1.clips","package_name":"run.nvpk","entry_name":"run","skeleton_path":"target.gltf","role_profile":"roles.json","clip_name":"Run"},{"kind":"animation","logical_path":"Animations/Imported","source_path":"motion.gltf","format":"nvskel.v1.clips","package_name":"imported.nvpk","entry_name":"imported","skeleton_path":"target.gltf","role_profile":"roles.json","clip_name":"ImportedWave"}]})json";
        Cook::TextureAssetSetCookRequest request;
        request.SourceRoot = sourceRoot;
        request.SpecPath = f.Root / "retarget.json";
        request.RuntimeRoot = f.Root / "retarget-runtime";
        Cook::CookBatchReport batch;
        request.Report = &batch;
        write(request.SpecPath, spec);
        Cook::CookManagedBootstrapOutcome outcome;
        F::Text error;
        const auto run = [&](Cook::TextureAssetSetCookResult expected)
        {
            const auto result = Cook::CookTextureAssetSetWithOutcome(request, outcome, error);
            if (result != expected)
            {
                std::fprintf(stderr, "retarget expected=%u actual=%u error=%s\n", unsigned(expected), unsigned(result),
                             error.c_str());
            }
            RIG_CHECK(result == expected);
        };
        run(Cook::TextureAssetSetCookResult::Created);
        run(Cook::TextureAssetSetCookResult::NoChange);
        const auto manifest = F::ReadBytes(request.RuntimeRoot / "manifest.json");
        auto assets =
            C::MakeShared<Core::Asset::AssetSystem>(C::AnsiString(request.RuntimeRoot.generic_string().c_str()));
        RIG_CHECK(assets->LoadManifestFromJsonText(C::String(
            F::Text(C::AnsiStringView(reinterpret_cast<const char*>(manifest.data()), manifest.size())).c_str())));
        Core::ResourceIO::RigSplitLoadPlan plan;
        plan.Assets = std::move(assets);
        plan.SkeletonPath = "Models/Dog.skeleton";
        plan.MeshPath = "Models/Dog";
        plan.BankPaths = {"Models/Dog.clips", "Animations/Run", "Animations/Imported"};
        plan.Profile = S::RigImportProfile::StaticRootFrame256;
        plan.Limits.MaxJoints = 256;
        S::CookedRigSplitCpuAsset loaded;
        Core::ResourceIO::RigSplitLoadReport report;
        RIG_CHECK(Core::ResourceIO::LoadRigSplitForWorker(plan, loaded, report));
        RIG_CHECK(loaded.GetData()->Clips.size() == 3);
        const auto& runClip = loaded.GetData()->Clips[1];
        RIG_CHECK(runClip.Name == C::String("Run") && runClip.RootMotion.size() == 31);
        const auto& last = runClip.RootMotion.back();
        RIG_CHECK(std::abs(last.TranslationX - std::sqrt(2.0)) < 1e-5 &&
                  std::abs(last.TranslationZ - std::sqrt(2.0)) < 1e-5);
        RIG_CHECK(std::abs(last.YawRadians - 3.141592653589793 / 6) < 1e-5);
        const auto& imported = loaded.GetData()->Clips[2];
        RIG_CHECK(imported.Name == C::String("ImportedWave") && !imported.RootMotion.empty());
        // glTF作者restが90度でも静止差分は0。絶対world姿勢を差分と誤認しない。
        for (const auto& sample : imported.RootMotion)
        {
            RIG_CHECK(std::abs(sample.YawRadians) < 1e-5);
        }
        write(sourceRoot / "roles.json", F::Replace(profile, "\"output_fps\":30", "\"output_fps\":24"));
        run(Cook::TextureAssetSetCookResult::Updated);
        run(Cook::TextureAssetSetCookResult::NoChange);
        write(sourceRoot / "target.gltf.import.json", R"({"version":1})");
        run(Cook::TextureAssetSetCookResult::Updated);
        auto changed = f.Binary;
        F::Float(changed, 0, .125f);
        f.SetBuffer(changed);
        run(Cook::TextureAssetSetCookResult::Updated);
        f.SetBuffer(f.Binary);
        std::filesystem::remove(sourceRoot / "target.gltf.import.json");
        std::printf("RIG_SPLIT_CASE retarget_bvh_gltf_v1_batch_root_motion_dependencies result=pass\n");
    }
    void ManagedMixedRig(F::Fixture& f)
    {
        f.SetBuffer(f.Binary);
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        F::WriteBytes("RigV1Fixture/rig.gltf", F::Bytes(f.Json.begin(), f.Json.end()));
        const F::Text spec =
            R"json({"version":2,"name":"rig-batch","package_root":"Cooked/Rig","default_variant":"default","emissiveNitsPerUnit":42,"assets":[{"kind":"skeletal","logical_path":"Models/Dog","source_path":"rig.gltf","format":"nvskel.v1.skinmesh.pnujiw.u32","package_name":"dog.nvpk","entry_name":"dog"},{"kind":"animation","logical_path":"Animations/Wave","source_path":"rig.gltf","format":"nvskel.v1.clips","package_name":"wave.nvpk","entry_name":"wave"},{"kind":"raw","logical_path":"Data/config","source_path":"config.bin","format":"raw.v0","package_name":"config.nvpk","entry_name":"config"},{"kind":"texture","logical_path":"Textures/check","source_path":"check.ppm","format":"nvtex.v0.rgba8.linear","package_name":"check.nvpk","entry_name":"check"},{"kind":"audio","logical_path":"Audio/tick","source_path":"tick.wav","format":"nvaud.v0.pcm16","package_name":"tick.nvpk","entry_name":"tick"}]})json";
        F::WriteBytes("RigV1Fixture/config.bin", F::Bytes{'o', 'k'});
        const F::Text ppmHeader = "P6\n2 2\n255\n";
        F::Bytes ppm(ppmHeader.begin(), ppmHeader.end());
        for (size_t i = 0; i < 12; ++i)
        {
            ppm.push_back(uint8_t(i * 19));
        }
        F::WriteBytes("RigV1Fixture/check.ppm", ppm);
        F::Bytes wav{'R', 'I', 'F', 'F'};
        X::U32(wav, 40);
        for (char c : F::Text("WAVEfmt "))
        {
            wav.push_back(uint8_t(c));
        }
        X::U32(wav, 16);
        wav.insert(wav.end(), {1, 0, 1, 0});
        X::U32(wav, 48000);
        X::U32(wav, 96000);
        wav.insert(wav.end(), {2, 0, 16, 0, 'd', 'a', 't', 'a'});
        X::U32(wav, 4);
        wav.insert(wav.end(), {0, 0, 255, 127});
        F::WriteBytes("RigV1Fixture/tick.wav", wav);
        Cook::TextureAssetSetCookRequest request;
        request.SpecPath = f.Root / "batch.json";
        request.SourceRoot = f.Root / "RigV1Fixture";
        request.RuntimeRoot = f.Root / "batch-runtime";
        F::WriteBytes(request.SpecPath, F::Bytes(spec.begin(), spec.end()));
        Cook::CookManagedBootstrapOutcome outcome;
        Cook::CookBatchReport report;
        request.Report = &report;
        F::Text error;
        const auto run = [&](Cook::TextureAssetSetCookResult expected)
        {
            const auto result = Cook::CookTextureAssetSetWithOutcome(request, outcome, error);
            if (result != expected)
            {
                std::fprintf(stderr, "mixed rig expected=%u actual=%u error=%s\n", unsigned(expected), unsigned(result),
                             error.c_str());
            }
            RIG_CHECK(result == expected);
        };
        run(Cook::TextureAssetSetCookResult::Created);
        Cook::SingleAssetCookRequest mismatched;
        mismatched.Kind = "animation";
        mismatched.Format = "nvskel.v1.clips";
        mismatched.LogicalPath = "Animations/Mismatch";
        mismatched.EntryName = "mismatch";
        mismatched.EntryTypeText = "Anm1";
        mismatched.Variant = "default";
        mismatched.InputPath = request.SourceRoot / "rig.gltf";
        mismatched.PackagePath = request.RuntimeRoot / "Cooked/Rig/mismatch.nvpk";
        mismatched.ManifestPath = request.RuntimeRoot / "manifest.json";
        const auto stale = F::Replace(f.Json, "Root", "Ruut");
        C::VariableArray<Cook::RigSplitCookEntry> rejected;
        RIG_CHECK(!Cook::Detail::BuildRigSingleOutputs(mismatched, F::View(stale), false, rejected, error));
        RIG_CHECK(error == "clip_source_snapshot_mismatch" && rejected.empty());
        RIG_CHECK(report.Assets.size() == 5 && report.Assets[0].Metrics.Joints == 2 &&
                  report.Assets[1].Metrics.Joints == 2 && report.Assets[0].Metrics.Triangles == 1);
        const auto manifest = F::ReadBytes(request.RuntimeRoot / "manifest.json");
        Core::Asset::AssetManifest parsed;
        RIG_CHECK(
            parsed.LoadFromJsonText(C::String(
                F::Text(C::AnsiStringView(reinterpret_cast<const char*>(manifest.data()), manifest.size())).c_str())) &&
            parsed.GetReferenceCount() == 7);
        for (size_t i = 0; i < parsed.GetReferenceCount(); ++i)
        {
            const auto& ref = parsed.GetReference(i);
            if (ref.Kind == Core::Asset::AssetKind::Model || ref.Kind == Core::Asset::AssetKind::Skeleton ||
                ref.Kind == Core::Asset::AssetKind::Animation)
            {
                RIG_CHECK(ref.bHasRigSplitMetadata && ref.RigSplitMetadata.Profile == 3);
            }
        }
        const auto serial = request.RuntimeRoot;
        request.RuntimeRoot = f.Root / "batch-parallel";
        request.Jobs = 4;
        run(Cook::TextureAssetSetCookResult::Created);
        RIG_CHECK(F::ReadBytes(request.RuntimeRoot / "manifest.json") == manifest);
        for (size_t i = 0; i < parsed.GetReferenceCount(); ++i)
        {
            const auto path = std::filesystem::path(parsed.GetReference(i).CookedPackage.c_str());
            RIG_CHECK(F::ReadBytes(serial / path) == F::ReadBytes(request.RuntimeRoot / path));
        }
        run(Cook::TextureAssetSetCookResult::NoChange);
        RIG_CHECK(report.Assets[0].bSkipped && report.Assets[1].bSkipped);
        request.bForce = true;
        run(Cook::TextureAssetSetCookResult::Updated);
        RIG_CHECK(!report.Assets[0].bSkipped && !report.Assets[1].bSkipped &&
                  F::ReadBytes(request.RuntimeRoot / "manifest.json") == manifest);
        request.bForce = false;
        auto binary = f.Binary;
        binary.push_back(17);
        f.SetBuffer(binary);
        run(Cook::TextureAssetSetCookResult::Updated);
        RIG_CHECK(!report.Assets[0].bSkipped && !report.Assets[1].bSkipped && report.Assets[2].bSkipped &&
                  report.Assets[3].bSkipped && report.Assets[4].bSkipped);
        X::Sidecar(R"({"version":1,"units":{"scale":2}})");
        run(Cook::TextureAssetSetCookResult::Updated);
        run(Cook::TextureAssetSetCookResult::NoChange);
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        // 既存assetに内包画像の派生出力が加わっても、同じcontrollerでinventoryを更新する。
        const auto png = X::Png(2, 2, F::Bytes(16, 255));
        F::WriteBytes("RigV1Fixture/batch-base.png", png);
        auto textured = X::AddMaterial(
            f.Json,
            R"({"name":"Body","emissiveFactor":[1,0,0],"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})");
        textured = F::Replace(textured, "\"skins\":",
                              "\"images\":[{\"uri\":\"batch-base.png\"}],\"textures\":[{\"source\":0}],\"skins\":");
        F::WriteBytes("RigV1Fixture/rig.gltf", F::Bytes(textured.begin(), textured.end()));
        run(Cook::TextureAssetSetCookResult::Updated);
        RIG_CHECK(report.Assets[0].Metrics.TextureBytes == 20);
        run(Cook::TextureAssetSetCookResult::NoChange);
        const auto imagePackage = request.RuntimeRoot / "Cooked/Rig/dog.nvpk.img0.nvpkg";
        const auto imageBytes = F::ReadBytes(imagePackage);
        F::WriteBytes("RigV1Fixture/rig.gltf", F::Bytes(f.Json.begin(), f.Json.end()));
        run(Cook::TextureAssetSetCookResult::Updated);
        RIG_CHECK(report.Assets[0].Metrics.TextureBytes == 0 && F::ReadBytes(imagePackage) == imageBytes);
        run(Cook::TextureAssetSetCookResult::NoChange);
        f.SetBuffer(f.Binary);
    }
    void FilePublication(F::Fixture& f)
    {
        f.SetBuffer(f.Binary);
        auto request = X::Request();
        request.Profile = S::RigImportProfile::StaticRootFrame256;
        request.Limits.MaxJoints = 256;
        request.bAnalyzeClips = true;
        request.ClipRootJoint = "Root";
        request.ImportOptions.bDisabled = true;
        const F::Bytes json(f.Json.begin(), f.Json.end());
        F::WriteBytes(request.SourcePath, json);
        Cook::RigSplitFileCookResult result;
        F::Text error;
        const auto directory = f.Root / std::filesystem::path(L"\u51fa\u529b\u89aa") / "split-output";
        RIG_CHECK(Cook::CookRigSplitFile(request, directory.parent_path() / "unused" / ".." / "split-output" / "",
                                         result, error));
        RIG_CHECK(result.JointCount == 2 && result.ClipCount == 1 && result.TextureCount == 0);
        const auto manifest = F::ReadBytes(directory / "manifest.json");
        RIG_CHECK(manifest == F::Bytes(result.ManifestJson.begin(), result.ManifestJson.end()));
        const auto hash = result.SourceHash;
        RIG_CHECK(!Cook::CookRigSplitFile(request, directory, result, error) && result.SourceHash == hash);
        RIG_CHECK(F::ReadBytes(directory / "manifest.json") == manifest);
        request.ClipRootJoint = "missing";
        RIG_CHECK(!Cook::CookRigSplitFile(request, f.Root / "invalid-root", result, error));
        RIG_CHECK(!std::filesystem::exists(f.Root / "invalid-root"));
        request.ClipRootJoint = "Root";
        F::Bytes pixels(16, 255);
        const auto png = X::Png(2, 2, pixels);
        F::WriteBytes("RigV1Fixture/base.png", png);
        auto textured =
            X::AddMaterial(f.Json, R"({"name":"Body","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})");
        textured = F::Replace(
            textured, "\"skins\":", "\"images\":[{\"uri\":\"base.png\"}],\"textures\":[{\"source\":0}],\"skins\":");
        F::WriteBytes(request.SourcePath, F::Bytes(textured.begin(), textured.end()));
        RIG_CHECK(Cook::CookRigSplitFile(request, f.Root / "textured-output", result, error));
        RIG_CHECK(result.TextureCount == 1);
        Core::Asset::AssetManifest parsed;
        RIG_CHECK(parsed.LoadFromJsonText(Core::Container::String(result.ManifestJson.c_str())) &&
                  parsed.GetReferenceCount() == 4);
    }
    void CookAndSettings(F::Fixture& f)
    {
        auto first = X::CookSource(f.Json), again = X::CookSource(f.Json);
        RIG_CHECK(first.Skeleton.Payload == again.Skeleton.Payload && first.Mesh.Package == again.Mesh.Package &&
                  first.ManifestJson == again.ManifestJson);
        RIG_CHECK(first.Skeleton.Reference.SourceHash == first.Mesh.Reference.SourceHash &&
                  first.Mesh.Reference.SourceHash == first.Bank.Reference.SourceHash);
        RIG_CHECK(first.SlotSourceMaterials.size() == 1 && first.SlotSourceMaterials[0] == UINT64_MAX &&
                  !first.bMaterialsRenderStaged);
        X::Sidecar(R"({"version":1,"units":{"scale":2},"material":{"profile":"ai_generated"}})");
        auto scaled = X::CookSource(f.Json);
        RIG_CHECK(scaled.SourceHash != first.SourceHash && scaled.Import.bPresent &&
                  !scaled.Import.RawSourceBytes.empty());
        S::SkinMeshV1 mesh;
        S::SkeletonV1 skeleton;
        S::RigV1Report report;
        RIG_CHECK(S::ParseSkinMeshV1(F::View(scaled.Mesh.Payload), mesh, report) &&
                  S::ParseSkeletonV1(F::View(scaled.Skeleton.Payload), skeleton, report));
        RIG_CHECK(skeleton.GetData()->CurrentRest.Rest[0].Translation.Y == 2 &&
                  mesh.GetData()->Vertices[2].Position.Y == 2);
        RIG_CHECK(mesh.GetData()->Materials[0].Record.Metallic == 0 &&
                  mesh.GetData()->Materials[0].Record.OcclusionStrength == 1);
        const auto stable = scaled.SourceHash;
        F::Text error;
        X::Sidecar(R"({"version":1,"materials":[{"name":"Missing","doubleSided":"force_true"}]})");
        RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(f.Json), X::Request(), scaled, report, error) &&
                  scaled.SourceHash == stable);
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        auto tail = f.Binary;
        tail.push_back(42);
        f.SetBuffer(tail);
        auto tailed = X::CookSource(f.Json);
        RIG_CHECK(tailed.SourceHash != first.SourceHash && tailed.Skeleton.Payload == first.Skeleton.Payload &&
                  tailed.Mesh.Payload == first.Mesh.Payload);
        f.SetBuffer(f.Binary);
        auto emissive = X::AddMaterial(f.Json, R"({"name":"Glow","emissiveFactor":[1,0,0]})");
        auto request = X::Request();
        RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(emissive), request, scaled, report, error) &&
                  error.find(C::AnsiStringView("emissiveNitsPerUnit")) != F::Text::npos);
        request.AssetSetEmission = {true, 100};
        auto glow = X::CookSource(emissive, request);
        RIG_CHECK(S::ParseSkinMeshV1(F::View(glow.Mesh.Payload), mesh, report) &&
                  mesh.GetData()->Materials[0].Record.EmissiveNits > 0);
        std::printf("RIG_SPLIT_CASE cook_settings result=pass\n");
    }
    void SlotMapping(F::Fixture& f)
    {
        const F::Text primitive =
            R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":5},"indices":8,"mode":4})";
        const auto p1 = F::Replace(primitive, "\"mode\":4", "\"mode\":4,\"material\":1");
        const auto p0 = F::Replace(primitive, "\"mode\":4", "\"mode\":4,\"material\":0");
        auto json = F::Replace(f.Json, primitive.c_str(), p1 + "," + p0 + "," + primitive);
        json = F::Replace(
            json, "\"skins\":",
            R"("materials":[{"name":"One","alphaMode":"MASK","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}},{"name":"Two","alphaMode":"BLEND","pbrMetallicRoughness":{"baseColorFactor":[0,1,0,1]}}],"skins":)");
        const auto cooked = X::CookSource(json);
        RIG_CHECK(cooked.SlotSourceMaterials.size() == 3 && cooked.SlotSourceMaterials[0] == 1 &&
                  cooked.SlotSourceMaterials[1] == 0 && cooked.SlotSourceMaterials[2] == UINT64_MAX);
        S::SkinMeshV1 mesh;
        S::RigV1Report report;
        RIG_CHECK(S::ParseSkinMeshV1(F::View(cooked.Mesh.Payload), mesh, report));
        const auto* d = mesh.GetData();
        RIG_CHECK(d->Slots[0].Name == "Two" && d->Slots[1].Name == "One" && d->Materials[0].Record.BaseColor[1] == 1 &&
                  d->Materials[1].Record.BaseColor[0] == 1);
        RIG_CHECK((d->Materials[0].Record.Flags & 6) == 4 && (d->Materials[1].Record.Flags & 6) == 2 &&
                  d->SubMeshes.size() == 3);
        auto duplicate = F::Replace(json, "\"name\":\"Two\"", "\"name\":\"One\"");
        auto warned = X::CookSource(duplicate);
        RIG_CHECK(warned.DuplicateMaterialNameGroups == 1 && !warned.Warnings.empty());
        X::Sidecar(
            R"({"version":1,"materials":[{"name":"One","doubleSided":"force_true"},{"index":0,"doubleSided":"force_false"}]})");
        Cook::RigSplitCookResult held = cooked;
        F::Text error;
        RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(json), X::Request(), held, report, error) &&
                  held.SourceHash == cooked.SourceHash);
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        std::printf("RIG_SPLIT_CASE source_slot_mapping result=pass\n");
    }
    void CapturedInputs(F::Fixture& f)
    {
        namespace I = NorvesLib::Core::AssetImport;
        X::Sidecar(R"({"version":1,"units":{"scale":2},"material":{"profile":"ai_generated"}})");
        I::LoadedImportSettingsDocument loaded;
        RIG_CHECK(I::LoadImportSettingsDocument("RigV1Fixture/rig.gltf", {}, loaded).Result ==
                  I::SettingsFileResult::Success);
        I::LoadedImportSettings geometry;
        geometry.Settings = loaded.Settings.Geometry;
        geometry.bPresent = loaded.bPresent;
        geometry.Path = loaded.Path;
        S::RigAuthoringCpu rig;
        S::RigGltfImportCapture capture;
        S::RigV1Report report;
        RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(f.Json), "RigV1Fixture/rig.gltf", rig, report, {}, &geometry,
                                                  nullptr, &capture));
        X::Sidecar("broken after first read");
        auto modified = f.Binary;
        modified.push_back(19);
        f.SetBuffer(modified);
        const auto root = capture.Document.GetRoot();
        I::SourceMaterialCatalog catalog;
        I::ResolvedMaterialImportPlan resolved;
        RIG_CHECK(I::ReadSourceMaterialCatalog(root, catalog) == I::SettingsResult::Success &&
                  I::ResolveMaterialImportPlan(catalog, loaded.Settings, {}, resolved).Succeeded());
        Cook::LoadedMeshMaterialV1Input input;
        input.Import = &loaded;
        input.Catalog = &catalog;
        input.Resolved = &resolved;
        Cook::MeshMaterialV1Plan material;
        F::Text error;
        RIG_CHECK(Cook::PrepareMeshMaterialV1(root, capture.Buffers, "RigV1Fixture/rig.gltf", "Models/Mesh.nvskel",
                                              false, 0, 0, nullptr, material, error, &input));
        RIG_CHECK(material.Material.Metallic == 0 && rig.GetData()->LocalRest[1].Translation.Y == 2 &&
                  capture.SlotSourceMaterialIndices[0] == UINT64_MAX &&
                  capture.Buffers.GetSourceBytes(0).size() == 416);
        auto held = rig.GetData();
        const auto oldCount = capture.Buffers.GetCount();
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(F::Text("bad")), "RigV1Fixture/rig.gltf", rig, report, {},
                                                   &geometry, nullptr, &capture) &&
                  rig.GetData() == held && capture.Buffers.GetCount() == oldCount);
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        f.SetBuffer(f.Binary);
        std::printf("RIG_SPLIT_CASE same_read_capture result=pass\n");
    }
    void MixedBudget(F::Fixture& f)
    {
        auto bytes = f.Binary;
        bytes.resize(656, 0);
        constexpr float scale[] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0};
        for (size_t i = 0; i < 18; ++i)
        {
            F::Float(bytes, 584 + i * 4, scale[i]);
        }
        f.SetBuffer(bytes);
        auto json = F::BaseJson("CubicChannels.gltf");
        json = F::Replace(json, R"({"bufferView":13,"componentType":5126,"count":6,"type":"VEC3"})",
                          R"({"bufferView":11,"componentType":5126,"count":2,"type":"VEC3"})");
        json = F::Replace(json, R"({"bufferView":14,"componentType":5126,"count":6,"type":"VEC4"})",
                          R"({"bufferView":12,"componentType":5126,"count":2,"type":"VEC4"})");
        json = F::Replace(json, R"({"input":10,"output":11,"interpolation":"CUBICSPLINE"})",
                          R"({"input":10,"output":11,"interpolation":"LINEAR"})");
        json = F::Replace(json, R"({"input":10,"output":12,"interpolation":"CUBICSPLINE"})",
                          R"({"input":10,"output":12,"interpolation":"STEP"})");
        const char* original =
            R"("channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},{"sampler":1,"target":{"node":0,"path":"rotation"}},{"sampler":2,"target":{"node":1,"path":"scale"}}])";
        const char* reordered =
            R"("channels":[{"sampler":2,"target":{"node":1,"path":"scale"}},{"sampler":0,"target":{"node":1,"path":"translation"}},{"sampler":1,"target":{"node":0,"path":"rotation"}}])";
        S::SkeletalGltfDecodeOptions options;
        options.CubicSplinePolicy = S::SkeletalCubicSplinePolicy::Bake;
        for (int order = 0; order < 2; ++order)
        {
            const auto text = order ? F::Replace(json, original, reordered) : json;
            S::RigAuthoringCpu good;
            S::RigV1Report report;
            RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(text), "RigV1Fixture/rig.gltf", good, report, {}, nullptr,
                                                      &options));
            size_t total = 0;
            for (const auto& channel : good.GetData()->Geometry.Clips[0].Channels)
            {
                total += channel.Samples.size();
            }
            RIG_CHECK(total > 6);
            const auto* stable = good.GetData();
            S::RigV1Limits low;
            low.MaxSamples = uint32_t(total - 1);
            RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(text), "RigV1Fixture/rig.gltf", good, report, low,
                                                       nullptr, &options) &&
                      good.GetData() == stable);
            low.MaxSamples = uint32_t(total);
            RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(text), "RigV1Fixture/rig.gltf", good, report, low,
                                                      nullptr, &options));
        }
        f.SetBuffer(f.Binary);
        std::printf("RIG_SPLIT_CASE mixed_linear_step_bake_budget result=pass\n");
    }
    F::Text Number(uint64_t value)
    {
        char text[32]{};
        std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
        return F::Text(text);
    }
    F::Text TwoMaterials(const F::Text& base, const F::Text& first, const F::Text& second)
    {
        const F::Text primitive =
            R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":5},"indices":8,"mode":4})";
        const auto a = F::Replace(primitive, "\"mode\":4", "\"mode\":4,\"material\":0");
        const auto b = F::Replace(primitive, "\"mode\":4", "\"mode\":4,\"material\":1");
        auto json = F::Replace(base, primitive.c_str(), a + "," + b);
        return F::Replace(json, "\"skins\":", F::Text("\"materials\":[") + first + "," + second + "],\"skins\":");
    }
    void NameAndPackageLimits(F::Fixture& f)
    {
        namespace P = NorvesLib::Tests::RigSplitWireFixture;
        auto request = X::Request();
        request.Limits.MaxNameBytes = 32;
        const auto named = [](const F::Text& name) { return F::Text("{\"name\":\"") + name + "\"}"; };
        for (int unicode = 0; unicode < 2; ++unicode)
        {
            const auto name = unicode ? F::Text(28, 'a') + "\\ud83d\\udc3a" : F::Text(32, 'a');
            auto good = X::CookSource(X::AddMaterial(f.Json, named(name)), request);
            S::SkinMeshV1 mesh;
            S::RigV1Report report;
            F::Text error;
            RIG_CHECK(S::ParseSkinMeshV1(F::View(good.Mesh.Payload), mesh, report) &&
                      mesh.GetData()->Slots[0].Name.size() == 32);
            P::AllocationCounts counts;
            {
                P::ObserveAllocations observe(counts);
                RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(X::AddMaterial(f.Json, named(name + "a"))), request,
                                                          good, report, error));
            }
            RIG_CHECK(report.Status == S::RigV1Status::LimitExceeded && counts.Base == 0 && counts.Slot == 0);
        }
        auto exact = TwoMaterials(f.Json, named(F::Text(28, 'x')), named(F::Text(28, 'x')));
        auto good = X::CookSource(exact, request);
        auto over = TwoMaterials(f.Json, named(F::Text(29, 'x')), named(F::Text(29, 'x')));
        S::RigV1Report report;
        F::Text error;
        P::AllocationCounts counts;
        {
            P::ObserveAllocations observe(counts);
            RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(over), request, good, report, error));
        }
        RIG_CHECK(report.Status == S::RigV1Status::LimitExceeded && counts.Slot == 0);
        request = X::Request();
        request.PackageDirectory = F::Text(4055, 'p');
        good = X::CookSource(f.Json, request);
        RIG_CHECK(good.Mesh.Reference.CookedPackage.size() == 4096 &&
                  S::IsSplitLogicalPath(good.Mesh.Reference.CookedPackage));
        const auto saved = good.ManifestJson;
        request.PackageDirectory.push_back('p');
        RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(f.Json), request, good, report, error) &&
                  report.Status != S::RigV1Status::Success && good.ManifestJson == saved);
        std::printf("RIG_SPLIT_CASE material_names_and_complete_package_path_budget result=pass\n");
    }
    void ImageLocatorAndBudgets(F::Fixture& f)
    {
        namespace P = NorvesLib::Tests::RigSplitWireFixture;
        const F::Bytes pixelsA{255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
        const F::Bytes pixelsB{0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
        auto fileA = X::Png(2, 2, pixelsA);
        const auto fileB = X::Png(2, 2, pixelsB);
        fileA.resize(1024, 0);
        fileA.insert(fileA.end(), f.Binary.begin(), f.Binary.end());
        F::WriteBytes("RigV1Fixture/A.png", fileA);
        F::WriteBytes("RigV1Fixture/B.png", fileB);
        NorvesLib::Core::JsonDocument original;
        RIG_CHECK(NorvesLib::Core::JsonDocument::TryParseUtf8(F::View(f.Json), original));
        auto json = F::Replace(f.Json, "\"uri\":\"fixture.bin\"", "\"uri\":\"A.png\"");
        json = F::Replace(json, "\"byteLength\":416", "\"byteLength\":1440");
        const auto views = original.GetRoot().FindMember("bufferViews");
        for (size_t i = 0; i < views.GetArraySize(); ++i)
        {
            const auto offset = views.GetArrayElement(i).FindMember("byteOffset").AsUInt32();
            const auto needle = F::Text("\"byteOffset\":") + Number(offset) + ",";
            json = F::Replace(json, needle.c_str(), F::Text("\"byteOffset\":") + Number(offset + 1024) + ",");
        }
        json = F::Replace(json, "\"skins\":", R"("images":[{"uri":"B.png"},{"uri":"A.png"}],"skins":)");
        S::RigAuthoringCpu rig;
        S::RigGltfImportCapture capture;
        S::RigV1Report report;
        RIG_CHECK(S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/rig.gltf", rig, report, {}, nullptr,
                                                  nullptr, &capture));
        RIG_CHECK(capture.SourceCanonicalFiles.size() == 1 &&
                  capture.SourceCanonicalFiles[0] == std::filesystem::weakly_canonical("RigV1Fixture/A.png"));
        // 取得後のURI解決先A→Bを、OS symlink権限に依存しないdocument probeで再現する。
        const auto lookup = F::Replace(json, "\"uri\":\"A.png\"", "\"uri\":\"B.png\"");
        RIG_CHECK(NorvesLib::Core::JsonDocument::TryParseUtf8(F::View(lookup), capture.Document));
        Cook::RigSplitImageInputs input;
        input.Capture = &capture;
        input.SourcePath = "RigV1Fixture/rig.gltf";
        Cook::MeshEmbeddedImage encoded;
        Cook::DecodedTextureRgba8 decoded;
        NorvesLib::Core::Gltf::DataUriMime mime;
        F::Text error;
        RIG_CHECK(input.Read(0, encoded, decoded, mime, error) && decoded.Pixels == pixelsB);
        std::filesystem::remove("RigV1Fixture/A.png");
        RIG_CHECK(input.Read(1, encoded, decoded, mime, error) &&
                  decoded.Pixels == pixelsA); // 保存locator Aの同readを再利用。
        uint64_t textureBytes = 0;
        RIG_CHECK(Cook::MeasureRigSplitTextureBytes(2, 2, {}, textureBytes));
        for (int kind = 0; kind < 3; ++kind)
        {
            Cook::RigSplitImageInputs limited;
            limited.Capture = &capture;
            limited.SourcePath = input.SourcePath;
            limited.Limits.MaxTotalOutputBytes = kind == 0 ? 1 : kind == 1 ? textureBytes * 2 - 1 : textureBytes * 2;
            P::AllocationCounts counts;
            P::ObserveAllocations observe(counts);
            if (kind == 0)
            {
                RIG_CHECK(!limited.Read(0, encoded, decoded, mime, error) && counts.Pixels == 0 &&
                          counts.ImageCopies == 0);
            }
            else
            {
                RIG_CHECK(limited.Read(0, encoded, decoded, mime, error) && counts.Pixels == 1);
                const auto copies = counts.ImageCopies;
                const bool ok = limited.Read(1, encoded, decoded, mime, error);
                RIG_CHECK(ok == (kind == 2));
                if (kind == 1)
                {
                    RIG_CHECK(counts.Pixels == 1 && counts.ImageCopies == copies);
                }
                else
                {
                    RIG_CHECK(counts.Pixels == 2 && limited.OutputBytes == textureBytes * 2);
                }
            }
        }
        std::printf("RIG_SPLIT_CASE acquired_locator_and_predecode_cumulative_output result=pass\n");
    }
    void DerivedCopies(F::Fixture& f)
    {
        namespace P = NorvesLib::Tests::RigSplitWireFixture;
        const F::Bytes pixels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        const auto png = X::Png(2, 2, pixels);
        F::WriteBytes("RigV1Fixture/mr.png", png);
        const F::Text material = R"({"pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0}}})";
        auto json = TwoMaterials(f.Json, material, material);
        json = F::Replace(json, "\"skins\":", R"("textures":[{"source":0}],"images":[{"uri":"mr.png"}],"skins":)");
        X::Sidecar(R"({"version":1,"material":{"arm":{"roughness":"texture","metallic":"texture"}}})");
        uint64_t textureBytes = 0;
        RIG_CHECK(Cook::MeasureRigSplitTextureBytes(2, 2, {}, textureBytes));
        auto request = X::Request();
        request.ImageLimits.MaxTotalDecodedBytes = 48;
        // cache encoded一回、二材質の返却encoded/decoded、二組のscratch/raw所有copy。
        const uint64_t copies = png.size() * 3 + 16 * 2 + 16 * 4;
        request.ImageLimits.MaxTotalEncodedBytes = copies - 48;
        request.ImageLimits.MaxTotalOutputBytes = textureBytes * 3;
        auto exact = X::CookSource(json, request);
        RIG_CHECK(exact.TexturePlans.size() == 2 && exact.TexturePlans[0].Payload == Cook::MeshImagePayload::RawRgba8);
        S::RigV1Report report;
        F::Text error;
        for (int kind = 0; kind < 2; ++kind)
        {
            auto low = request;
            if (kind == 0)
            {
                --low.ImageLimits.MaxTotalEncodedBytes;
            }
            else
            {
                --low.ImageLimits.MaxTotalOutputBytes;
            }
            P::AllocationCounts counts;
            {
                P::ObserveAllocations observe(counts);
                RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(json), low, exact, report, error) &&
                          report.Status != S::RigV1Status::Success);
            }
            RIG_CHECK(counts.Arm == 1);
        }
        std::filesystem::remove("RigV1Fixture/rig.gltf.import.json");
        std::printf("RIG_SPLIT_CASE derived_arm_double_copy_budget result=pass\n");
    }
    void Images(F::Fixture& f)
    {
        const F::Bytes pixels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        const auto png = X::Png(2, 2, pixels);
        Cook::DecodedTextureRgba8 decoded;
        F::Text error;
        Cook::RigSplitImageLimits limits;
        RIG_CHECK(Cook::DecodeRigSplitImage(F::View(png), limits, decoded, error) && decoded.Pixels == pixels);
        auto low = limits;
        low.MaxDecoderWorkspaceBytes = 1;
        RIG_CHECK(!Cook::DecodeRigSplitImage(F::View(png), low, decoded, error) && decoded.Pixels == pixels &&
                  error == "split_image_workspace_limit");
        low = limits;
        low.MaxDecodedBytes = 15;
        RIG_CHECK(!Cook::DecodeRigSplitImage(F::View(png), low, decoded, error) && decoded.Pixels == pixels);
        low = limits;
        low.MaxDimension = 1;
        RIG_CHECK(!Cook::DecodeRigSplitImage(F::View(png), low, decoded, error));
        auto json =
            X::AddMaterial(f.Json, R"({"name":"Body","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})");
        json = F::Replace(json, "\"skins\":", R"("textures":[{"source":0}],"images":[{"uri":"base.png"}],"skins":)");
        F::WriteBytes("RigV1Fixture/base.png", png);
        auto cooked = X::CookSource(json);
        RIG_CHECK(cooked.TexturePlans.size() == 1 && !cooked.TexturePlans[0].IsBorrowed() &&
                  cooked.TexturePlans[0].GetBytes().size() == png.size());
        auto request = X::Request();
        request.ImageLimits.MaxEncodedBytes = png.size() - 1;
        S::RigV1Report report;
        const auto stable = cooked.SourceHash;
        RIG_CHECK(!Cook::CookRigSplitV1NativePath(F::View(json), request, cooked, report, error) &&
                  cooked.SourceHash == stable);
        std::filesystem::remove("RigV1Fixture/base.png");
        RIG_CHECK(std::equal(cooked.TexturePlans[0].GetBytes().begin(), cooked.TexturePlans[0].GetBytes().end(),
                             png.begin()));
        std::printf("RIG_SPLIT_CASE bounded_image_ownership result=pass\n");
    }
} // namespace
int main()
{
    F::Fixture fixture;
    CookAndSettings(fixture);
    SlotMapping(fixture);
    CapturedInputs(fixture);
    MixedBudget(fixture);
    Images(fixture);
    NameAndPackageLimits(fixture);
    ImageLocatorAndBudgets(fixture);
    DerivedCopies(fixture);
    ManagedMixedRig(fixture);
    RetargetBatch(fixture);
    FilePublication(fixture);
    std::printf(
        "RIG_SPLIT_COOK result=pass same_read_settings_buffers_source_slot_mapping_full_mats_three_packages_no_cli_publish\n");
    return 0;
}
