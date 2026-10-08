// 分離meshの完全材質とIBM、bank順、実ownerと実Samplerを同じ値で検査する。
#include "ClipBankV1Fixture.h"
#include "RigSplitWireTestFixture.h"
#include "Asset/CookedSkinMeshV1.h"
#include "Animation/RigSplitBinding.h"
#include "Animation/RigSplitBindingTestAccess.h"
#include <stdexcept>
#include <thread>
#include "Animation/SkeletalAnimationSampler.h"
#include "Object/ResourceRegistry.h"
#include <cmath>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace Core = NorvesLib::Core;
namespace S = Core::Skeletal;
namespace C = Core::Container;
namespace A = Core::Animation;
namespace T = NorvesLib::Thread;
namespace
{
    struct Bundle
    {
        S::RigAuthoringCpu Rig;
        S::SkeletonV1 Skeleton;
        S::SkinMeshV1 Mesh;
        S::ClipBankV1 Bank;
    };
    Bundle Build(F::Fixture& f, const F::Text& json)
    {
        Bundle b;
        b.Rig = f.Import(json);
        b.Bank = f.Bank(json);
        S::RigV1Report report;
        RIG_CHECK(S::BuildSkeletonV1(b.Rig, b.Skeleton, report));
        S::SkinMaterialV1 material;
        RIG_CHECK(S::BuildSkinMeshV1(b.Rig, b.Skeleton, "Models/Rig.nvskel", {&material, 1}, b.Mesh, report));
        return b;
    }
    void Codec(F::Fixture& f)
    {
        auto b = Build(f, f.Json);
        S::RigV1Report report;
        F::Bytes bytes, again;
        RIG_CHECK(S::WriteSkinMeshV1(b.Mesh, bytes, report));
        S::SkinMeshV1 parsed;
        RIG_CHECK(S::ParseSkinMeshV1(F::View(bytes), parsed, report));
        RIG_CHECK(S::WriteSkinMeshV1(parsed, again, report) && again == bytes);
        auto optional = NorvesLib::Tests::RigSplitWireFixture::Optional(bytes);
        S::SkinMeshV1 withOptional;
        RIG_CHECK(S::ParseSkinMeshV1(F::View(optional), withOptional, report));
        F::Put32(optional, 256 + 10 * 32, 0x50494c43);
        F::Reseal(optional);
        RIG_CHECK(!S::ParseSkinMeshV1(F::View(optional), withOptional, report) &&
                  report.Status == S::RigV1Status::UnsupportedSection);
        const auto* m = parsed.GetData();
        RIG_CHECK(m->Vertices[0].JointIndices[0] == 1 && m->Vertices[0].JointIndices[1] == 0 &&
                  m->Vertices[0].JointIndices[2] == 1);
        RIG_CHECK(m->InverseBindMatrices[0][13] == -1 && m->InverseBindMatrices[1][13] == 0 &&
                  m->MeshTransform[12] == 5);
        RIG_CHECK(m->Slots.size() == 1 && m->Slots[0].Name == "Default" && m->Materials.size() == 1);
        RIG_CHECK(S::MatchSkinMeshSkeletonV1(parsed, b.Skeleton, report));
        for (int n = 0; n < 11; ++n)
        {
            auto broken = bytes;
            const size_t ref = F::SectionOffset(bytes, 2), vertex = F::SectionOffset(bytes, 3),
                         ibm = F::SectionOffset(bytes, 5), sub = F::SectionOffset(bytes, 7),
                         slot = F::SectionOffset(bytes, 8), mat = F::SectionOffset(bytes, 9);
            switch (n)
            {
            case 0:
                F::Put32(broken, vertex + 40, 2);
                break; // zero-weight slotも範囲内。
            case 1:
                F::Float(broken, vertex + 48, -1);
                break;
            case 2:
                F::Float(broken, ibm, 0);
                break;
            case 3:
                F::Put32(broken, sub, 1);
                break;
            case 4:
                F::Put32(broken, sub + 16, 1);
                break;
            case 5:
                F::Put32(broken, slot + 16, 1);
                break;
            case 6:
                broken[mat + 124] = 1;
                break;
            case 7:
                F::Put32(broken, mat + 116, 64);
                break;
            case 8:
                F::Put32(broken, ref + 12, 2);
                break;
            case 9:
                F::Put32(broken, F::SectionOffset(bytes, 4), 3);
                break;
            case 10:
                F::Float(broken, vertex + 48, 0);
                break;
            }
            F::Reseal(broken);
            RIG_CHECK(!S::ParseSkinMeshV1(F::View(broken), parsed, report) && parsed.GetData() == m);
        }
        for (size_t n = 0; n < bytes.size(); ++n)
        {
            RIG_CHECK(!S::ParseSkinMeshV1({bytes.data(), n}, parsed, report));
        }
        auto newRig = f.Import(F::ChildTrs(f.Json, "\"translation\":[0,2,0]"));
        S::SkeletonV1 changed;
        RIG_CHECK(S::BuildSkeletonV1(newRig, changed, report));
        RIG_CHECK(!S::MatchSkinMeshSkeletonV1(parsed, changed, report) &&
                  report.Status == S::RigV1Status::HashMismatch);
        S::SkinMaterialV1 material;
        material.Record.Flags = Core::Asset::CookedMaterialFormatV1::ArmUseMetallic |
                                Core::Asset::CookedMaterialFormatV1::DoubleSided | (2 << 1);
        material.Textures[0] = "Textures/base.nvtex";
        material.Textures[2] = "Textures/arm.nvtex";
        S::SkinMeshV1 full;
        RIG_CHECK(S::BuildSkinMeshV1(b.Rig, b.Skeleton, "Models/Rig.nvskel", {&material, 1}, full, report));
        RIG_CHECK(S::WriteSkinMeshV1(full, again, report) && S::ParseSkinMeshV1(F::View(again), full, report));
        RIG_CHECK(full.GetData()->Materials[0].Record.Flags == material.Record.Flags &&
                  full.GetData()->Materials[0].Textures[2] == material.Textures[2]);
        namespace P = NorvesLib::Tests::RigSplitWireFixture;
        auto alias = bytes;
        const auto materialOffset = F::SectionOffset(alias, 9);
        F::Bytes strings;
        const auto stringStart = F::SectionOffset(alias, 0), stringSize = size_t(F::U64(alias, 256 + 16));
        strings.insert(strings.end(), alias.begin() + stringStart, alias.begin() + stringStart + stringSize);
        const F::Text shared = "Textures/a.nvtex";
        for (size_t i = 0; i < 4; ++i)
        {
            F::Put64(alias, materialOffset + i * 16, strings.size());
            F::Put32(alias, materialOffset + i * 16 + 8, uint32_t(shared.size()));
        }
        strings.insert(strings.end(), shared.begin(), shared.end());
        alias = P::Strings(alias, strings);
        auto extraMaterial = bytes;
        const auto mats = F::SectionOffset(bytes, 9);
        F::Bytes extraRecord;
        extraRecord.insert(extraRecord.end(), bytes.begin() + mats, bytes.begin() + mats + 128);
        extraMaterial.insert(extraMaterial.end(), extraRecord.begin(), extraRecord.end());
        F::Put64(extraMaterial, 40, extraMaterial.size());
        F::Put64(extraMaterial, 256 + 9 * 32 + 16, 256);
        F::Put32(extraMaterial, 256 + 9 * 32 + 28, 2);
        F::Reseal(extraMaterial);
        P::AllocationCounts mismatchCounts;
        {
            P::ObserveAllocations observe(mismatchCounts);
            RIG_CHECK(!S::ParseSkinMeshV1(F::View(extraMaterial), parsed, report) &&
                      report.Status == S::RigV1Status::BadWire && mismatchCounts.Owned == 0);
        }
        S::RigV1Limits aliasLimits;
        aliasLimits.MaxStringBytes = uint32_t(strings.size());
        P::AllocationCounts counts;
        {
            P::ObserveAllocations observe(counts);
            RIG_CHECK(!S::ParseSkinMeshV1(F::View(alias), parsed, report, aliasLimits) &&
                      report.Status == S::RigV1Status::LimitExceeded);
        }
        RIG_CHECK(counts.Owned == 0 && counts.Topology == 0 && parsed.GetData() == m);
        aliasLimits.MaxStringBytes = uint32_t(stringSize + shared.size() * 4);
        RIG_CHECK(S::ParseSkinMeshV1(F::View(alias), parsed, report, aliasLimits) &&
                  S::WriteSkinMeshV1(parsed, again, report, aliasLimits));
        --aliasLimits.MaxStringBytes;
        counts = {};
        {
            P::ObserveAllocations observe(counts);
            RIG_CHECK(!S::ParseSkinMeshV1(F::View(alias), parsed, report, aliasLimits) && counts.Owned == 0 &&
                      counts.Topology == 0);
        }
        const char* output = std::getenv("NORVES_RIG_SPLIT_V1_OUTPUT");
        if (output && *output)
        {
            std::filesystem::create_directories(output);
            F::WriteBytes(std::filesystem::path(output) / "skinmesh-v1.nvskel", bytes);
        }
        std::printf("RIG_SPLIT_CASE mesh_wire result=pass\n");
    }
    struct Failure
    {
        uint32_t Stop = 0;
        bool bThrow = false;
        C::VariableArray<C::TWeakPtr<Core::Resource>> Seen;
    };
    bool FailCreate(uint32_t ordinal, const C::TSharedPtr<Core::Resource>& resource, void* context)
    {
        auto& failure = *static_cast<Failure*>(context);
        failure.Seen.push_back(resource);
        if (ordinal != failure.Stop)
        {
            return true;
        }
        if (failure.bThrow)
        {
            throw std::runtime_error("split-create-test");
        }
        return false;
    }
    void BindingAndPose(F::Fixture& f, Core::ResourceRegistry& registry)
    {
        auto b = Build(f, f.Json);
        S::RigSplitReport report;
        S::CookedRigSplitCpuAsset cpu;
        RIG_CHECK(S::BindRigSplitV1(b.Skeleton, b.Mesh, {&b.Bank, 1}, {}, cpu, report));
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        RIG_CHECK(S::AssembleRigSplitV1(cpu, {&registry, T::Thread::GetCurrentThreadId()}, asset, report));
        RIG_CHECK(asset->GetSkeleton()->GetJoints().empty() && asset->GetSkeleton()->GetAuthorRestPose().size() == 2);
        RIG_CHECK(asset->GetMesh()->IsLoaded() && !asset->GetMesh()->GetRenderAssetLease());
        auto clip = asset->GetClip(C::StringView(_T("Wave")));
        RIG_CHECK(clip);
        A::SkeletalPoseSnapshot pose;
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), 1,
                                                      F::MeshMatrix(*asset->GetMesh()), pose));
        RIG_CHECK(std::abs(pose.BonePalette[0].m31 - 1) < 1e-5 && std::abs(pose.JointModelMatrices[0].m31 - 2) < 1e-5);
        auto v = A::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
        RIG_CHECK(std::abs(v.Position.x + 5) < 1e-5 && std::abs(v.Position.y - 2) < 1e-5);
        auto second = f.Bank(F::Replace(f.Json, "\"name\":\"Wave\"", "\"name\":\"Walk\""));
        S::ClipBankV1 banks[] = {b.Bank, second};
        RIG_CHECK(S::BindRigSplitV1(b.Skeleton, b.Mesh, {banks, 2}, {}, cpu, report));
        RIG_CHECK(cpu.GetData()->Clips.size() == 2 && cpu.GetData()->Clips[1].Name == _T("Walk"));
        const auto* stable = cpu.GetData();
        banks[1] = b.Bank;
        RIG_CHECK(!S::BindRigSplitV1(b.Skeleton, b.Mesh, {banks, 2}, {}, cpu, report) && cpu.GetData() == stable);
        auto changed = Build(f, F::ChildTrs(f.Json, "\"translation\":[0,2,0]"));
        banks[1] = f.Bank(
            F::Replace(F::ChildTrs(f.Json, "\"translation\":[0,2,0]"), "\"name\":\"Wave\"", "\"name\":\"Walk\""));
        RIG_CHECK(!S::BindRigSplitV1(b.Skeleton, b.Mesh, {banks, 2}, {}, cpu, report) && report.FailedBank == 1 &&
                  report.Banks.size() == 2 && report.Status == S::RigV1Status::RestMismatch && cpu.GetData() == stable);
        S::RigBindingPolicy overridePolicy;
        overridePolicy.bAllowRestMismatch = true;
        RIG_CHECK(S::BindRigSplitV1(b.Skeleton, b.Mesh, {banks, 2}, overridePolicy, cpu, report) &&
                  report.Banks[1].bOverrideUsed);
        RIG_CHECK(!S::BindRigSplitV1(changed.Skeleton, b.Mesh, {banks, 2}, overridePolicy, cpu, report) &&
                  report.Status == S::RigV1Status::HashMismatch);
        // 同じSkeleton値に違うmesh IBMを結び、poseの役割分離を実数で確かめる。
        F::Bytes changedMeshBytes;
        S::RigV1Report wireReport;
        RIG_CHECK(S::WriteSkinMeshV1(b.Mesh, changedMeshBytes, wireReport));
        F::Float(changedMeshBytes, F::SectionOffset(changedMeshBytes, 5) + 52, -2);
        F::Reseal(changedMeshBytes);
        S::SkinMeshV1 alternateMesh;
        RIG_CHECK(S::ParseSkinMeshV1(F::View(changedMeshBytes), alternateMesh, wireReport));
        S::CookedRigSplitCpuAsset alternateCpu;
        RIG_CHECK(S::BindRigSplitV1(b.Skeleton, alternateMesh, {&b.Bank, 1}, {}, alternateCpu, report));
        C::TSharedPtr<Core::SkeletalAssetResource> alternate;
        RIG_CHECK(S::AssembleRigSplitV1(alternateCpu, {&registry, T::Thread::GetCurrentThreadId()}, alternate, report));
        RIG_CHECK(alternate->GetSkeleton()->GetSplitSkeleton() == asset->GetSkeleton()->GetSplitSkeleton());
        A::SkeletalPoseSnapshot alternatePose;
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*alternate->GetSkeleton(), *alternate->GetClip(0),
                                                      *alternate->GetMesh(), 1, F::MeshMatrix(*alternate->GetMesh()),
                                                      alternatePose));
        RIG_CHECK(std::abs(alternatePose.BonePalette[0].m31) < 1e-5 &&
                  std::abs(alternatePose.JointModelMatrices[0].m31 - 2) < 1e-5);
        for (uint32_t ordinal = 0; ordinal < cpu.GetData()->Clips.size() + 3; ++ordinal)
        {
            for (bool bThrow : {false, true})
            {
                Failure failure;
                failure.Stop = ordinal;
                failure.bThrow = bThrow;
                auto sentinel = asset;
                RIG_CHECK(!S::Detail::AssembleRigSplitWithProbe(cpu, {&registry, T::Thread::GetCurrentThreadId()},
                                                                asset, report, FailCreate, &failure) &&
                          asset == sentinel);
                for (const auto& weak : failure.Seen)
                {
                    RIG_CHECK(weak.expired());
                }
                RIG_CHECK(registry.GetResourceCount() == 0 && registry.GetCachedPathCount() == 0);
            }
        }
        const auto owner = T::Thread::GetCurrentThreadId();
        bool wrongSucceeded = true;
        std::thread wrong(
            [&]()
            {
                S::RigSplitReport local;
                C::TSharedPtr<Core::SkeletalAssetResource> ignored;
                wrongSucceeded = S::AssembleRigSplitV1(cpu, {&registry, owner}, ignored, local);
                RIG_CHECK(local.Status == S::RigV1Status::WrongOwner);
            });
        wrong.join();
        RIG_CHECK(!wrongSucceeded);
        auto kept = asset;
        RIG_CHECK(!S::AssembleRigSplitV1(cpu, {&registry, {}}, asset, report) && asset == kept);
        RIG_CHECK(registry.GetResourceCount() == 0 && registry.GetCachedPathCount() == 0);
        // 旧setterがsplit内容を破壊したり、GPUの既定素材へ戻したりしない。
        asset->GetMesh()->SetVertices({});
        asset->GetMesh()->RefreshRenderAssetLease();
        asset->GetSkeleton()->SetJoints({});
        RIG_CHECK(asset->GetMesh()->GetVertices().size() == 3 && asset->GetSkeleton()->GetSplitSkeleton());
        asset->GetMesh()->Unload();
        RIG_CHECK(!asset->GetMesh()->Load() && asset->GetMesh()->IsSplitV1());
        RIG_CHECK(!A::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), 1,
                                                       F::MeshMatrix(*asset->GetMesh()), pose));
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*alternate->GetSkeleton(), *alternate->GetClip(0),
                                                      *alternate->GetMesh(), 1, F::MeshMatrix(*alternate->GetMesh()),
                                                      alternatePose));
        std::printf("RIG_SPLIT_CASE binding_pose result=pass\n");
    }
} // namespace
int main()
{
    F::Fixture fixture;
    Codec(fixture);
    Core::ResourceRegistry registry;
    RIG_CHECK(registry.Initialize());
    BindingAndPose(fixture, registry);
    RIG_CHECK(registry.GetResourceCount() == 0 && registry.GetCachedPathCount() == 0);
    registry.Shutdown();
    std::printf(
        "SKINMESH_V1_WIRE result=pass per_mesh_ibm_full_materials_ordered_banks_rest_guard_owner_cpu_pose_no_render_lease\n");
    return 0;
}
