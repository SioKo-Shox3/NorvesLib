#include "Asset/CookedSkeletonV1.h"
#include "Animation/RigRootFrame.h"
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
                                      W::Four('R', 'O', 'O', 'T'), W::Four('S', 'O', 'C', 'K')};
        constexpr uint32_t Records[] = {1, 24, 48, 48, 64, 64};
        bool SocketName(Identity name, C::AnsiString& out, const RigV1Limits& limits)
        {
            const auto text = name.GetView();
            using Char = C::String::value_type;
            const auto measured = Asset::MeasureSkeletalNameEncoding<Char>(2, {text.data(), text.size()});
            if (!measured.Succeeded() || !measured.ByteCount || measured.ByteCount > limits.MaxNameBytes)
                return false;
            C::VariableArray<uint8_t> bytes(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, C::Span<const Char>{text.data(), text.size()},
                                               C::Span<uint8_t>{bytes.data(), bytes.size()})
                     .Succeeded())
                return false;
            out = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return true;
        }
        RigV1Status Encode(const SkeletonV1Data& d, W::Bytes& out, const RigV1Limits& limits)
        {
            if (!IsValidRigProfileLimits(d.Profile, limits) || !IsValidRigRootFrame(d.RootTransform, d.Profile))
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
            Animation::SocketReport socketReport;
            if (!Animation::ValidateSockets(d.Sockets, count, socketReport))
                return RigV1Status::InvalidInput;
            C::VariableArray<C::AnsiString> socketNames;
            for (const auto& socket : d.Sockets)
            {
                C::AnsiString name;
                if (!SocketName(socket.Name, name, limits))
                    return RigV1Status::InvalidName;
                stringBytes += name.size();
                socketNames.push_back(std::move(name));
            }
            if (stringBytes > limits.MaxStringBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            const uint64_t sizes[] = {stringBytes, count * 24, 48, count * 48, 64, d.Sockets.size() * 64};
            const size_t sectionCount = d.Sockets.empty() ? 5 : 6;
            uint64_t total = 256 + sectionCount * 32;
            for (size_t i = 0; i < sectionCount; ++i)
            {
                total = ((total + 15) & ~uint64_t{15}) + sizes[i];
            }
            if (total > limits.MaxWireBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            W::OutputSection sections[6];
            for (size_t i = 0; i < 6; ++i)
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
            W::W32(set, 20, uint32_t(d.Profile));
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
            sections[5].Required = false;
            sections[5].Data.resize(d.Sockets.size() * 64, 0);
            for (size_t i = 0; i < d.Sockets.size(); ++i)
            {
                const auto& socket = d.Sockets[i];
                const auto& t = socket.Offset;
                const size_t o = i * 64;
                W::W64(sections[5].Data, o, W::AppendName(sections[0].Data, socketNames[i]));
                W::W32(sections[5].Data, o + 8, uint32_t(socketNames[i].size()));
                W::W32(sections[5].Data, o + 12, socket.ParentJoint);
                const float values[] = {t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y,
                                        t.rotation.z, t.rotation.w, t.scale.x,    t.scale.y,    t.scale.z};
                for (size_t k = 0; k < 10; ++k)
                    W::WF(sections[5].Data, o + 16 + k * 4, values[k]);
            }
            return W::WriteEnvelope(1, d.Topology.SkeletonId, {sections, sectionCount}, out, limits, d.Profile);
        }
    } // namespace
    bool BuildSkeletonV1(const RigAuthoringCpu& source, SkeletonV1& out, RigV1Report& report, const RigV1Limits& limits,
                         RigImportProfile profile)
    {
        report = {};
        try
        {
            const auto* rig = source.GetData();
            if (!rig || !IsSupportedRigImportProfile(profile) || rig->Profile != profile ||
                !IsValidRigProfileLimits(profile, limits))
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
            data.Profile = profile;
            data.RootTransform = rig->RootFrame;
            data.Topology = rig->Topology;
            data.CurrentRest.Label = rig->SourceLabel;
            data.CurrentRest.ResolvedImportScale = rig->ResolvedImportScale;
            data.CurrentRest.RootFrame = rig->RootFrame;
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
            return ParseSkeletonV1(W::ViewOf(bytes), out, report, limits, profile);
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool WithSkeletonSockets(const SkeletonV1& skeleton, C::Span<const Animation::SocketDefinition> sockets,
                             SkeletonV1& out, RigV1Report& report, const RigV1Limits& limits, RigImportProfile profile)
    {
        report = {};
        try
        {
            const auto* base = skeleton.GetData();
            if (!base || base->Profile != profile)
                return false;
            const auto fail = [&](RigV1Status status) {
                report.Status = status;
                return false;
            };
            if (!IsValidRigProfileLimits(profile, limits) || !IsValidRigRootFrame(base->RootTransform, profile))
                return fail(RigV1Status::InvalidInput);
            const size_t joints = base->Topology.Joints.size();
            if (!joints || joints > limits.MaxJoints || base->CurrentRest.Rest.size() != joints || sockets.size() > 256)
                return fail(RigV1Status::LimitExceeded);
            Animation::SocketReport socketReport;
            if (!Animation::ValidateSockets(sockets, joints, socketReport))
                return fail(RigV1Status::InvalidInput);
            uint64_t remaining = limits.MaxStringBytes;
            const auto name = [&](const C::AnsiString& text) {
                if (!W::ValidName(text, limits) || text.size() > remaining)
                    return false;
                remaining -= text.size();
                return true;
            };
            if (!name(base->CurrentRest.Label))
                return fail(RigV1Status::LimitExceeded);
            for (const auto& joint : base->Topology.Joints)
                if (!name(joint.Name))
                    return fail(RigV1Status::LimitExceeded);
            for (const auto& socket : sockets)
            {
                const auto text = socket.Name.GetView();
                const auto measured =
                    Asset::MeasureSkeletalNameEncoding<C::String::value_type>(2, {text.data(), text.size()});
                if (!measured.Succeeded() || !measured.ByteCount || measured.ByteCount > limits.MaxNameBytes)
                    return fail(RigV1Status::InvalidName);
                if (measured.ByteCount > remaining)
                    return fail(RigV1Status::LimitExceeded);
                remaining -= measured.ByteCount;
            }
            const uint64_t sizes[] = {
                limits.MaxStringBytes - remaining, joints * 24ull, 48, joints * 48ull, 64, sockets.size() * 64ull};
            const size_t count = sockets.empty() ? 5 : 6;
            uint64_t total = 256 + count * 32;
            for (size_t i = 0; i < count; ++i)
                total = ((total + 15) & ~uint64_t{15}) + sizes[i];
            if (total > limits.MaxWireBytes)
                return fail(RigV1Status::LimitExceeded);
            auto candidate = *base;
            candidate.Sockets = C::VariableArray<Animation::SocketDefinition>(sockets.begin(), sockets.end());
            W::Bytes bytes;
            report.Status = Encode(candidate, bytes, limits);
            if (report.Status != RigV1Status::Success)
                return false;
            return ParseSkeletonV1(W::ViewOf(bytes), out, report, limits, profile);
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool WriteSkeletonV1(const SkeletonV1& skeleton, W::Bytes& out, RigV1Report& report, const RigV1Limits& limits,
                         RigImportProfile profile)
    {
        report = {};
        try
        {
            const auto* d = skeleton.GetData();
            if (!d || d->Profile != profile)
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
    bool ParseSkeletonV1(W::View bytes, SkeletonV1& out, RigV1Report& report, const RigV1Limits& limits,
                         RigImportProfile profile)
    {
        report = {};
        try
        {
            const auto fail = [&](RigV1Status status)
            {
                report.Status = status;
                return false;
            };
            W::Section sections[6];
            for (size_t i = 0; i < 6; ++i)
            {
                sections[i].Code = Codes[i];
                sections[i].Record = Records[i];
            }
            sections[5].Required = false;
            report.Status = W::ReadEnvelope(bytes, 1, {sections, 6}, limits, profile);
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
            if (sections[5].Count > 256)
                return fail(RigV1Status::LimitExceeded);
            for (uint32_t i = 0; i < sections[5].Count; ++i)
            {
                const size_t o = size_t(sections[5].Offset) + size_t(i) * 64;
                const auto status =
                    W::PreflightName(strings, W::U64(bytes, o), W::U32(bytes, o + 8), limits, stringRemaining);
                if (status != RigV1Status::Success)
                    return fail(status);
                if (W::U32(bytes, o + 12) >= joints || !W::Zero(bytes, o + 56, o + 64))
                    return fail(RigV1Status::BadWire);
                for (size_t k = 0; k < 10; ++k)
                    if (!std::isfinite(W::F32(bytes, o + 16 + k * 4)))
                        return fail(RigV1Status::InvalidRest);
                double norm = 0;
                for (size_t k = 0; k < 4; ++k)
                {
                    const double q = W::F32(bytes, o + 28 + k * 4);
                    norm += q * q;
                }
                if (std::fabs(norm - 1) >= 1e-4 || W::F32(bytes, o + 44) != 1 || W::F32(bytes, o + 48) != 1 ||
                    W::F32(bytes, o + 52) != 1)
                    return fail(RigV1Status::InvalidRest);
                if (W::U32(bytes, o + 8) > 1024)
                    return fail(RigV1Status::InvalidName);
                for (uint32_t j = 0; j < i; ++j)
                {
                    const size_t p = size_t(sections[5].Offset) + size_t(j) * 64;
                    if (W::U32(bytes, p + 8) == W::U32(bytes, o + 8) &&
                        std::memcmp(strings.data() + W::U64(bytes, p), strings.data() + W::U64(bytes, o),
                                    W::U32(bytes, o + 8)) == 0)
                        return fail(RigV1Status::InvalidName);
                }
            }
            Detail::ObserveSplitAllocation("skeleton_owned");
            auto data = C::MakeShared<SkeletonV1Data>();
            data->Profile = profile;
            report.Status = W::ReadTopology(bytes, strings, sections[1], limits, data->Topology);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            auto& rest = data->CurrentRest;
            const size_t set = size_t(sections[2].Offset);
            if (W::U32(bytes, set) != 0 || W::U32(bytes, set + 4) != joints ||
                W::U32(bytes, set + 20) != uint32_t(profile) || !W::Zero(bytes, set + 40, set + 48) ||
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
                data->RootTransform[i] = W::F32(bytes, root + i * 4);
            }
            if (!IsValidRigRootFrame(data->RootTransform, profile))
            {
                return fail(RigV1Status::UnsupportedProfile);
            }
            for (uint32_t i = 0; i < sections[5].Count; ++i)
            {
                const size_t o = size_t(sections[5].Offset) + size_t(i) * 64;
                C::AnsiString utf8;
                C::String name;
                if (!W::ReadName(strings, W::U64(bytes, o), W::U32(bytes, o + 8), utf8, limits) ||
                    !W::NativeName(utf8, name))
                    return fail(RigV1Status::InvalidName);
                Animation::SocketDefinition socket;
                socket.Name = Identity(name);
                socket.ParentJoint = W::U32(bytes, o + 12);
                socket.Offset.position = {W::F32(bytes, o + 16), W::F32(bytes, o + 20), W::F32(bytes, o + 24)};
                socket.Offset.rotation = {W::F32(bytes, o + 28), W::F32(bytes, o + 32), W::F32(bytes, o + 36),
                                          W::F32(bytes, o + 40)};
                socket.Offset.scale = {W::F32(bytes, o + 44), W::F32(bytes, o + 48), W::F32(bytes, o + 52)};
                data->Sockets.push_back(socket);
            }
            Animation::SocketReport socketReport;
            if (!Animation::ValidateSockets(data->Sockets, joints, socketReport))
                return fail(RigV1Status::InvalidInput);
            rest.RootFrame = data->RootTransform;
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
