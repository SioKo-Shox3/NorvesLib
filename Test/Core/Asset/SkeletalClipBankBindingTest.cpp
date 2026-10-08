// 実ResourceとSamplerでrest拒否/明示overrideを検査する。
#include "ClipBankV1Fixture.h"
#include "Animation/ClipBankBindingTestAccess.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Object/ResourceRegistry.h"
#include <cmath>
#include <limits>
#include <thread>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace Core = NorvesLib::Core;
namespace S = Core::Skeletal;
namespace C = Core::Container;
namespace A = Core::Animation;
namespace T = NorvesLib::Thread;
namespace
{
    bool Near(double a, double b, double epsilon = 1e-5)
    {
        return std::abs(a - b) <= epsilon;
    }
    S::BoundClipBank Bind(const S::ClipBankV1& bank, const S::RigAuthoringCpu& rig,
                          const S::RigBindingPolicy& policy = {})
    {
        S::BoundClipBank result;
        S::RigV1Report report;
        RIG_CHECK(S::BindClipBankV1(bank, rig, policy, result, report));
        return result;
    }
    C::TSharedPtr<Core::SkeletalAssetResource> Assemble(Core::ResourceRegistry& registry, const S::BoundClipBank& bound)
    {
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        S::RigV1Report report;
        RIG_CHECK(S::AssembleBoundClipBank(bound, {&registry, T::Thread::GetCurrentThreadId()}, asset, report));
        return asset;
    }
    A::SkeletalPoseSnapshot Sample(const C::TSharedPtr<Core::SkeletalAssetResource>& asset, float time = 1.f)
    {
        auto clip = asset->GetClip(C::StringView(_T("Wave")));
        RIG_CHECK(clip);
        A::SkeletalPoseSnapshot pose;
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), time,
                                                      F::MeshMatrix(*asset->GetMesh()), pose));
        return pose;
    }
    void SameAndReordered(F::Fixture& f, Core::ResourceRegistry& registry)
    {
        auto bank = f.Bank(f.Json);
        auto rig = f.Import(f.Json);
        auto bound = Bind(bank, rig);
        auto asset = Assemble(registry, bound);
        auto pose = Sample(asset);
        RIG_CHECK(pose.JointModelMatrices.size() == 2 && Near(pose.JointModelMatrices[0].values[12], -5) &&
                  Near(pose.JointModelMatrices[1].values[13], 2));
        RIG_CHECK(Near(pose.BonePalette[1].values[12], -5) && Near(pose.BonePalette[1].values[13], 1));
        auto vertex = A::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
        RIG_CHECK(Near(vertex.Position.x, -5) && Near(vertex.Position.y, 2));
        // 対照: legacyの未アニメrootはIBM/M由来x=5なのでmodel x=0。新rest既定と混同しない。
        auto legacy = registry.CreateResource<Core::SkeletonResource>(_T("legacy"));
        auto joints = rig.GetData()->Geometry.Joints;
        legacy->SetJoints(std::move(joints));
        RIG_CHECK(legacy->Load());
        A::SkeletalPoseSnapshot old;
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*legacy, *asset->GetAnimationClip(), *asset->GetMesh(), 1,
                                                      F::MeshMatrix(*asset->GetMesh()), old));
        RIG_CHECK(Near(old.JointModelMatrices[0].values[12], 0));
        RIG_CHECK(!asset->GetSkeleton()->SetAuthorRestPose(rig.GetData()->LocalRest)); // Load済みを無言で差し替えない。
        auto bytes = F::SwappedBuffer(f.Binary);
        f.SetBuffer(bytes);
        auto reordered = f.Import(F::Replace(f.Json, "\"joints\":[0,1]", "\"joints\":[1,0]"));
        RIG_CHECK(reordered.GetData()->Topology.SkeletonId == rig.GetData()->Topology.SkeletonId);
        auto swappedAsset = Assemble(registry, Bind(bank, reordered));
        auto swapped = Sample(swappedAsset);
        for (size_t n = 0; n < 16; ++n)
        {
            RIG_CHECK(Near(swapped.BonePalette[0].values[n], pose.BonePalette[1].values[n]) &&
                      Near(swapped.BonePalette[1].values[n], pose.BonePalette[0].values[n]));
        }
        auto swappedVertex =
            A::SkeletalAnimationSampler::SkinVertex(swappedAsset->GetMesh()->GetVertices()[2], swapped.BonePalette);
        RIG_CHECK(Near(swappedVertex.Position.x, vertex.Position.x) &&
                  Near(swappedVertex.Position.y, vertex.Position.y));
        f.SetBuffer(f.Binary);
        RIG_CHECK(registry.GetResourceCount() == 0 && registry.GetCachedPathCount() == 0);
        std::printf("CLIPBANK_V1_CASE result=pass source_rest_real_pose_reordered_names_legacy_negative_control\n");
    }
    void MismatchAndThresholds(F::Fixture& f, Core::ResourceRegistry& registry)
    {
        const auto source = F::AllTranslation(f.Json);
        auto bank = f.Bank(source);
        auto rig = f.Import(source);
        S::BoundClipBank stable = Bind(bank, rig);
        const auto* old = stable.GetData();
        S::RigV1Report report;
        auto changed = f.Import(F::ChildTrs(source, "\"translation\":[0,2,0]"));
        RIG_CHECK(changed.GetData()->Topology.SkeletonId == rig.GetData()->Topology.SkeletonId);
        RIG_CHECK(!S::BindClipBankV1(bank, changed, {}, stable, report) &&
                  report.Status == S::RigV1Status::RestMismatch && stable.GetData() == old &&
                  report.bComparisonComplete);
        RIG_CHECK(report.Snapshots.size() == 1 && report.Snapshots[0].ExceededJoints == 1 &&
                  Near(report.Snapshots[0].MaximumTranslationMeters, 1));
        S::RigBindingPolicy allow;
        allow.bAllowRestMismatch = true;
        S::BoundClipBank overrideBound;
        RIG_CHECK(S::BindClipBankV1(bank, changed, allow, overrideBound, report) && report.bOverrideUsed &&
                  report.Differences.size() == 2);
        auto asset = Assemble(registry, overrideBound);
        auto pose = Sample(asset, 0);
        // 古い絶対Translationがそのまま使われる。overrideはretarget/補正ではない。
        RIG_CHECK(Near(pose.JointModelMatrices[1].values[13], 2)); // root1 + old child1。
        RIG_CHECK(asset->GetSkeleton()->GetAuthorRestPose()[1].Translation.Y == 2);
        RIG_CHECK(bank.GetData()->Snapshots[0].Rest[0].Translation.Y == 1);
        if (const char* output = std::getenv("NORVES_CLIPBANK_V1_OUTPUT"); output && *output)
        {
            const std::filesystem::path directory(output);
            std::filesystem::create_directories(directory);
            std::ofstream json(directory / "rest-binding.json", std::ios::binary);
            char text[1024];
            const int length = std::snprintf(
                text, sizeof(text),
                "{\"schema\":1,\"skeleton_id\":\"%016llx\",\"comparison_complete\":%s,\"override_used\":%s,\"exceeded_joints\":%u,\"translation_delta_m\":%.17g,\"author_child_y\":%.17g,\"target_child_y\":%.17g,\"sampled_child_y\":%.17g}\n",
                static_cast<unsigned long long>(report.SkeletonId), report.bComparisonComplete ? "true" : "false",
                report.bOverrideUsed ? "true" : "false", report.Snapshots[0].ExceededJoints,
                report.Snapshots[0].MaximumTranslationMeters,
                double(bank.GetData()->Snapshots[0].Rest[0].Translation.Y),
                double(asset->GetSkeleton()->GetAuthorRestPose()[1].Translation.Y),
                double(pose.JointModelMatrices[1].values[13]));
            RIG_CHECK(length > 0 && static_cast<size_t>(length) < sizeof(text));
            json.write(text, length);
            json.close();
            RIG_CHECK(!json.fail());
        }
        auto fresh = f.Bank(F::ChildTrs(source, "\"translation\":[0,2,0]"));
        // 新rigで再exportしただけでは古いaction履歴を復元したことにはしない。
        RIG_CHECK(fresh.GetData()->Snapshots[0].Rest[0].Translation.Y == 2);
        auto unanimated = f.Import(F::RootTrs(f.Json, "\"translation\":[0.00001,0,0],"));
        auto original = f.Bank(f.Json);
        RIG_CHECK(S::BindClipBankV1(original, unanimated, allow, overrideBound, report));
        const double delta = report.Snapshots[0].MaximumTranslationMeters;
        RIG_CHECK(delta > 0 && Near(delta, 1e-5, 1e-11));
        S::RigBindingPolicy exact;
        exact.Tolerance.TranslationMeters = delta;
        RIG_CHECK(S::BindClipBankV1(original, unanimated, exact, overrideBound, report) && !report.bOverrideUsed);
        exact.Tolerance.TranslationMeters = std::nextafter(delta, 0.0);
        RIG_CHECK(!S::BindClipBankV1(original, unanimated, exact, overrideBound, report));
        exact.Tolerance.TranslationMeters = std::nextafter(delta, std::numeric_limits<double>::infinity());
        RIG_CHECK(S::BindClipBankV1(original, unanimated, exact, overrideBound, report));
        auto sign = f.Import(F::RootTrs(f.Json, "\"rotation\":[0,0,0,-1],"));
        RIG_CHECK(S::BindClipBankV1(original, sign, {}, overrideBound, report) &&
                  report.Snapshots[0].MaximumRotationRadians == 0);
        auto rotation =
            f.Import(F::ChildTrs(f.Json, "\"translation\":[0,1,0],\"rotation\":[0,0,0.70710677,0.70710677]"));
        RIG_CHECK(!S::BindClipBankV1(original, rotation, {}, overrideBound, report));
        RIG_CHECK(Near(report.Snapshots[0].MaximumRotationRadians, 1.5707963267948966, 1e-7));
        const double angle = report.Snapshots[0].MaximumRotationRadians;
        exact = {};
        exact.Tolerance.RotationRadians = angle;
        RIG_CHECK(S::BindClipBankV1(original, rotation, exact, overrideBound, report));
        exact.Tolerance.RotationRadians = std::nextafter(angle, 0.0);
        RIG_CHECK(!S::BindClipBankV1(original, rotation, exact, overrideBound, report));
        exact.Tolerance.RotationRadians = std::nextafter(angle, std::numeric_limits<double>::infinity());
        RIG_CHECK(S::BindClipBankV1(original, rotation, exact, overrideBound, report));
        auto nonuniform = f.Import(F::ChildTrs(f.Json, "\"translation\":[0,1,0],\"scale\":[2,1,1]"));
        RIG_CHECK(!S::BindClipBankV1(original, nonuniform, {}, overrideBound, report));
        RIG_CHECK(Near(report.Snapshots[0].MaximumLogScale, 0.6931471805599453, 1e-12));
        const double scaleDelta = report.Snapshots[0].MaximumLogScale;
        exact = {};
        exact.Tolerance.LogScale = scaleDelta;
        RIG_CHECK(S::BindClipBankV1(original, nonuniform, exact, overrideBound, report));
        exact.Tolerance.LogScale = std::nextafter(scaleDelta, 0.0);
        RIG_CHECK(!S::BindClipBankV1(original, nonuniform, exact, overrideBound, report));
        exact.Tolerance.LogScale = std::nextafter(scaleDelta, std::numeric_limits<double>::infinity());
        RIG_CHECK(S::BindClipBankV1(original, nonuniform, exact, overrideBound, report));
        auto scalePose = Sample(Assemble(registry, Bind(original, nonuniform, allow)));
        RIG_CHECK(Near(scalePose.BonePalette[1].values[0], 2) && Near(scalePose.BonePalette[1].values[5], 1) &&
                  Near(scalePose.BonePalette[1].values[10], 1));
        auto foreign = f.Import(F::Replace(f.Json, "\"name\":\"Child\"", "\"name\":\"Other\""));
        RIG_CHECK(!S::BindClipBankV1(original, foreign, allow, overrideBound, report) &&
                  report.Status == S::RigV1Status::TopologyMismatch);
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        {
            auto bad = allow;
            bad.Tolerance.LogScale = invalid;
            RIG_CHECK(!S::BindClipBankV1(original, rig, bad, overrideBound, report));
        }
        auto duplicate = f.Import(F::Replace(source, "\"name\":\"Wave\"", "\"name\":\"Second\""));
        S::RigAuthoringCpu sources[] = {rig, duplicate};
        S::ClipBankV1 multi;
        RIG_CHECK(S::BuildClipBankV1({sources, 2}, multi, report));
        F::Bytes wire;
        RIG_CHECK(S::WriteClipBankV1(multi, wire, report) && S::ParseClipBankV1(F::View(wire), multi, report));
        auto multiAsset = Assemble(registry, Bind(multi, rig));
        RIG_CHECK(multiAsset->GetClipCount() == 2 && multiAsset->GetClip(C::StringView(_T("Second"))));
        sources[1] = rig;
        RIG_CHECK(!S::BuildClipBankV1({sources, 2}, multi, report) && report.Status == S::RigV1Status::InvalidName);
        auto secondAuthor = f.Import(
            F::ChildTrs(F::Replace(source, "\"name\":\"Wave\"", "\"name\":\"Second\""), "\"translation\":[0,2,0]"));
        sources[1] = secondAuthor;
        RIG_CHECK(S::BuildClipBankV1({sources, 2}, multi, report));
        RIG_CHECK(S::WriteClipBankV1(multi, wire, report) && S::ParseClipBankV1(F::View(wire), multi, report));
        RIG_CHECK(!S::BindClipBankV1(multi, rig, {}, overrideBound, report) && report.Snapshots.size() == 2);
        RIG_CHECK(report.Snapshots[0].ExceededJoints == 0 && report.Snapshots[1].ExceededJoints == 1);
        RIG_CHECK(S::BindClipBankV1(multi, rig, allow, overrideBound, report) && report.bOverrideUsed);
        RIG_CHECK(multi.GetData()->ClipSnapshots[0] == 0 && multi.GetData()->ClipSnapshots[1] == 1);
        RIG_CHECK(multi.GetData()->Snapshots[0].Rest[0].Translation.Y == 1 &&
                  multi.GetData()->Snapshots[1].Rest[0].Translation.Y == 2);
        std::printf(
            "CLIPBANK_V1_CASE result=pass all_translation_default_reject_override_report_thresholds_qsign_nonuniform_multisnapshot\n");
    }
    struct Probe
    {
        uint32_t FailAt = 0;
        bool bThrow = false;
        C::VariableArray<C::TWeakPtr<Core::Resource>> Weak;
        static bool Call(uint32_t ordinal, const C::TSharedPtr<Core::Resource>& candidate, void* context)
        {
            auto& p = *static_cast<Probe*>(context);
            p.Weak.push_back(candidate);
            if (ordinal == p.FailAt)
            {
                if (p.bThrow)
                {
                    throw std::bad_alloc();
                }
                return false;
            }
            return true;
        }
    };
    void FailureOwnership(F::Fixture& f, Core::ResourceRegistry& registry)
    {
        auto bank = f.Bank(f.Json);
        auto rig = f.Import(f.Json);
        auto bound = Bind(bank, rig);
        auto stable = Assemble(registry, bound);
        const auto sentinel = stable;
        S::RigV1Report report;
        RIG_CHECK(!S::AssembleBoundClipBank(bound, {&registry, {}}, stable, report) &&
                  report.Status == S::RigV1Status::WrongOwner && stable == sentinel);
        Core::ResourceRegistry uninitialized;
        RIG_CHECK(!S::AssembleBoundClipBank(bound, {&uninitialized, T::Thread::GetCurrentThreadId()}, stable, report) &&
                  report.Status == S::RigV1Status::RegistryNotReady);
        const auto owner = T::Thread::GetCurrentThreadId();
        std::thread wrong(
            [&]()
            {
                RIG_CHECK(!S::AssembleBoundClipBank(bound, {&registry, owner}, stable, report) &&
                          report.Status == S::RigV1Status::WrongOwner);
            });
        wrong.join();
        for (bool throwing : {false, true})
        {
            for (uint32_t at = 0; at < 4; ++at)
            {
                Probe p;
                p.FailAt = at;
                p.bThrow = throwing;
                RIG_CHECK(!S::Detail::AssembleBoundClipBankWithProbe(bound, {&registry, owner}, stable, report,
                                                                     Probe::Call, &p));
                RIG_CHECK(stable == sentinel && registry.GetResourceCount() == 0 &&
                          registry.GetCachedPathCount() == 0 && p.Weak.size() == at + 1);
                for (const auto& weak : p.Weak)
                {
                    RIG_CHECK(weak.expired());
                }
            }
        }
        auto targetPointer = bound.GetData()->Target.GetData();
        rig = {};
        bank = {};
        RIG_CHECK(bound.GetData()->Target.GetData() == targetPointer);
        auto after = Assemble(registry, bound);
        RIG_CHECK(Sample(after).BonePalette.size() == 2);
        std::printf(
            "CLIPBANK_V1_CASE result=pass explicit_owner_unregistered_atomic_output_faults_owned_target_no_gpu\n");
    }
} // namespace
int main()
{
    F::Fixture fixture;
    Core::ResourceRegistry registry;
    RIG_CHECK(registry.Initialize());
    SameAndReordered(fixture, registry);
    MismatchAndThresholds(fixture, registry);
    FailureOwnership(fixture, registry);
    RIG_CHECK(registry.GetResourceCount() == 0 && registry.GetCachedPathCount() == 0);
    registry.Shutdown();
    std::printf(
        "CLIPBANK_V1_BINDING result=pass author_rest_default_reject_explicit_override_names_actual_resource_sampler_legacy_preserved_no_split_runtime_gpu\n");
    return 0;
}
