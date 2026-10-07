#include "Animation/ClipBankBinding.h"
#include "Animation/ClipBankRestComparison.h"
#include "Animation/ClipBankBindingTestAccess.h"
#include "Object/ResourceRegistry.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Skeletal
{
    namespace C = Container;
    bool BindClipBankV1(const ClipBankV1& bank, const RigAuthoringCpu& target, const RigBindingPolicy& policy,
                        BoundClipBank& out, RigV1Report& report)
    {
        report = {};
        report.Policy = policy;
        try
        {
            const auto* source = bank.GetData();
            const auto* rig = target.GetData();
            if (!source || !rig || !IsValidRigBindingPolicy(policy))
            {
                return false;
            }
            if (source->Profile != RigImportProfile::DirectTrs128 || rig->Profile != RigImportProfile::DirectTrs128)
            {
                report.Status = RigV1Status::UnsupportedProfile;
                return false;
            }
            report.TargetLabel = rig->SourceLabel;
            report.SkeletonId = rig->Topology.SkeletonId;
            report.BankPayloadHash = source->PayloadHash;
            if (!SameRigTopology(source->Topology, rig->Topology))
            {
                report.Status = RigV1Status::TopologyMismatch;
                return false;
            }
            C::VariableArray<SkeletalRestTransform> targetCanonical;
            targetCanonical.reserve(rig->LocalRest.size());
            for (uint32_t index : rig->Topology.CanonicalToSource)
            {
                targetCanonical.push_back(rig->LocalRest[index]);
            }
            if (!Detail::CompareClipBankRest(
                    *source, {targetCanonical.data(), targetCanonical.size()},
                    {rig->Topology.CanonicalToSource.data(), rig->Topology.CanonicalToSource.size()},
                    rig->ResolvedImportScale, policy, report))
            {
                return false;
            }
            auto data = C::MakeShared<BoundClipBankData>();
            data->Target = target;
            data->Clips = source->Clips;
            for (auto& clip : data->Clips)
            {
                for (auto& channel : clip.Channels)
                {
                    if (channel.JointIndex >= rig->Topology.CanonicalToSource.size())
                    {
                        report.Status = RigV1Status::InvalidClip;
                        return false;
                    }
                    channel.JointIndex = rig->Topology.CanonicalToSource[channel.JointIndex];
                }
            }
            report.Status = RigV1Status::Success;
            data->BindingReport = report;
            BoundClipBank candidate;
            candidate.m_Data = std::move(data);
            out = std::move(candidate);
            return true;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool Detail::AssembleBoundClipBankWithProbe(const BoundClipBank& bound,
                                                const ResourceIO::SkeletalAssetCreateContext& context,
                                                C::TSharedPtr<SkeletalAssetResource>& out, RigV1Report& report,
                                                RigCreateProbe probe, void* probeContext)
    {
        report = {};
        try
        {
            if (context.OwnerThread == Thread::Thread::ThreadId{} ||
                context.OwnerThread != Thread::Thread::GetCurrentThreadId())
            {
                report.Status = RigV1Status::WrongOwner;
                return false;
            }
            if (!context.Registry || !context.Registry->IsInitialized())
            {
                report.Status = RigV1Status::RegistryNotReady;
                return false;
            }
            const auto* data = bound.GetData();
            if (!data || !data->Target.GetData())
            {
                return false;
            }
            report = data->BindingReport;
            const auto& target = *data->Target.GetData();
            auto& registry = *context.Registry;
            uint32_t ordinal = 0;
            const auto observe = [&](const C::TSharedPtr<Resource>& value)
            {
                if (!value || value->GetResourceId() == 0 || (probe && !probe(ordinal, value, probeContext)))
                {
                    report.Status = RigV1Status::ResourceFailure;
                    return false;
                }
                ++ordinal;
                return true;
            };
            // P1と同じ未登録CreateResource/Load/全成功後out置換の境界を保つ。
            auto mesh = registry.CreateResource<SkinnedMeshResource>(_T("rig-v1-bound"));
            if (!observe(mesh))
            {
                return false;
            }
            auto vertices = target.Geometry.Vertices;
            auto indices = target.Geometry.Indices;
            auto submeshes = target.Geometry.SubMeshes;
            auto slots = target.Geometry.MaterialSlots;
            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));
            mesh->SetSubmeshTables(std::move(submeshes), std::move(slots));
            mesh->SetMeshNodeGlobalTransform(target.Geometry.MeshNodeGlobalTransform);
            if (!mesh->Load())
            {
                report.Status = RigV1Status::ResourceFailure;
                return false;
            }
            auto skeleton = registry.CreateResource<SkeletonResource>(_T("rig-v1-bound"));
            if (!observe(skeleton))
            {
                return false;
            }
            auto joints = target.Geometry.Joints;
            skeleton->SetJoints(std::move(joints));
            if (!skeleton->SetAuthorRestPose(target.LocalRest) || !skeleton->Load())
            {
                report.Status = RigV1Status::ResourceFailure;
                return false;
            }
            C::VariableArray<C::TSharedPtr<AnimationClipResource>> clips;
            clips.reserve(data->Clips.size());
            for (const auto& value : data->Clips)
            {
                auto clip = registry.CreateResource<AnimationClipResource>(_T("rig-v1-bound"));
                if (!observe(clip))
                {
                    return false;
                }
                clip->SetClip(SkeletalAnimationClip(value));
                if (!clip->Load())
                {
                    report.Status = RigV1Status::ResourceFailure;
                    return false;
                }
                clips.push_back(std::move(clip));
            }
            auto asset = registry.CreateResource<SkeletalAssetResource>(_T("rig-v1-bound"));
            if (!observe(asset))
            {
                return false;
            }
            asset->SetClipResources(mesh, skeleton, clips);
            if (!asset->Load())
            {
                report.Status = RigV1Status::ResourceFailure;
                return false;
            }
            out = std::move(asset);
            report.Status = RigV1Status::Success;
            return true;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool AssembleBoundClipBank(const BoundClipBank& bound, const ResourceIO::SkeletalAssetCreateContext& context,
                               C::TSharedPtr<SkeletalAssetResource>& out, RigV1Report& report)
    {
        return Detail::AssembleBoundClipBankWithProbe(bound, context, out, report, nullptr, nullptr);
    }
} // namespace NorvesLib::Core::Skeletal
