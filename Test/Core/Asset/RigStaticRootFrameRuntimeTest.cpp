// profile2を実package/worker/owner公開へ接続し、直接Sampleの束縛証明も検査する。
#include "RigStaticRootFrameFixture.h"
#include "Resource/SkeletalAssetRuntimeTestAccess.h"
#include "Animation/SkeletalAssetRuntime.h"
#include "Thread/JobSystem.h"
#include <process.h>
#include <windows.h>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace H = NorvesLib::Tests::RigStaticRootFrameFixture;
namespace X = NorvesLib::Tests::RigSplitFixture;
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace R = Core::ResourceIO;
namespace T = NorvesLib::Thread;
namespace A = Core::Animation;
using Status = Core::SkeletalRuntimeStatus;
using Failure = Core::SkeletalRuntimeFailure;
using Access = R::Detail::SkeletalRuntimeTestAccess;
namespace
{
    void JsonNumber(F::Text& out, double value)
    {
        char b[48];
        std::snprintf(b, sizeof(b), "%.9g", value);
        out += b;
    }
    void MatrixJson(F::Text& out, const C::VariableArray<NorvesLib::Math::Matrix4x4>& values)
    {
        out += "[";
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i)
            {
                out += ",";
            }
            out += "[";
            for (size_t k = 0; k < 16; ++k)
            {
                if (k)
                {
                    out += ",";
                }
                JsonNumber(out, values[i].values[k]);
            }
            out += "]";
        }
        out += "]";
    }
    void SavePose(const C::TSharedPtr<Core::SkeletalAssetResource>& asset, const A::SkeletalPoseSnapshot& pose,
                  bool rawRejected)
    {
        if (const char* directory = std::getenv("NORVES_ROOT_FRAME_V2_OUTPUT"))
        {
            const auto& diagnostics = R::RigSplitAssetAccess::Get(*asset)->GetDiagnostics();
            F::Text json = "{\"profile\":2,\"clips\":";
            JsonNumber(json, double(asset->GetClipCount()));
            json += ",\"palettes\":";
            MatrixJson(json, pose.BonePalette);
            json += ",\"models\":";
            MatrixJson(json, pose.JointModelMatrices);
            const auto v =
                A::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
            json += ",\"vertex\":[";
            JsonNumber(json, v.Position.x);
            json += ",";
            JsonNumber(json, v.Position.y);
            json += ",";
            JsonNumber(json, v.Position.z);
            json += "]";
            json += ",\"frame_comparison_complete\":";
            json += diagnostics->Load.BindingReport.Banks[0].bFrameComparisonComplete ? "true" : "false";
            json += ",\"no_render_lease\":";
            json += asset->GetMesh()->GetRenderAssetLease() ? "false" : "true";
            json += ",\"manual_clip_rejected\":";
            json += rawRejected ? "true" : "false";
            json += "}";
            F::Bytes bytes;
            bytes.insert(bytes.end(), json.begin(), json.end());
            std::filesystem::create_directories(directory);
            F::WriteBytes(std::filesystem::path(directory) / "root-frame-pose.json", bytes);
        }
    }
    bool Sample(const Core::SkeletonResource& skeleton, const Core::AnimationClipResource& clip,
                const Core::SkinnedMeshResource& mesh, A::SkeletalPoseSnapshot& pose)
    {
        return A::SkeletalAnimationSampler::Sample(skeleton, clip, mesh, 1, F::MeshMatrix(mesh), pose);
    }
    void Proof(F::Fixture& f)
    {
        const auto cooked = H::Cook(H::Armature(f.Json));
        X::WritePackages(cooked);
        auto plan = H::Plan(f, cooked.ManifestJson);
        S::CookedRigSplitCpuAsset cpu;
        R::RigSplitLoadReport loaded;
        RIG_CHECK(R::LoadRigSplitForWorker(plan, cpu, loaded));
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        const R::SkeletalAssetCreateContext context{&registry, T::Thread::GetCurrentThreadId()};
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        S::RigSplitReport report;
        RIG_CHECK(S::AssembleRigSplitV1(cpu, context, asset, report));
        H::Pose(asset);
        const auto original = asset->GetClip(0)->GetClip();
        auto raw = registry.CreateResource<Core::AnimationClipResource>(_T("raw"));
        raw->SetClip(S::SkeletalAnimationClip(original));
        RIG_CHECK(raw->Load());
        A::SkeletalPoseSnapshot pose;
        RIG_CHECK(!Sample(*asset->GetSkeleton(), *raw, *asset->GetMesh(), pose) && pose.BonePalette.empty());
        asset->GetClip(0)->SetClip(S::SkeletalAnimationClip(original));
        RIG_CHECK(!Sample(*asset->GetSkeleton(), *asset->GetClip(0), *asset->GetMesh(), pose) &&
                  pose.BonePalette.empty());
        asset->GetClip(0)->Unload();
        asset->GetClip(0)->SetClip(S::SkeletalAnimationClip(original));
        RIG_CHECK(asset->GetClip(0)->Load());
        RIG_CHECK(!Sample(*asset->GetSkeleton(), *asset->GetClip(0), *asset->GetMesh(), pose));
        RIG_CHECK(S::AssembleRigSplitV1(cpu, context, asset, report));
        H::Pose(asset);
        S::SkeletonV1 separatelyParsed;
        S::RigV1Report parsed;
        RIG_CHECK(S::ParseSkeletonV1(cooked.Skeleton.Payload, separatelyParsed, parsed, {}, H::Profile));
        auto other = registry.CreateResource<Core::SkeletonResource>(_T("other"));
        RIG_CHECK(other->SetSplitSkeleton(separatelyParsed) && other->Load());
        RIG_CHECK(!Sample(*other, *asset->GetClip(0), *asset->GetMesh(), pose) && pose.BonePalette.empty());
        auto alias = registry.CreateResource<Core::SkeletonResource>(_T("alias"));
        RIG_CHECK(alias->SetSplitSkeleton(cpu.GetData()->Skeleton) && alias->Load());
        RIG_CHECK(Sample(*alias, *asset->GetClip(0), *asset->GetMesh(), pose));
        S::CookedRigSplitCpuAsset rebound;
        RIG_CHECK(S::BindRigSplitV1(separatelyParsed, cpu.GetData()->Mesh, cpu.GetData()->Banks, {}, rebound, report,
                                    {}, H::Profile));
        RIG_CHECK(S::AssembleRigSplitV1(rebound, context, asset, report));
        H::Pose(asset);
        const auto legacy = X::CookSource(f.Json);
        X::WritePackages(legacy);
        auto oldPlan = X::LoadPlan(f, legacy.ManifestJson);
        RIG_CHECK(R::LoadRigSplitForWorker(oldPlan, cpu, loaded) && S::AssembleRigSplitV1(cpu, context, asset, report));
        asset->GetClip(0)->SetClip(S::SkeletalAnimationClip(original));
        RIG_CHECK(Sample(*asset->GetSkeleton(), *asset->GetClip(0), *asset->GetMesh(), pose));
        Core::AssetImport::LoadedImportSettings settings;
        settings.bPresent = true;
        settings.Settings.Scale = 2;
        const auto rig = H::Import(H::Armature(f.Json), &settings);
        S::SkeletonV1 sk;
        S::SkinMeshV1 mesh;
        S::ClipBankV1 bank;
        RIG_CHECK(S::BuildSkeletonV1(rig, sk, parsed, {}, H::Profile));
        RIG_CHECK(S::BuildSkinMeshV1(rig, sk, "Models/Rig.nvskel", cpu.GetData()->Mesh.GetData()->Materials, mesh,
                                     parsed, {}, H::Profile));
        RIG_CHECK(S::BuildClipBankV1({&rig, 1}, bank, parsed, {}, H::Profile) &&
                  S::BindRigSplitV1(sk, mesh, {&bank, 1}, {}, cpu, report, {}, H::Profile));
        RIG_CHECK(S::AssembleRigSplitV1(cpu, context, asset, report));
        H::Pose(asset, 2);
        std::printf(
            "ROOT_FRAME_RUNTIME_CASE result=pass bound_proof_raw_setter_unload_separate_target_rebind_legacy_scale2_pose\n");
    }
    void GeneralRootTrs(F::Fixture& f)
    {
        // 非対角の祖先Gと実際に変化するroot T/R/Sを、独立な行列リテラルで照合する。
        auto json = H::Armature(f.Json);
        json = F::Replace(json, "\"rotation\":[0,0,1,0]", "\"rotation\":[0,0,1,1]");
        json = F::Replace(json, "\"rotation\":[1,0,0,0]", "\"rotation\":[1,0,0,1]");
        json = F::AllTranslation(json);
        json = F::Replace(json, "\"byteLength\":416", "\"byteLength\":440");
        json = F::Replace(
            json, "{\"buffer\":0,\"byteOffset\":384,\"byteLength\":32}",
            "{\"buffer\":0,\"byteOffset\":384,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":416,\"byteLength\":24}");
        json = F::Replace(
            json, "{\"bufferView\":12,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}",
            "{\"bufferView\":12,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},{\"bufferView\":13,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}");
        json = F::Replace(
            json, "{\"input\":10,\"output\":12,\"interpolation\":\"STEP\"}",
            "{\"input\":10,\"output\":12,\"interpolation\":\"STEP\"},{\"input\":10,\"output\":13,\"interpolation\":\"LINEAR\"}");
        json = F::Replace(json, "\"channels\":[",
                          "\"channels\":[{\"sampler\":2,\"target\":{\"node\":0,\"path\":\"scale\"}},");
        auto buffer = f.Binary;
        buffer.resize(440, 0);
        const float scales[] = {1, 1, 1, 2, 3, 4};
        for (size_t i = 0; i < 6; ++i)
        {
            F::Float(buffer, 416 + i * 4, scales[i]);
        }
        f.SetBuffer(buffer);
        const auto cooked = H::Cook(json);
        X::WritePackages(cooked);
        const auto plan = H::Plan(f, cooked.ManifestJson);
        S::CookedRigSplitCpuAsset cpu;
        R::RigSplitLoadReport load;
        RIG_CHECK(R::LoadRigSplitForWorker(plan, cpu, load));
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        S::RigSplitReport report;
        RIG_CHECK(S::AssembleRigSplitV1(cpu, {&registry, T::Thread::GetCurrentThreadId()}, asset, report));
        A::SkeletalPoseSnapshot pose;
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *asset->GetClip(0), *asset->GetMesh(), 2,
                                                      F::MeshMatrix(*asset->GetMesh()), pose));
        const double linear[] = {0, 0, -4, 0, 6, 0, 0, 0, 0, -8, 0, 0};
        RIG_CHECK(pose.BonePalette.size() == 2 && pose.JointModelMatrices.size() == 2);
        for (size_t j = 0; j < 2; ++j)
        {
            for (size_t i = 0; i < 12; ++i)
            {
                RIG_CHECK(std::abs(double(pose.BonePalette[j].values[i]) - linear[i]) <= 1e-4);
                RIG_CHECK(std::abs(double(pose.JointModelMatrices[j].values[i]) - linear[i]) <= 1e-4);
            }
            RIG_CHECK(std::abs(double(pose.BonePalette[j].values[12]) - (j == 0 ? 2 : -10)) <= 1e-4);
            RIG_CHECK(std::abs(double(pose.JointModelMatrices[j].values[12]) - (j == 0 ? 8 : -10)) <= 1e-4);
            for (size_t k = 13; k < 16; ++k)
            {
                RIG_CHECK(std::abs(double(pose.BonePalette[j].values[k]) - (k == 15 ? 1 : 4)) <= 1e-4);
                RIG_CHECK(std::abs(double(pose.JointModelMatrices[j].values[k]) - (k == 15 ? 1 : 4)) <= 1e-4);
            }
        }
        const auto vertex =
            A::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
        RIG_CHECK(std::abs(vertex.Position.x - 8) <= 1e-4 && std::abs(vertex.Position.y - 4) <= 1e-4 &&
                  std::abs(vertex.Position.z - 4) <= 1e-4);
        if (const char* directory = std::getenv("NORVES_ROOT_FRAME_V2_OUTPUT"))
        {
            F::Text value = "{\"palettes\":";
            MatrixJson(value, pose.BonePalette);
            value += ",\"models\":";
            MatrixJson(value, pose.JointModelMatrices);
            value += ",\"vertex\":[";
            JsonNumber(value, vertex.Position.x);
            value += ",";
            JsonNumber(value, vertex.Position.y);
            value += ",";
            JsonNumber(value, vertex.Position.z);
            value += "]}";
            F::Bytes bytes;
            bytes.insert(bytes.end(), value.begin(), value.end());
            std::filesystem::create_directories(directory);
            F::WriteBytes(std::filesystem::path(directory) / "root-frame-general.json", bytes);
        }
        f.SetBuffer(f.Binary);
        std::printf(
            "ROOT_FRAME_RUNTIME_CASE result=pass nondiagonal_parent_noncommuting_root_translation_rotation_scale_all_matrix_oracle\n");
    }
    void Runtime(F::Fixture& f, T::JobSystem::ExecutionMode mode)
    {
        const auto json = H::Armature(f.Json);
        const auto cooked = H::Cook(json);
        X::WritePackages(cooked);
        const auto plan = H::Plan(f, cooked.ManifestJson);
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        Core::SkeletalAssetRuntime runtime;
        RIG_CHECK(runtime.Bind(registry, jobs, plan.Assets, T::Thread::GetCurrentThreadId()) == Status::Success);
        Core::SkeletalAssetCompletion completed, wrong;
        unsigned calls = 0;
        const auto request = H::Request();
        const auto first = runtime.LoadRigSplitAsync(request,
                                                     [&](const auto& c)
                                                     {
                                                         completed = c;
                                                         ++calls;
                                                     });
        RIG_CHECK(first.Status == Status::Accepted);
        RIG_CHECK(runtime
                      .LoadRigSplitAsync(request,
                                         [&](const auto& c)
                                         {
                                             RIG_CHECK(c.Failure == Failure::None);
                                             ++calls;
                                         })
                      .RequestId == first.RequestId);
        auto oldRequest = request;
        oldRequest.Profile = S::RigImportProfile::DirectTrs128;
        const auto old = runtime.LoadRigSplitAsync(oldRequest, [&](const auto& c) { wrong = c; });
        RIG_CHECK(old.Status == Status::Accepted && old.RequestId != first.RequestId);
        RIG_CHECK(Access::WaitReady(runtime, 2) && registry.GetResourceCount() == 0);
        RIG_CHECK(runtime.FlushCompleted().Callbacks == 3);
        RIG_CHECK(calls == 2 && completed.Failure == Failure::None && wrong.Failure == Failure::FormatRejected &&
                  !wrong.Asset && registry.GetResourceCount() == 4);
        const auto pose = H::Pose(completed.Asset);
        RIG_CHECK(completed.SplitDiagnostics->Load.BindingReport.Banks[0].bFrameComparisonComplete);
        auto raw = registry.CreateResource<Core::AnimationClipResource>(_T("raw"));
        raw->SetClip(S::SkeletalAnimationClip(completed.Asset->GetClip(0)->GetClip()));
        RIG_CHECK(raw->Load());
        A::SkeletalPoseSnapshot rejected;
        const bool rawRejected = !Sample(*completed.Asset->GetSkeleton(), *raw, *completed.Asset->GetMesh(), rejected);
        RIG_CHECK(rawRejected && rejected.BonePalette.empty());
        SavePose(completed.Asset, pose, rawRejected);
        unsigned hits = 0;
        RIG_CHECK(runtime
                      .LoadRigSplitAsync(request,
                                         [&](const auto& c)
                                         {
                                             RIG_CHECK(c.bCacheHit && c.Asset == completed.Asset);
                                             ++hits;
                                         })
                      .Status == Status::Accepted);
        RIG_CHECK(hits == 0 && runtime.FlushCompleted().Callbacks == 1 && hits == 1);
        // 同じ本文をsetterで入れ直してもproofは失効し、cacheは安全な成功にしない。
        completed.Asset->GetClip(0)->SetClip(S::SkeletalAnimationClip(completed.Asset->GetClip(0)->GetClip()));
        RIG_CHECK(runtime.LoadRigSplitAsync(request, [](const auto&) { RIG_CHECK(false); }).Status ==
                  Status::CacheRejected);
        runtime.Close();
        RIG_CHECK(runtime.Drain() == Status::Drained);
        jobs.Shutdown();
        registry.Shutdown();
        std::printf(
            "ROOT_FRAME_RUNTIME_CASE result=pass actual_packages_worker_owner_delegate_profile_isolation_cached_proof_pose\n");
    }
    void MismatchAndCancel(F::Fixture& f, T::JobSystem::ExecutionMode mode)
    {
        const auto json = H::Armature(f.Json);
        const auto cooked = H::Cook(json);
        X::WritePackages(cooked);
        const auto other = H::Cook(F::Replace(json, "\"translation\":[2,3,0]", "\"translation\":[3,3,0]"));
        X::WriteEntry(other.Bank);
        const auto q = H::CookRequest();
        const auto mixed = F::Text("{\"version\":1,\"assets\":[") + H::P::Record(cooked.ManifestJson, q.SkeletonPath) +
                           "," + H::P::Record(cooked.ManifestJson, q.MeshPath) + "," +
                           H::P::Record(other.ManifestJson, q.BankPath) + "]}";
        auto plan = H::Plan(f, mixed);
        auto& jobs = T::JobSystem::Get();
        jobs.Initialize(1, mode);
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        Core::SkeletalAssetRuntime runtime;
        RIG_CHECK(runtime.Bind(registry, jobs, plan.Assets, T::Thread::GetCurrentThreadId()) == Status::Success);
        for (bool overrideRest : {false, true})
        {
            auto request = H::Request();
            request.Policy.bAllowRestMismatch = overrideRest;
            Core::SkeletalAssetCompletion event;
            RIG_CHECK(runtime.LoadRigSplitAsync(request, [&](const auto& c) { event = c; }).Status == Status::Accepted);
            RIG_CHECK(Access::WaitReady(runtime, 1) && runtime.FlushCompleted().Callbacks == 1);
            RIG_CHECK(event.Failure == Failure::BindingRejected && !event.Asset &&
                      event.SplitDiagnostics->Load.BindingReport.Status == S::RigV1Status::FrameMismatch);
            RIG_CHECK(event.SplitDiagnostics->Load.BindingReport.Banks[0].bFrameComparisonComplete &&
                      !event.SplitDiagnostics->Load.BindingReport.Banks[0].bOverrideUsed &&
                      registry.GetResourceCount() == 0);
        }
        auto correct = H::Plan(f, cooked.ManifestJson);
        RIG_CHECK(runtime.SetSnapshot(correct.Assets) == Status::Success);
        const auto hook = +[](R::Detail::SkeletalRuntimePoint point, uint64_t id, void* value)
        {
            if (point == R::Detail::SkeletalRuntimePoint::BeforeCommit)
            {
                RIG_CHECK(static_cast<Core::SkeletalAssetRuntime*>(value)->Cancel(id));
            }
        };
        Access::SetHooks(runtime, {hook, nullptr, &runtime});
        RIG_CHECK(runtime.LoadRigSplitAsync(H::Request(), [](const auto&) { RIG_CHECK(false); }).Status ==
                  Status::Accepted);
        RIG_CHECK(Access::WaitReady(runtime, 1) && runtime.FlushCompleted().Callbacks == 0 &&
                  registry.GetResourceCount() == 0);
        Access::SetHooks(runtime, {});
        runtime.Close();
        RIG_CHECK(runtime.Drain() == Status::Drained);
        jobs.Shutdown();
        registry.Shutdown();
        std::printf(
            "ROOT_FRAME_RUNTIME_CASE result=pass same_local_rest_different_author_frame_override_rejected_precommit_cancel\n");
    }
    void Child(const char* exe, const char* name)
    {
        const char* args[] = {exe, "--root-frame-child", name, nullptr};
        const auto child = _spawnv(_P_NOWAIT, exe, args);
        RIG_CHECK(child != -1);
        const auto handle = reinterpret_cast<HANDLE>(child);
        const auto wait = WaitForSingleObject(handle, 60000);
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(handle, 1);
            WaitForSingleObject(handle, INFINITE);
        }
        DWORD code = 1;
        const bool read = GetExitCodeProcess(handle, &code) != 0;
        CloseHandle(handle);
        RIG_CHECK(wait == WAIT_OBJECT_0 && read && code == 0);
    }
} // namespace
int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--root-frame-child") == 0)
    {
        F::Fixture f;
        const auto mode = std::strcmp(argv[2], "simple") == 0 ? T::JobSystem::EXECUTION_SIMPLE
                                                              : T::JobSystem::EXECUTION_WORK_STEALING;
        Proof(f);
        GeneralRootTrs(f);
        Runtime(f, mode);
        MismatchAndCancel(f, mode);
        return 0;
    }
    Child(argv[0], "simple");
    Child(argv[0], "steal");
    std::printf("ROOT_FRAME_RUNTIME result=pass profile2_proof_real_runtime_safe_frame_binding_cpu\n");
    return 0;
}
