#include "Resource/RigSplitAssetLoader.h"
#include <algorithm>
namespace NorvesLib::Core::ResourceIO
{
    namespace C = Container;
    namespace S = Skeletal;
    bool IsRigSplitReferenceFormat(const Asset::AssetCookedReference& r, uint32_t role)
    {
        if (!r.bHasRigSplitMetadata || r.RigSplitMetadata.Role != role || r.CookedVersion != 1)
        {
            return false;
        }
        if (!S::IsSplitLogicalPath(r.CookedPackage) || !S::IsSplitLogicalPath(r.EntryName))
        {
            return false;
        }
        if (role == 1)
        {
            return r.Kind == Asset::AssetKind::Skeleton && r.Format == "nvskel.v1.skeleton" &&
                   r.EntryType == Asset::MakeAssetPackageFourCC('S', 'k', 'e', '1');
        }
        if (role == 2)
        {
            return r.Kind == Asset::AssetKind::Model && r.Format == "nvskel.v1.skinmesh.pnujiw.u32" &&
                   r.EntryType == Asset::MakeAssetPackageFourCC('S', 'k', 'm', '1');
        }
        return r.Kind == Asset::AssetKind::Animation && r.Format == "nvskel.v1.clips" &&
               r.EntryType == Asset::MakeAssetPackageFourCC('A', 'n', 'm', '1');
    }
    namespace
    {
        bool CommonMetadata(const Asset::AssetRigSplitMetadata& m, const S::RigTopology& topology)
        {
            return m.Profile == 1 && m.SkeletonId == topology.SkeletonId && m.JointCount == topology.Joints.size();
        }
    } // namespace
    bool LoadRigSplitForWorker(const RigSplitLoadPlan& plan, S::CookedRigSplitCpuAsset& out, RigSplitLoadReport& report,
                               RigSplitLoadEvidence* evidence)
    {
        report = {};
        try
        {
            if (!plan.Assets || !S::IsValidRigV1Limits(plan.Limits) || !S::IsValidRigBindingPolicy(plan.Policy) ||
                !S::IsSplitLogicalPath(plan.SkeletonPath) || !S::IsSplitLogicalPath(plan.MeshPath) ||
                plan.SkeletonPath == plan.MeshPath || plan.BankPaths.empty() || plan.BankPaths.size() > 16 ||
                !S::IsSplitLogicalPath(plan.Variant) || !plan.MaxPackageBytes ||
                plan.MaxPackageBytes > 68ull * 1024 * 1024 || !plan.MaxTotalPackageBytes ||
                plan.MaxTotalPackageBytes > 256ull * 1024 * 1024)
            {
                return false;
            }
            for (size_t i = 0; i < plan.BankPaths.size(); ++i)
            {
                const auto& path = plan.BankPaths[i];
                if (!S::IsSplitLogicalPath(path) || path == plan.SkeletonPath || path == plan.MeshPath)
                {
                    return false;
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (path == plan.BankPaths[j])
                    {
                        return false;
                    }
                }
            }
            RigSplitLoadEvidence captured;
            if (evidence)
            {
                captured.Entries.reserve(plan.BankPaths.size() + 2);
            }
            const auto read =
                [&](const C::AnsiString& path, Asset::AssetKind kind, uint32_t role, Asset::AssetResolveResult& result)
            {
                report.LogicalPath = path;
                const auto reference = plan.Assets->FindCookedVariant(path, kind, plan.Variant);
                if (!reference.ShouldUseCooked())
                {
                    report.Status = RigSplitLoadStatus::ResolveRejected;
                    return false;
                }
                if (!IsRigSplitReferenceFormat(reference.Reference, role))
                {
                    report.Status = RigSplitLoadStatus::FormatRejected;
                    return false;
                }
                Asset::AssetResolveRequest request;
                request.LogicalPath = path;
                request.Kind = kind;
                request.Variant = plan.Variant;
                request.FallbackMode = Asset::AssetFallbackMode::FailOnCookedFailure;
                request.MaxCookedPackageBytes =
                    std::min(plan.MaxPackageBytes, plan.MaxTotalPackageBytes - report.PackageBytesRead);
                result = plan.Assets->ResolveAsset(request);
                report.ResolveStatus = result.Status;
                report.PackageReadStatus = result.PackageReadStatus;
                report.PackageBytesRead += result.PackageBytesRead;
                if (!result.UsedCooked() || result.NormalizedLogicalPath != path)
                {
                    report.Status = RigSplitLoadStatus::ResolveRejected;
                    return false;
                }
                if (evidence)
                {
                    captured.Entries.push_back({result.CookedReference, S::RigBytesHash(result.Blob.GetSpan())});
                }
                return true;
            };
            S::SkeletonV1 skeleton;
            {
                Asset::AssetResolveResult result;
                if (!read(plan.SkeletonPath, Asset::AssetKind::Skeleton, 1, result))
                {
                    return false;
                }
                if (!S::ParseSkeletonV1(result.Blob.GetSpan(), skeleton, report.ParseReport, plan.Limits))
                {
                    report.Status = RigSplitLoadStatus::ParseRejected;
                    return false;
                }
                if (!CommonMetadata(result.CookedReference.RigSplitMetadata, skeleton.GetData()->Topology))
                {
                    report.Status = RigSplitLoadStatus::MetadataMismatch;
                    return false;
                }
            }
            S::SkinMeshV1 mesh;
            {
                Asset::AssetResolveResult result;
                if (!read(plan.MeshPath, Asset::AssetKind::Model, 2, result))
                {
                    return false;
                }
                if (!S::ParseSkinMeshV1(result.Blob.GetSpan(), mesh, report.ParseReport, plan.Limits))
                {
                    report.Status = RigSplitLoadStatus::ParseRejected;
                    return false;
                }
                const auto& d = *mesh.GetData();
                const auto& m = result.CookedReference.RigSplitMetadata;
                if (d.SkeletonPath != plan.SkeletonPath || !CommonMetadata(m, d.Topology) ||
                    m.VertexCount != d.Vertices.size() || m.IndexCount != d.Indices.size() ||
                    m.SubmeshCount != d.SubMeshes.size() || m.MaterialSlotCount != d.Slots.size() ||
                    m.MaterialCount != d.Materials.size())
                {
                    report.Status = RigSplitLoadStatus::MetadataMismatch;
                    return false;
                }
            }
            // 全bankをcopyする前の累積数量。parse自体にも残量を渡す。
            C::VariableArray<S::ClipBankV1> banks;
            banks.reserve(plan.BankPaths.size());
            uint32_t clips = 0, snapshots = 0, channels = 0, samples = 0;
            for (const auto& path : plan.BankPaths)
            {
                Asset::AssetResolveResult result;
                if (!read(path, Asset::AssetKind::Animation, 3, result))
                {
                    return false;
                }
                const auto& metadata = result.CookedReference.RigSplitMetadata;
                if (metadata.ClipCount > plan.Limits.MaxClips - clips ||
                    metadata.SnapshotCount > plan.Limits.MaxSnapshots - snapshots ||
                    metadata.ChannelCount > plan.Limits.MaxChannels - channels ||
                    metadata.SampleCount > plan.Limits.MaxSamples - samples)
                {
                    report.Status = RigSplitLoadStatus::ParseRejected;
                    report.ParseReport.Status = S::RigV1Status::LimitExceeded;
                    return false;
                }
                S::RigV1Limits remaining = plan.Limits;
                remaining.MaxClips -= clips;
                remaining.MaxSnapshots -= snapshots;
                // zero channelのbankを受理できるよう、0残量はmetadata実数照合と後段の全体bindで止める。
                remaining.MaxChannels = std::max<uint32_t>(1, remaining.MaxChannels - channels);
                remaining.MaxSamples = std::max<uint32_t>(1, remaining.MaxSamples - samples);
                S::ClipBankV1 bank;
                if (!S::ParseClipBankV1(result.Blob.GetSpan(), bank, report.ParseReport, remaining))
                {
                    report.Status = RigSplitLoadStatus::ParseRejected;
                    return false;
                }
                const auto& d = *bank.GetData();
                uint32_t actualChannels = 0, actualSamples = 0;
                for (const auto& clip : d.Clips)
                {
                    actualChannels += uint32_t(clip.Channels.size());
                    for (const auto& channel : clip.Channels)
                    {
                        actualSamples += uint32_t(channel.Samples.size());
                    }
                }
                if (!CommonMetadata(metadata, d.Topology) || metadata.ClipCount != d.Clips.size() ||
                    metadata.SnapshotCount != d.Snapshots.size() || metadata.ChannelCount != actualChannels ||
                    metadata.SampleCount != actualSamples)
                {
                    report.Status = RigSplitLoadStatus::MetadataMismatch;
                    return false;
                }
                clips += metadata.ClipCount;
                snapshots += metadata.SnapshotCount;
                channels += actualChannels;
                samples += actualSamples;
                banks.push_back(std::move(bank));
            }
            S::CookedRigSplitCpuAsset candidate;
            if (!S::BindRigSplitV1(skeleton, mesh, {banks.data(), banks.size()}, plan.Policy, candidate,
                                   report.BindingReport, plan.Limits))
            {
                report.Status = RigSplitLoadStatus::BindingRejected;
                return false;
            }
            if (evidence)
            {
                evidence->Entries.swap(captured.Entries);
            }
            out = std::move(candidate);
            report.Status = RigSplitLoadStatus::Success;
            return true;
        }
        catch (...)
        {
            report.Status = RigSplitLoadStatus::Exception;
            return false;
        }
    }
} // namespace NorvesLib::Core::ResourceIO
