#include "Resource/RigSplitPublicationIdentity.h"
#include <bit>
#include <algorithm>
namespace NorvesLib::Core::ResourceIO
{
    namespace C = Container;
    namespace S = Skeletal;
    namespace
    {
        // 一回目は長さだけを調べ、二回目だけ確保済み文字列へ書く。全数値は16桁固定hex。
        struct Encoder
        {
            size_t Size = 0, Limit = 0;
            C::String* Output = nullptr;
            bool bGood = true;
            bool Reserve(size_t n)
            {
                if (!bGood || Size > Limit || n > Limit - Size)
                {
                    bGood = false;
                    return false;
                }
                Size += n;
                return true;
            }
            void Number(uint64_t value)
            {
                if (!Reserve(16))
                {
                    return;
                }
                if (Output)
                {
                    constexpr char digits[] = "0123456789abcdef";
                    for (int i = 15; i >= 0; --i)
                    {
                        Output->push_back(static_cast<C::String::value_type>(digits[(value >> (i * 4)) & 15]));
                    }
                }
            }
            void Text(C::AnsiStringView text)
            {
                Number(text.size());
                if (!Reserve(text.size()))
                {
                    return;
                }
                if (Output)
                {
                    for (unsigned char ch : text)
                    {
                        Output->push_back(static_cast<C::String::value_type>(ch));
                    }
                }
            }
        };
        bool AsciiField(const C::AnsiString& text, size_t maximum = 4096)
        {
            if (text.size() > maximum)
            {
                return false;
            }
            for (unsigned char ch : text)
            {
                if (ch < 0x20 || ch > 0x7e)
                {
                    return false;
                }
            }
            return true;
        }
        template <typename E> void Reference(E& e, const Asset::AssetCookedReference& r)
        {
            e.Text(r.LogicalPath);
            e.Number(uint64_t(r.Kind));
            e.Number(r.SourceHash);
            e.Text(r.SourceHashHex);
            e.Text(r.Variant);
            e.Text(r.Format);
            e.Text(r.CookedPackage);
            e.Text(r.EntryName);
            e.Number(r.EntryType);
            e.Text(r.EntryTypeText);
            e.Number(r.CookedHash);
            e.Text(r.CookedHashHex);
            e.Number(r.CookedVersion);
            e.Number(r.bHasSkeletalMetadata);
            const auto& a = r.SkeletalMetadata;
            e.Number(a.VertexCount);
            e.Number(a.IndexCount);
            e.Number(a.JointCount);
            e.Number(a.ClipCount);
            e.Number(a.bHasSubmeshCounts);
            e.Number(a.SubmeshCount);
            e.Number(a.MaterialSlotCount);
            e.Number(r.bHasRigSplitMetadata);
            const auto& m = r.RigSplitMetadata;
            e.Number(m.Role);
            e.Number(m.Profile);
            e.Number(m.SkeletonId);
            e.Number(m.JointCount);
            e.Number(m.VertexCount);
            e.Number(m.IndexCount);
            e.Number(m.SubmeshCount);
            e.Number(m.MaterialSlotCount);
            e.Number(m.MaterialCount);
            e.Number(m.ClipCount);
            e.Number(m.SnapshotCount);
            e.Number(m.ChannelCount);
            e.Number(m.SampleCount);
        }
        bool BoundedReference(const Asset::AssetCookedReference& r)
        {
            return AsciiField(r.LogicalPath) && AsciiField(r.SourceHashHex, 16) && AsciiField(r.Variant) &&
                   AsciiField(r.Format) && AsciiField(r.CookedPackage) && AsciiField(r.EntryName) &&
                   AsciiField(r.EntryTypeText, 4) && AsciiField(r.CookedHashHex, 16);
        }
        void Request(Encoder& e, const S::RigSplitRequest& q, bool bFull)
        {
            e.Text("rig_split_v1");
            e.Number(1);
            e.Text(q.Variant);
            e.Text(q.SkeletonPath);
            e.Text(q.MeshPath);
            e.Number(q.BankPaths.size());
            for (const auto& path : q.BankPaths)
            {
                e.Text(path);
            }
            if (!bFull)
            {
                return;
            }
            e.Number(1);
            e.Number(q.Policy.bAllowRestMismatch);
            const auto& t = q.Policy.Tolerance;
            e.Number(std::bit_cast<uint64_t>(t.TranslationMeters == 0 ? 0.0 : t.TranslationMeters));
            e.Number(std::bit_cast<uint64_t>(t.RotationRadians == 0 ? 0.0 : t.RotationRadians));
            e.Number(std::bit_cast<uint64_t>(t.LogScale == 0 ? 0.0 : t.LogScale));
            const auto& l = q.Limits;
            e.Number(l.MaxJoints);
            e.Number(l.MaxClips);
            e.Number(l.MaxSnapshots);
            e.Number(l.MaxChannels);
            e.Number(l.MaxSamples);
            e.Number(l.MaxNameBytes);
            e.Number(l.MaxStringBytes);
            e.Number(l.MaxWireBytes);
            e.Number(l.MaxSourceBytes);
            e.Number(l.MaxBufferBytes);
            e.Number(l.MaxNodes);
            e.Number(l.MaxAccessors);
            e.Number(l.MaxBuffers);
            e.Number(l.MaxVertices);
            e.Number(l.MaxIndices);
            e.Number(q.MaxPackageBytes);
            e.Number(q.MaxTotalPackageBytes);
        }
        const Asset::AssetCookedReference* BorrowReference(const Asset::AssetSystem& assets, const C::AnsiString& path,
                                                           Asset::AssetKind kind, const C::AnsiString& variant)
        {
            // 構文検証済み正準論理pathを照合する。巨大な参照をcopyしてから長さ検査しない。
            for (size_t i = 0; i < assets.GetAssetCount(); ++i)
            {
                const auto& r = assets.GetAssetReference(i);
                if (r.LogicalPath == path && r.Kind == kind && r.Variant == variant)
                {
                    return &r;
                }
            }
            return nullptr;
        }
        bool ValidRequest(const S::RigSplitRequest& q)
        {
            if (!S::IsValidRigV1Limits(q.Limits) || !S::IsValidRigBindingPolicy(q.Policy) ||
                !S::IsSplitLogicalPath(q.SkeletonPath) || !S::IsSplitLogicalPath(q.MeshPath) ||
                q.SkeletonPath == q.MeshPath || !S::IsSplitLogicalPath(q.Variant) || q.BankPaths.empty() ||
                q.BankPaths.size() > 16 || !q.MaxPackageBytes || q.MaxPackageBytes > 68ull * 1024 * 1024 ||
                !q.MaxTotalPackageBytes || q.MaxTotalPackageBytes > 256ull * 1024 * 1024)
            {
                return false;
            }
            for (size_t i = 0; i < q.BankPaths.size(); ++i)
            {
                const auto& p = q.BankPaths[i];
                if (!S::IsSplitLogicalPath(p) || p == q.SkeletonPath || p == q.MeshPath)
                {
                    return false;
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (p == q.BankPaths[j])
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        bool SameClip(const S::SkeletalAnimationClip& a, const S::SkeletalAnimationClip& b) noexcept
        {
            const auto bits = [](float v) { return std::bit_cast<uint32_t>(v); };
            if (a.Name != b.Name || bits(a.DurationSeconds) != bits(b.DurationSeconds) ||
                a.Channels.size() != b.Channels.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.Channels.size(); ++i)
            {
                const auto& x = a.Channels[i];
                const auto& y = b.Channels[i];
                if (x.JointIndex != y.JointIndex || x.Path != y.Path || x.Interpolation != y.Interpolation ||
                    x.Samples.size() != y.Samples.size())
                {
                    return false;
                }
                for (size_t k = 0; k < x.Samples.size(); ++k)
                {
                    const auto& p = x.Samples[k];
                    const auto& q = y.Samples[k];
                    if (bits(p.TimeSeconds) != bits(q.TimeSeconds) || bits(p.Value.X) != bits(q.Value.X) ||
                        bits(p.Value.Y) != bits(q.Value.Y) || bits(p.Value.Z) != bits(q.Value.Z) ||
                        bits(p.Value.W) != bits(q.Value.W))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        size_t ClipBytes(const S::SkeletalAnimationClip& clip)
        {
            size_t n = clip.Name.size() * sizeof(C::String::value_type) +
                       clip.Channels.capacity() * sizeof(S::SkeletalAnimationChannel);
            for (const auto& c : clip.Channels)
            {
                n += c.Samples.capacity() * sizeof(S::SkeletalAnimationSample);
            }
            return n;
        }
        size_t ReportBytes(const S::RigSplitReport& report)
        {
            size_t n = report.Banks.capacity() * sizeof(S::RigV1Report);
            for (const auto& b : report.Banks)
            {
                n += b.TargetLabel.size() + b.Snapshots.capacity() * sizeof(S::RigSnapshotComparison) +
                     b.Differences.capacity() * sizeof(S::RigJointDifference);
                for (const auto& s : b.Snapshots)
                {
                    n += s.AuthorLabel.size();
                }
                for (const auto& d : b.Differences)
                {
                    n += d.Name.size();
                }
            }
            return n;
        }
        size_t ReferenceBytes(const Asset::AssetCookedReference& r)
        {
            return r.LogicalPath.size() + r.SourceHashHex.size() + r.Variant.size() + r.Format.size() +
                   r.CookedPackage.size() + r.EntryName.size() + r.EntryTypeText.size() + r.CookedHashHex.size();
        }
        size_t ReceiptBytes(const RigSplitPublicationReceipt& r)
        {
            const auto& d = *r.GetIdentity().GetData();
            const auto& cpu = *r.GetCpu().GetData();
            size_t n = sizeof(r) + sizeof(d) + sizeof(cpu) + sizeof(RigSplitAssetDiagnostics);
            n += (d.Key.size() + d.BundleUri.size()) * sizeof(C::String::value_type);
            n += d.Plan.SkeletonPath.size() + d.Plan.MeshPath.size() + d.Plan.Variant.size();
            n += d.Plan.BankPaths.capacity() * sizeof(C::AnsiString);
            for (const auto& p : d.Plan.BankPaths)
            {
                n += p.size();
            }
            n += d.References.capacity() * sizeof(Asset::AssetCookedReference);
            for (const auto& ref : d.References)
            {
                n += ReferenceBytes(ref);
            }
            n += r.GetEvidence().Entries.capacity() * sizeof(RigSplitResolvedEntry);
            for (const auto& e : r.GetEvidence().Entries)
            {
                n += ReferenceBytes(e.Reference);
            }
            n += cpu.Clips.capacity() * sizeof(S::SkeletalAnimationClip) + cpu.Banks.capacity() * sizeof(S::ClipBankV1);
            for (const auto& c : cpu.Clips)
            {
                n += ClipBytes(c);
            }
            for (const auto& bank : cpu.Banks)
            {
                const auto& b = *bank.GetData();
                n += sizeof(b) + b.Topology.CanonicalBytes.capacity();
                n += b.Topology.Joints.capacity() * sizeof(S::RigTopologyJoint);
                n += (b.Topology.SourceToCanonical.capacity() + b.Topology.CanonicalToSource.capacity()) *
                     sizeof(uint32_t);
                for (const auto& j : b.Topology.Joints)
                {
                    n += j.Name.size();
                }
                n +=
                    b.Snapshots.capacity() * sizeof(S::RigClipSnapshot) + b.ClipSnapshots.capacity() * sizeof(uint32_t);
                for (const auto& s : b.Snapshots)
                {
                    n += s.Label.size() + s.Rest.capacity() * sizeof(S::SkeletalRestTransform);
                }
                n += b.Clips.capacity() * sizeof(S::SkeletalAnimationClip);
                for (const auto& c : b.Clips)
                {
                    n += ClipBytes(c);
                }
            }
            n += ReportBytes(cpu.BindingReport) + ReportBytes(r.GetDiagnostics()->Load.BindingReport) +
                 ReportBytes(r.GetDiagnostics()->Assembly) + r.GetDiagnostics()->Load.LogicalPath.size();
            // childが持つimmutable Skeleton/Mesh本文と、外部snapshotのmanifestは二重加算しない。
            return n;
        }
    } // namespace
    RigSplitIdentityStatus BuildRigSplitRequestIdentity(const S::RigSplitRequest& q,
                                                        C::TSharedPtr<const Asset::AssetSystem> assets,
                                                        uint64_t session, uint64_t domain, uint64_t generation,
                                                        size_t limit, RigSplitRequestIdentity& out)
    {
        try
        {
            if (!assets || !session || !domain || !generation || !ValidRequest(q))
            {
                return RigSplitIdentityStatus::InvalidRequest;
            }
            if (!limit || limit > RigSplitMaximumKeyBytes)
            {
                return RigSplitIdentityStatus::LimitExceeded;
            }
            C::FixedArray<const Asset::AssetCookedReference*, 18> refs{};
            refs[0] = BorrowReference(*assets, q.SkeletonPath, Asset::AssetKind::Skeleton, q.Variant);
            refs[1] = BorrowReference(*assets, q.MeshPath, Asset::AssetKind::Model, q.Variant);
            for (size_t i = 0; i < q.BankPaths.size(); ++i)
            {
                refs[i + 2] = BorrowReference(*assets, q.BankPaths[i], Asset::AssetKind::Animation, q.Variant);
            }
            bool bCanCache = true;
            for (size_t i = 0; i < q.BankPaths.size() + 2; ++i)
            {
                if (refs[i] && !BoundedReference(*refs[i]))
                {
                    return RigSplitIdentityStatus::LimitExceeded;
                }
                if (!refs[i] || !IsRigSplitReferenceFormat(*refs[i], i < 2 ? uint32_t(i + 1) : 3))
                {
                    bCanCache = false;
                }
            }
            const auto key = [&](Encoder& e)
            {
                e.Number(session);
                e.Number(domain);
                e.Number(generation);
                Request(e, q, true);
                e.Number(bCanCache);
                for (size_t i = 0; i < q.BankPaths.size() + 2; ++i)
                {
                    e.Number(refs[i] != nullptr);
                    if (refs[i])
                    {
                        Reference(e, *refs[i]);
                    }
                }
            };
            Encoder measure{0, limit};
            key(measure);
            Encoder uriMeasure{0, limit};
            Request(uriMeasure, q, false);
            if (!measure.bGood || !uriMeasure.bGood)
            {
                return RigSplitIdentityStatus::LimitExceeded;
            }
            auto data = C::MakeShared<RigSplitRequestIdentityData>();
            data->Key.reserve(measure.Size);
            data->BundleUri.reserve(uriMeasure.Size);
            Encoder encoded{0, limit, &data->Key};
            key(encoded);
            Encoder uri{0, limit, &data->BundleUri};
            Request(uri, q, false);
            if (!encoded.bGood || !uri.bGood || encoded.Size != measure.Size || uri.Size != uriMeasure.Size)
            {
                return RigSplitIdentityStatus::InvalidRequest;
            }
            data->Plan = {assets,   q.SkeletonPath, q.MeshPath,        q.BankPaths,           q.Variant,
                          q.Policy, q.Limits,       q.MaxPackageBytes, q.MaxTotalPackageBytes};
            auto& t = data->Plan.Policy.Tolerance;
            if (t.TranslationMeters == 0)
            {
                t.TranslationMeters = 0;
            }
            if (t.RotationRadians == 0)
            {
                t.RotationRadians = 0;
            }
            if (t.LogScale == 0)
            {
                t.LogScale = 0;
            }
            data->References.reserve(q.BankPaths.size() + 2);
            for (size_t i = 0; i < q.BankPaths.size() + 2; ++i)
            {
                data->References.push_back(refs[i] ? *refs[i] : Asset::AssetCookedReference{});
            }
            data->Session = session;
            data->Domain = domain;
            data->Generation = generation;
            data->bCanCache = bCanCache;
            RigSplitRequestIdentity candidate;
            candidate.m_Data = std::move(data);
            out = std::move(candidate);
            return RigSplitIdentityStatus::Success;
        }
        catch (...)
        {
            return RigSplitIdentityStatus::Exception;
        }
    }
    bool SameRigSplitRequestIdentity(const RigSplitRequestIdentity& a, const RigSplitRequestIdentity& b) noexcept
    {
        const auto* x = a.GetData();
        const auto* y = b.GetData();
        return x && y && x->Plan.Assets == y->Plan.Assets && x->Key == y->Key && x->BundleUri == y->BundleUri &&
               x->Session == y->Session && x->Domain == y->Domain && x->Generation == y->Generation &&
               x->bCanCache == y->bCanCache;
    }
    bool SameRigSplitReference(const Asset::AssetCookedReference& a, const Asset::AssetCookedReference& b) noexcept
    {
        const auto& x = a.SkeletalMetadata;
        const auto& y = b.SkeletalMetadata;
        const auto& p = a.RigSplitMetadata;
        const auto& q = b.RigSplitMetadata;
        return a.LogicalPath == b.LogicalPath && a.Kind == b.Kind && a.SourceHash == b.SourceHash &&
               a.SourceHashHex == b.SourceHashHex && a.Variant == b.Variant && a.Format == b.Format &&
               a.CookedPackage == b.CookedPackage && a.EntryName == b.EntryName && a.EntryType == b.EntryType &&
               a.EntryTypeText == b.EntryTypeText && a.CookedHash == b.CookedHash &&
               a.CookedHashHex == b.CookedHashHex && a.CookedVersion == b.CookedVersion &&
               a.bHasSkeletalMetadata == b.bHasSkeletalMetadata && a.bHasRigSplitMetadata == b.bHasRigSplitMetadata &&
               x.VertexCount == y.VertexCount && x.IndexCount == y.IndexCount && x.JointCount == y.JointCount &&
               x.ClipCount == y.ClipCount && x.bHasSubmeshCounts == y.bHasSubmeshCounts &&
               x.SubmeshCount == y.SubmeshCount && x.MaterialSlotCount == y.MaterialSlotCount && p.Role == q.Role &&
               p.Profile == q.Profile && p.SkeletonId == q.SkeletonId && p.JointCount == q.JointCount &&
               p.VertexCount == q.VertexCount && p.IndexCount == q.IndexCount && p.SubmeshCount == q.SubmeshCount &&
               p.MaterialSlotCount == q.MaterialSlotCount && p.MaterialCount == q.MaterialCount &&
               p.ClipCount == q.ClipCount && p.SnapshotCount == q.SnapshotCount && p.ChannelCount == q.ChannelCount &&
               p.SampleCount == q.SampleCount;
    }
    bool LoadRigSplitForPublication(const RigSplitRequestIdentity& identity,
                                    C::TSharedPtr<const RigSplitPublicationReceipt>& out,
                                    RigSplitAssetDiagnostics& diagnostics)
    {
        diagnostics = {};
        try
        {
            const auto* d = identity.GetData();
            if (!d)
            {
                return false;
            }
            auto value = C::MakeShared<RigSplitPublicationReceipt>();
            value->m_Identity = identity;
            if (!LoadRigSplitForWorker(d->Plan, value->m_Cpu, diagnostics.Load, &value->m_Evidence))
            {
                return false;
            }
            if (!d->bCanCache || value->m_Evidence.Entries.size() != d->References.size())
            {
                diagnostics.Load.Status = RigSplitLoadStatus::MetadataMismatch;
                return false;
            }
            for (size_t i = 0; i < d->References.size(); ++i)
            {
                const auto& actual = value->m_Evidence.Entries[i];
                if (!SameRigSplitReference(d->References[i], actual.Reference) ||
                    actual.FullBlobHash != actual.Reference.CookedHash)
                {
                    diagnostics.Load.Status = RigSplitLoadStatus::MetadataMismatch;
                    return false;
                }
            }
            const auto* cpu = value->m_Cpu.GetData();
            if (cpu->Skeleton.GetData()->ContentHash != value->m_Evidence.Entries[0].FullBlobHash ||
                cpu->Mesh.GetData()->ContentHash != value->m_Evidence.Entries[1].FullBlobHash)
            {
                diagnostics.Load.Status = RigSplitLoadStatus::MetadataMismatch;
                return false;
            }
            value->m_Diagnostics = C::MakeShared<const RigSplitAssetDiagnostics>(diagnostics);
            value->m_OwnedBytes = ReceiptBytes(*value);
            out = std::move(value);
            return true;
        }
        catch (...)
        {
            diagnostics.Load.Status = RigSplitLoadStatus::Exception;
            return false;
        }
    }
    C::TSharedPtr<const RigSplitPublicationReceipt> CompleteRigSplitOwnerReceipt(
        const RigSplitPublicationReceipt& source, const S::RigSplitReport& assembly)
    {
        if (!source.IsValid())
        {
            return {};
        }
        auto value = C::MakeShared<RigSplitPublicationReceipt>(source);
        auto diagnostics = C::MakeShared<RigSplitAssetDiagnostics>(*source.m_Diagnostics);
        diagnostics->Assembly = assembly;
        value->m_Diagnostics = std::move(diagnostics);
        value->m_OwnedBytes = ReceiptBytes(*value);
        return value;
    }
    void RigSplitAssetAccess::Attach(SkeletalAssetResource& asset,
                                     C::TSharedPtr<const RigSplitPublicationReceipt> receipt)
    {
        asset.m_SplitReceipt = std::move(receipt);
    }
    const C::TSharedPtr<const RigSplitPublicationReceipt>& RigSplitAssetAccess::Get(const SkeletalAssetResource& asset)
    {
        return asset.m_SplitReceipt;
    }
    size_t RigSplitAssetAccess::MemorySize(const SkeletalAssetResource& asset) noexcept
    {
        return asset.m_SplitReceipt ? asset.m_SplitReceipt->m_OwnedBytes : 0;
    }
    bool RigSplitAssetAccess::Matches(const SkeletalAssetResource& asset,
                                      const RigSplitRequestIdentity& identity) noexcept
    {
        const auto& receipt = asset.m_SplitReceipt;
        if (!receipt || !SameRigSplitRequestIdentity(receipt->m_Identity, identity) || !identity.GetData()->bCanCache)
        {
            return false;
        }
        const auto* cpu = receipt->m_Cpu.GetData();
        if (!cpu || !asset.GetMesh() || !asset.GetSkeleton() ||
            asset.GetResourcePath() != identity.GetData()->BundleUri ||
            asset.GetMesh()->GetSplitMesh() != cpu->Mesh.GetData() ||
            asset.GetSkeleton()->GetSplitSkeleton() != cpu->Skeleton.GetData() ||
            asset.GetMesh()->GetRenderAssetLease() || asset.GetClipCount() != cpu->Clips.size())
        {
            return false;
        }
        for (size_t i = 0; i < cpu->Clips.size(); ++i)
        {
            if (!asset.GetClip(i) || !SameClip(asset.GetClip(i)->GetClip(), cpu->Clips[i]))
            {
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::ResourceIO
