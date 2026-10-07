#include "Animation/RigSplitBinding.h"
#include "Animation/RigSplitBindingTestAccess.h"
#include "Animation/ClipBankRestComparison.h"
#include "Object/ResourceRegistry.h"
namespace NorvesLib::Core::Skeletal
{
    namespace C = Container;
    bool BindRigSplitV1(const SkeletonV1& skeleton, const SkinMeshV1& mesh, C::Span<const ClipBankV1> banks,
                        const RigBindingPolicy& policy, CookedRigSplitCpuAsset& out, RigSplitReport& report,
                        const RigV1Limits& limits)
    {
        report = {};
        try
        {
            if (!IsValidRigV1Limits(limits) || !IsValidRigBindingPolicy(policy) || banks.empty() || banks.size() > 16 ||
                !banks.data())
            {
                return false;
            }
            RigV1Report match;
            if (!MatchSkinMeshSkeletonV1(mesh, skeleton, match))
            {
                report.Status = match.Status;
                return false;
            }
            const auto* sk = skeleton.GetData();
            if (sk->Topology.Joints.size() > limits.MaxJoints)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            size_t clips = 0, snapshots = 0, channels = 0, samples = 0;
            for (size_t b = 0; b < banks.size(); ++b)
            {
                const auto* bank = banks[b].GetData();
                report.FailedBank = uint32_t(b);
                if (!bank)
                {
                    return false;
                }
                if (!SameRigTopology(bank->Topology, sk->Topology))
                {
                    report.Status = RigV1Status::TopologyMismatch;
                    return false;
                }
                if (bank->Clips.size() > limits.MaxClips - clips ||
                    bank->Snapshots.size() > limits.MaxSnapshots - snapshots)
                {
                    report.Status = RigV1Status::LimitExceeded;
                    return false;
                }
                clips += bank->Clips.size();
                snapshots += bank->Snapshots.size();
                for (const auto& clip : bank->Clips)
                {
                    if (clip.Channels.size() > limits.MaxChannels - channels)
                    {
                        report.Status = RigV1Status::LimitExceeded;
                        return false;
                    }
                    channels += clip.Channels.size();
                    for (const auto& channel : clip.Channels)
                    {
                        if (channel.Samples.size() > limits.MaxSamples - samples)
                        {
                            report.Status = RigV1Status::LimitExceeded;
                            return false;
                        }
                        samples += channel.Samples.size();
                    }
                }
            }
            auto data = C::MakeShared<RigSplitCpuData>();
            data->Skeleton = skeleton;
            data->Mesh = mesh;
            data->Clips.reserve(clips);
            data->Banks.reserve(banks.size());
            report.Banks.reserve(banks.size());
            for (size_t b = 0; b < banks.size(); ++b)
            {
                report.FailedBank = uint32_t(b);
                const auto& source = *banks[b].GetData();
                RigV1Report comparison;
                comparison.TargetLabel = sk->CurrentRest.Label;
                comparison.SkeletonId = sk->Topology.SkeletonId;
                comparison.BankPayloadHash = source.PayloadHash;
                comparison.Policy = policy;
                const bool bBound = Detail::CompareClipBankRest(
                    source, {sk->CurrentRest.Rest.data(), sk->CurrentRest.Rest.size()},
                    {sk->Topology.CanonicalToSource.data(), sk->Topology.CanonicalToSource.size()},
                    sk->CurrentRest.ResolvedImportScale, policy, comparison);
                const auto status = comparison.Status;
                report.Banks.push_back(std::move(comparison));
                if (!bBound)
                {
                    report.Status = status;
                    return false;
                }
                for (const auto& clip : source.Clips)
                {
                    for (const auto& old : data->Clips)
                    {
                        if (old.Name == clip.Name)
                        {
                            report.Status = RigV1Status::InvalidClip;
                            return false;
                        }
                    }
                    data->Clips.push_back(clip);
                }
                data->Banks.push_back(banks[b]);
            }
            report.FailedBank = UINT32_MAX;
            report.Status = RigV1Status::Success;
            data->BindingReport = report;
            CookedRigSplitCpuAsset candidate;
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
    bool Detail::AssembleRigSplitWithProbe(const CookedRigSplitCpuAsset& cpu,
                                           const ResourceIO::SkeletalAssetCreateContext& context,
                                           C::TSharedPtr<SkeletalAssetResource>& out, RigSplitReport& report,
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
            const auto* d = cpu.GetData();
            if (!d)
            {
                return false;
            }
            report = d->BindingReport;
            const auto fail = [&]()
            {
                report.Status = RigV1Status::ResourceFailure;
                return false;
            };
            auto& registry = *context.Registry;
            uint32_t ordinal = 0;
            const auto observe = [&](const C::TSharedPtr<Resource>& value)
            {
                if (!value || !value->GetResourceId() || (probe && !probe(ordinal, value, probeContext)))
                {
                    return false;
                }
                ++ordinal;
                return true;
            };
            auto mesh = registry.CreateResource<SkinnedMeshResource>(_T("rig-split-v1"));
            if (!observe(mesh) || !mesh->SetSplitMesh(d->Mesh) || !mesh->Load())
            {
                return fail();
            }
            auto skeleton = registry.CreateResource<SkeletonResource>(_T("rig-split-v1"));
            if (!observe(skeleton) || !skeleton->SetSplitSkeleton(d->Skeleton) || !skeleton->Load())
            {
                return fail();
            }
            C::VariableArray<C::TSharedPtr<AnimationClipResource>> clips;
            clips.reserve(d->Clips.size());
            for (const auto& value : d->Clips)
            {
                auto clip = registry.CreateResource<AnimationClipResource>(_T("rig-split-v1"));
                if (!observe(clip))
                {
                    return fail();
                }
                clip->SetClip(SkeletalAnimationClip(value));
                if (!clip->Load())
                {
                    return fail();
                }
                clips.push_back(std::move(clip));
            }
            auto asset = registry.CreateResource<SkeletalAssetResource>(_T("rig-split-v1"));
            if (!observe(asset))
            {
                return fail();
            }
            asset->SetClipResources(mesh, skeleton, clips);
            if (!asset->Load())
            {
                return fail();
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
    bool AssembleRigSplitV1(const CookedRigSplitCpuAsset& cpu, const ResourceIO::SkeletalAssetCreateContext& context,
                            C::TSharedPtr<SkeletalAssetResource>& out, RigSplitReport& report)
    {
        return Detail::AssembleRigSplitWithProbe(cpu, context, out, report, nullptr, nullptr);
    }
} // namespace NorvesLib::Core::Skeletal
