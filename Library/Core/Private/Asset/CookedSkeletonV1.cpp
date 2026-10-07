#include "Asset/CookedSkeletonV1.h"
#include "Asset/RigSplitWire.h"
#include "Asset/RigSplitAllocationTestAccess.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <bit>
#include <cmath>
namespace NorvesLib::Core::Skeletal
{
    namespace C = Container;
    namespace W = SplitWire;
    namespace
    {
        constexpr uint32_t Codes[] = {W::Four('S', 'T', 'R', 'S'), W::Four('T', 'J', 'N', 'T'),
                                      W::Four('R', 'S', 'E', 'T'), W::Four('A', 'R', 'S', 'T'),
                                      W::Four('R', 'O', 'O', 'T')};
        constexpr uint32_t Records[] = {1, 24, 48, 48, 64};
        RigV1Status Encode(const SkeletonV1Data& d, W::Bytes& out, const RigV1Limits& limits)
        {
            if (!IsValidRigV1Limits(limits))
            {
                return RigV1Status::InvalidInput;
            }
            const size_t count = d.Topology.Joints.size();
            if (!count || count > limits.MaxJoints || d.CurrentRest.Rest.size() != count)
            {
                return RigV1Status::LimitExceeded;
            }
            if (!W::ValidName(d.CurrentRest.Label, limits))
            {
                return RigV1Status::InvalidName;
            }
            uint64_t stringBytes = d.CurrentRest.Label.size();
            for (const auto& j : d.Topology.Joints)
            {
                if (!W::ValidName(j.Name, limits))
                {
                    return RigV1Status::InvalidName;
                }
                stringBytes += j.Name.size();
            }
            if (stringBytes > limits.MaxStringBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            const uint64_t sizes[] = {stringBytes, count * 24, 48, count * 48, 64};
            uint64_t total = 416;
            for (const auto size : sizes)
            {
                total = ((total + 15) & ~uint64_t{15}) + size;
            }
            if (total > limits.MaxWireBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            W::OutputSection sections[5];
            for (size_t i = 0; i < 5; ++i)
            {
                sections[i].Code = Codes[i];
                sections[i].Record = Records[i];
            }
            W::WriteTopology(d.Topology, sections[0].Data, sections[1].Data);
            auto& set = sections[2].Data;
            set.resize(48, 0);
            W::W32(set, 4, uint32_t(count));
            W::W64(set, 8, W::AppendName(sections[0].Data, d.CurrentRest.Label));
            W::W32(set, 16, uint32_t(d.CurrentRest.Label.size()));
            W::W32(set, 20, 1);
            W::W64(set, 24, d.CurrentRest.RestHash);
            W::W64(set, 32, std::bit_cast<uint64_t>(d.CurrentRest.ResolvedImportScale));
            sections[3].Data.resize(count * 48, 0);
            for (size_t i = 0; i < count; ++i)
            {
                W::WriteRest(sections[3].Data, i * 48, d.CurrentRest.Rest[i]);
            }
            sections[4].Data.resize(64, 0);
            for (size_t i = 0; i < 16; ++i)
            {
                W::WF(sections[4].Data, i * 4, d.RootTransform[i]);
            }
            return W::WriteEnvelope(1, d.Topology.SkeletonId, {sections, 5}, out, limits);
        }
    } // namespace
    bool BuildSkeletonV1(const RigAuthoringCpu& source, SkeletonV1& out, RigV1Report& report, const RigV1Limits& limits)
    {
        report = {};
        try
        {
            const auto* rig = source.GetData();
            if (!rig || !IsValidRigV1Limits(limits))
            {
                return false;
            }
            if (rig->Topology.Joints.size() > limits.MaxJoints)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            uint64_t names = rig->SourceLabel.size();
            if (!W::ValidName(rig->SourceLabel, limits))
            {
                report.Status = RigV1Status::InvalidName;
                return false;
            }
            for (const auto& joint : rig->Topology.Joints)
            {
                if (!W::ValidName(joint.Name, limits))
                {
                    report.Status = RigV1Status::InvalidName;
                    return false;
                }
                names += joint.Name.size();
            }
            uint64_t bytesRequired = 416;
            const uint64_t sizes[] = {names, rig->Topology.Joints.size() * 24, 48, rig->Topology.Joints.size() * 48,
                                      64};
            for (auto bytes : sizes)
            {
                bytesRequired = ((bytesRequired + 15) & ~uint64_t{15}) + bytes;
            }
            if (names > limits.MaxStringBytes || bytesRequired > limits.MaxWireBytes)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            SkeletonV1Data data;
            data.Topology = rig->Topology;
            data.CurrentRest.Label = rig->SourceLabel;
            data.CurrentRest.ResolvedImportScale = rig->ResolvedImportScale;
            data.CurrentRest.Rest.reserve(rig->LocalRest.size());
            for (auto index : rig->Topology.CanonicalToSource)
            {
                data.CurrentRest.Rest.push_back(rig->LocalRest[index]);
            }
            data.CurrentRest.RestHash = RigRestHash({data.CurrentRest.Rest.data(), data.CurrentRest.Rest.size()});
            W::Bytes bytes;
            report.Status = Encode(data, bytes, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            // 公開値は必ず正準順、完全wire hash付き。source順の写像やgeometryを持ち越さない。
            return ParseSkeletonV1(W::ViewOf(bytes), out, report, limits);
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool WriteSkeletonV1(const SkeletonV1& skeleton, W::Bytes& out, RigV1Report& report, const RigV1Limits& limits)
    {
        report = {};
        try
        {
            const auto* d = skeleton.GetData();
            if (!d)
            {
                return false;
            }
            report.Status = Encode(*d, out, limits);
            report.SkeletonId = d->Topology.SkeletonId;
            return report.Status == RigV1Status::Success;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool ParseSkeletonV1(W::View bytes, SkeletonV1& out, RigV1Report& report, const RigV1Limits& limits)
    {
        report = {};
        try
        {
            const auto fail = [&](RigV1Status status)
            {
                report.Status = status;
                return false;
            };
            W::Section sections[5];
            for (size_t i = 0; i < 5; ++i)
            {
                sections[i].Code = Codes[i];
                sections[i].Record = Records[i];
            }
            report.Status = W::ReadEnvelope(bytes, 1, {sections, 5}, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            const auto joints = sections[1].Count;
            if (!sections[0].Count || sections[0].Count > limits.MaxStringBytes || !joints || joints > limits.MaxJoints)
            {
                return fail(RigV1Status::LimitExceeded);
            }
            if (sections[2].Count != 1 || sections[3].Count != joints || sections[4].Count != 1)
            {
                return fail(RigV1Status::BadWire);
            }
            const W::View strings{bytes.data() + sections[0].Offset, size_t(sections[0].Size)};
            if (!Asset::MeasureSkeletalNameDecoding<char>(2, strings).Succeeded())
            {
                return fail(RigV1Status::InvalidName);
            }
            uint64_t stringRemaining = limits.MaxStringBytes;
            for (uint32_t i = 0; i < joints; ++i)
            {
                const size_t o = size_t(sections[1].Offset) + i * 24;
                const auto status =
                    W::PreflightName(strings, W::U64(bytes, o), W::U32(bytes, o + 8), limits, stringRemaining);
                if (status != RigV1Status::Success)
                {
                    return fail(status);
                }
            }
            const size_t setOffset = size_t(sections[2].Offset);
            const auto labelStatus = W::PreflightName(strings, W::U64(bytes, setOffset + 8),
                                                      W::U32(bytes, setOffset + 16), limits, stringRemaining);
            if (labelStatus != RigV1Status::Success)
            {
                return fail(labelStatus);
            }
            Detail::ObserveSplitAllocation("skeleton_owned");
            auto data = C::MakeShared<SkeletonV1Data>();
            report.Status = W::ReadTopology(bytes, strings, sections[1], limits, data->Topology);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            auto& rest = data->CurrentRest;
            const size_t set = size_t(sections[2].Offset);
            if (W::U32(bytes, set) != 0 || W::U32(bytes, set + 4) != joints || W::U32(bytes, set + 20) != 1 ||
                !W::Zero(bytes, set + 40, set + 48) ||
                !W::ReadName(strings, W::U64(bytes, set + 8), W::U32(bytes, set + 16), rest.Label, limits))
            {
                return fail(RigV1Status::InvalidRest);
            }
            rest.ResolvedImportScale = W::F64(bytes, set + 32);
            if (!std::isfinite(rest.ResolvedImportScale) || rest.ResolvedImportScale <= 0)
            {
                return fail(RigV1Status::InvalidRest);
            }
            rest.Rest.reserve(joints);
            for (uint32_t i = 0; i < joints; ++i)
            {
                const size_t o = size_t(sections[3].Offset) + i * 48;
                if (!W::Zero(bytes, o + 40, o + 48))
                {
                    return fail(RigV1Status::BadWire);
                }
                const auto value = W::ReadRest(bytes, o);
                if (!IsValidSkeletalRestTransform(value))
                {
                    return fail(RigV1Status::InvalidRest);
                }
                rest.Rest.push_back(value);
            }
            rest.RestHash = RigRestHash({rest.Rest.data(), rest.Rest.size()});
            if (rest.RestHash != W::U64(bytes, set + 24))
            {
                return fail(RigV1Status::HashMismatch);
            }
            const size_t root = size_t(sections[4].Offset);
            for (size_t i = 0; i < 16; ++i)
            {
                // profile1の外部親は恒等に固定。NaN/inf/-0もcanonical bytesとして拒否する。
                const uint32_t expected = (i % 5 == 0) ? 0x3f800000u : 0u;
                if (W::U32(bytes, root + i * 4) != expected)
                {
                    return fail(RigV1Status::UnsupportedProfile);
                }
            }
            data->RootHash = RigBytesHash({bytes.data() + root, 64});
            data->PayloadHash = W::U64(bytes, 48);
            data->ContentHash = RigBytesHash(bytes);
            report.TargetLabel = rest.Label;
            report.SkeletonId = data->Topology.SkeletonId;
            SkeletonV1 candidate;
            candidate.m_Data = std::move(data);
            out = std::move(candidate);
            report.Status = RigV1Status::Success;
            return true;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
} // namespace NorvesLib::Core::Skeletal
