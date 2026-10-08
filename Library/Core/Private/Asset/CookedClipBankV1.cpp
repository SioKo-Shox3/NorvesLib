#include "Asset/CookedClipBankV1.h"
#include "Animation/SkeletalRootMotion.h"
#include "Animation/RigRootFrame.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
namespace NorvesLib::Core::Skeletal
{
    namespace C = Container;
    namespace
    {
        using Bytes = C::VariableArray<uint8_t>;
        using View = C::Span<const uint8_t>;
        constexpr uint32_t Four(char a, char b, char c, char d)
        {
            return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
                   (uint32_t(uint8_t(d)) << 24);
        }
        constexpr uint32_t Codes[] = {Four('S', 'T', 'R', 'S'), Four('T', 'J', 'N', 'T'), Four('R', 'S', 'E', 'T'),
                                      Four('A', 'R', 'S', 'T'), Four('C', 'L', 'I', 'P'), Four('C', 'H', 'A', 'N'),
                                      Four('S', 'A', 'M', 'P'), Four('A', 'F', 'R', 'M'), Four('A', 'N', 'L', 'Y'),
                                      Four('R', 'M', 'T', 'N'), Four('E', 'V', 'N', 'T'), Four('M', 'A', 'R', 'K'),
                                      Four('M', 'E', 'T', 'A')};
        constexpr uint32_t RecordSizes[] = {1, 24, 48, 48, 48, 48, 32, 64, 96, 40, 32, 16, 48};
        constexpr uint32_t MaxBankEvents = 262144, MaxBankMarkers = 65536;
        struct Section
        {
            uint32_t Code = 0, Flags = 0, Record = 0, Count = 0;
            uint64_t Offset = 0, Size = 0;
        };
        uint32_t U32(View b, size_t o)
        {
            return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
        }
        uint64_t U64(View b, size_t o)
        {
            return U32(b, o) | (uint64_t(U32(b, o + 4)) << 32);
        }
        float F32(View b, size_t o)
        {
            return std::bit_cast<float>(U32(b, o));
        }
        double F64(View b, size_t o)
        {
            return std::bit_cast<double>(U64(b, o));
        }
        void W32(Bytes& b, size_t o, uint32_t v)
        {
            for (unsigned n = 0; n < 4; ++n)
            {
                b[o + n] = uint8_t(v >> (n * 8));
            }
        }
        void W64(Bytes& b, size_t o, uint64_t v)
        {
            W32(b, o, uint32_t(v));
            W32(b, o + 4, uint32_t(v >> 32));
        }
        void WF(Bytes& b, size_t o, float v)
        {
            W32(b, o, std::bit_cast<uint32_t>(v));
        }
        bool Zero(View b, size_t first, size_t last)
        {
            return first <= last && last <= b.size() &&
                   std::all_of(b.begin() + first, b.begin() + last, [](uint8_t v) { return v == 0; });
        }
        View ViewOf(const Bytes& b)
        {
            return {b.data(), b.size()};
        }
        View NameView(const C::AnsiString& s)
        {
            return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
        }
        bool NameLess(const C::AnsiString& a, const C::AnsiString& b)
        {
            const int c = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
            return c < 0 || (c == 0 && a.size() < b.size());
        }
        bool NativeName(const C::AnsiString& utf8, C::String& out)
        {
            using Char = C::String::value_type;
            const auto measured = Asset::MeasureSkeletalNameDecoding<Char>(2, NameView(utf8));
            if (!measured.Succeeded())
            {
                return false;
            }
            C::VariableArray<Char> units(measured.CodeUnitCount);
            if (!Asset::DecodeSkeletalWireName(2, NameView(utf8), C::Span<Char>{units.data(), units.size()})
                     .Succeeded())
            {
                return false;
            }
            out = C::String(C::StringView(units.data(), units.size()));
            return true;
        }
        bool Utf8Name(const C::String& text, C::AnsiString& out, const RigV1Limits& limits)
        {
            using Char = C::String::value_type;
            const C::Span<const Char> units{text.data(), text.size()};
            const auto m = Asset::MeasureSkeletalNameEncoding(2, units);
            if (!m.Succeeded() || m.ByteCount == 0 || m.ByteCount > limits.MaxNameBytes)
            {
                return false;
            }
            Bytes bytes(m.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, units, C::Span<uint8_t>{bytes.data(), bytes.size()}).Succeeded())
            {
                return false;
            }
            out = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return true;
        }
        bool ValidUtf8(const C::AnsiString& name, const RigV1Limits& limits)
        {
            return !name.empty() && name.size() <= limits.MaxNameBytes &&
                   Asset::MeasureSkeletalNameDecoding<char>(2, NameView(name)).Succeeded();
        }
        int32_t FindJoint(const RigTopology& topology, const C::AnsiString& name)
        {
            const auto i =
                std::lower_bound(topology.Joints.begin(), topology.Joints.end(), name,
                                 [](const RigTopologyJoint& a, const C::AnsiString& b) { return NameLess(a.Name, b); });
            return i != topology.Joints.end() && i->Name == name ? static_cast<int32_t>(i - topology.Joints.begin())
                                                                 : -1;
        }
        bool ValidAnalysis(const RigClipAnalysis& a, const SkeletalAnimationClip& clip, size_t joints)
        {
            return a.RootJoint < joints && std::isfinite(a.LoopError) && a.LoopError >= 0 &&
                   a.DurationSeconds == double(clip.DurationSeconds) && std::isfinite(a.SourceFps) &&
                   a.SourceFps >= 0 && std::isfinite(a.TranslationX) && std::isfinite(a.TranslationZ) &&
                   std::isfinite(a.PlanarDistanceMeters) && a.PlanarDistanceMeters >= 0 &&
                   std::isfinite(a.AverageSpeedMetersPerSecond) && a.AverageSpeedMetersPerSecond >= 0 &&
                   std::isfinite(a.TotalYawRadians);
        }
        RigV1Status ValidateData(const ClipBankV1Data& d, const RigV1Limits& limits)
        {
            if (!IsSupportedRigImportProfile(d.Profile) || !IsValidRigProfileLimits(d.Profile, limits))
            {
                return RigV1Status::InvalidInput;
            }
            if (d.Topology.Joints.empty() || d.Topology.Joints.size() > limits.MaxJoints || d.Snapshots.empty() ||
                d.Snapshots.size() > limits.MaxSnapshots || d.Clips.empty() || d.Clips.size() > limits.MaxClips ||
                d.ClipSnapshots.size() != d.Clips.size())
            {
                return RigV1Status::LimitExceeded;
            }
            if (!d.Analyses.empty())
            {
                if (!IsStaticRootFrameProfile(d.Profile) || d.Analyses.size() != d.Clips.size())
                {
                    return RigV1Status::InvalidClip;
                }
                for (size_t i = 0; i < d.Clips.size(); ++i)
                {
                    if (!ValidAnalysis(d.Analyses[i], d.Clips[i], d.Topology.Joints.size()))
                    {
                        return RigV1Status::InvalidClip;
                    }
                }
            }
            size_t strings = 0, channels = 0, samples = 0, roots = 0, events = 0, markers = 0;
            const auto addName = [&](const C::AnsiString& name)
            {
                if (!ValidUtf8(name, limits) || name.size() > limits.MaxStringBytes - strings)
                {
                    return false;
                }
                strings += name.size();
                return true;
            };
            for (const auto& j : d.Topology.Joints)
            {
                if (!addName(j.Name))
                {
                    return RigV1Status::InvalidName;
                }
                if (j.ParentIndex < 0)
                {
                    ++roots;
                }
            }
            if (roots != 1)
            {
                return RigV1Status::UnsupportedProfile;
            }
            for (const auto& s : d.Snapshots)
            {
                if (!IsValidRigRootFrame(s.RootFrame, d.Profile) || !addName(s.Label) ||
                    s.Rest.size() != d.Topology.Joints.size() || !std::isfinite(s.ResolvedImportScale) ||
                    s.ResolvedImportScale <= 0)
                {
                    return RigV1Status::InvalidRest;
                }
                for (const auto& r : s.Rest)
                {
                    if (!IsValidSkeletalRestTransform(r))
                    {
                        return RigV1Status::InvalidRest;
                    }
                }
                if (s.RestHash != RigRestHash({s.Rest.data(), s.Rest.size()}))
                {
                    return RigV1Status::HashMismatch;
                }
            }
            C::VariableArray<C::AnsiString> names;
            names.reserve(d.Clips.size());
            C::VariableArray<uint8_t> usedSnapshots(d.Snapshots.size(), 0);
            for (size_t n = 0; n < d.Clips.size(); ++n)
            {
                const auto& clip = d.Clips[n];
                C::AnsiString name;
                if (!Utf8Name(clip.Name, name, limits) || !addName(name))
                {
                    return RigV1Status::InvalidName;
                }
                names.push_back(std::move(name));
                if (d.ClipSnapshots[n] >= d.Snapshots.size() || !std::isfinite(clip.DurationSeconds) ||
                    clip.DurationSeconds < 0 || clip.Channels.empty())
                {
                    return RigV1Status::InvalidClip;
                }
                if (!IsValidSkeletalRootMotion(clip, d.Topology.Joints.size()) ||
                    (!clip.RootMotion.empty() && (!IsStaticRootFrameProfile(d.Profile) ||
                                                  d.Topology.Joints[clip.RootMotionJoint].ParentIndex != -1)))
                {
                    return RigV1Status::InvalidClip;
                }
                if (clip.RootMotion.size() > limits.MaxSamples - samples)
                {
                    return RigV1Status::LimitExceeded;
                }
                samples += clip.RootMotion.size();
                Animation::ClipMetadataReport metadataReport;
                if (!Animation::ValidateClipMetadata(clip.Metadata, clip.DurationSeconds, metadataReport) ||
                    (clip.Metadata.Root.Joint != UINT32_MAX &&
                     (clip.Metadata.Root.Joint >= d.Topology.Joints.size() ||
                      d.Topology.Joints[clip.Metadata.Root.Joint].ParentIndex != -1)))
                    return RigV1Status::InvalidClip;
                if (clip.Metadata.Events.size() > MaxBankEvents - events ||
                    clip.Metadata.Markers.size() > MaxBankMarkers - markers)
                    return RigV1Status::LimitExceeded;
                events += clip.Metadata.Events.size();
                markers += clip.Metadata.Markers.size();
                for (const auto& event : clip.Metadata.Events)
                {
                    C::AnsiString text;
                    if (!Utf8Name(C::String(event.Name.GetView()), text, limits) || !addName(text))
                        return RigV1Status::InvalidName;
                }
                for (const auto& marker : clip.Metadata.Markers)
                {
                    C::AnsiString text;
                    if (!Utf8Name(C::String(marker.Name.GetView()), text, limits) || !addName(text))
                        return RigV1Status::InvalidName;
                }

                usedSnapshots[d.ClipSnapshots[n]] = 1;
                if (clip.Channels.size() > limits.MaxChannels - channels)
                {
                    return RigV1Status::LimitExceeded;
                }
                channels += clip.Channels.size();
                C::VariableArray<uint8_t> used(d.Topology.Joints.size() * 3, 0);
                for (const auto& channel : clip.Channels)
                {
                    const auto path = static_cast<uint32_t>(channel.Path),
                               interpolation = static_cast<uint32_t>(channel.Interpolation);
                    if (channel.JointIndex >= d.Topology.Joints.size() || path > 2 || interpolation > 1 ||
                        channel.Samples.empty() || used[channel.JointIndex * 3 + path])
                    {
                        return RigV1Status::InvalidClip;
                    }
                    used[channel.JointIndex * 3 + path] = 1;
                    if (channel.Samples.size() > limits.MaxSamples - samples)
                    {
                        return RigV1Status::LimitExceeded;
                    }
                    samples += channel.Samples.size();
                    float previous = -1;
                    for (const auto& sample : channel.Samples)
                    {
                        const auto& v = sample.Value;
                        if (!std::isfinite(sample.TimeSeconds) || sample.TimeSeconds < 0 ||
                            sample.TimeSeconds <= previous || sample.TimeSeconds > clip.DurationSeconds ||
                            !std::isfinite(v.X) || !std::isfinite(v.Y) || !std::isfinite(v.Z) || !std::isfinite(v.W))
                        {
                            return RigV1Status::InvalidClip;
                        }
                        previous = sample.TimeSeconds;
                        if ((path == 1 && !IsRepresentableSkeletalRotation(v)) ||
                            (path == 2 && (v.X <= 0 || v.Y <= 0 || v.Z <= 0)))
                        {
                            return RigV1Status::InvalidClip;
                        }
                    }
                }
            }
            if (std::find(usedSnapshots.begin(), usedSnapshots.end(), uint8_t{0}) != usedSnapshots.end())
            {
                return RigV1Status::InvalidRest;
            }
            std::sort(names.begin(), names.end(), NameLess);
            for (size_t i = 1; i < names.size(); ++i)
            {
                if (names[i] == names[i - 1])
                {
                    return RigV1Status::InvalidName;
                }
            }
            return RigV1Status::Success;
        }
        void WriteRest(Bytes& b, size_t o, const SkeletalRestTransform& r)
        {
            const float values[] = {r.Translation.X, r.Translation.Y, r.Translation.Z, r.Rotation.X, r.Rotation.Y,
                                    r.Rotation.Z,    r.Rotation.W,    r.Scale.X,       r.Scale.Y,    r.Scale.Z};
            for (size_t i = 0; i < 10; ++i)
            {
                WF(b, o + i * 4, values[i]);
            }
        }
        SkeletalRestTransform ReadRest(View b, size_t o)
        {
            SkeletalRestTransform r;
            r.Translation = {F32(b, o), F32(b, o + 4), F32(b, o + 8)};
            r.Rotation = {F32(b, o + 12), F32(b, o + 16), F32(b, o + 20), F32(b, o + 24)};
            r.Scale = {F32(b, o + 28), F32(b, o + 32), F32(b, o + 36)};
            return r;
        }
    } // namespace
    bool BuildClipBankV1(C::Span<const RigAuthoringCpu> sources, ClipBankV1& out, RigV1Report& report,
                         const RigV1Limits& limits, RigImportProfile profile,
                         const RigClipAnalysisOptions* analysisOptions,
                         C::Span<const SkeletalAnimationClip> replacementClips)
    {
        report = {};
        try
        {
            if (!IsSupportedRigImportProfile(profile) || !IsValidRigProfileLimits(profile, limits) || sources.empty() ||
                !sources.data() || (!replacementClips.empty() && sources.size() != 1))
            {
                return false;
            }
            if (sources.size() > limits.MaxSnapshots)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            if (analysisOptions && !IsStaticRootFrameProfile(profile))
            {
                report.Status = RigV1Status::UnsupportedProfile;
                return false;
            }
            size_t totalClips = 0, totalChannels = 0, totalSamples = 0;
            for (const auto& source : sources)
            {
                const auto* rig = source.GetData();
                const auto clips = !replacementClips.empty()
                                       ? replacementClips
                                       : (rig ? C::Span<const SkeletalAnimationClip>(rig->Geometry.Clips)
                                              : C::Span<const SkeletalAnimationClip>{});
                if (!rig || rig->Profile != profile || clips.empty())
                {
                    report.Status = RigV1Status::InvalidInput;
                    return false;
                }
                if (clips.size() > limits.MaxClips - totalClips || rig->Geometry.Joints.size() > limits.MaxJoints)
                {
                    report.Status = RigV1Status::LimitExceeded;
                    return false;
                }
                totalClips += clips.size();
                for (const auto& clip : clips)
                {
                    if (clip.Channels.size() > limits.MaxChannels - totalChannels)
                    {
                        report.Status = RigV1Status::LimitExceeded;
                        return false;
                    }
                    totalChannels += clip.Channels.size();
                    if (clip.RootMotion.size() > limits.MaxSamples - totalSamples)
                    {
                        report.Status = RigV1Status::LimitExceeded;
                        return false;
                    }
                    totalSamples += clip.RootMotion.size();
                    for (const auto& channel : clip.Channels)
                    {
                        if (channel.Samples.size() > limits.MaxSamples - totalSamples)
                        {
                            report.Status = RigV1Status::LimitExceeded;
                            return false;
                        }
                        totalSamples += channel.Samples.size();
                    }
                }
            }
            auto d = C::MakeShared<ClipBankV1Data>();
            d->Profile = profile;
            for (const auto& source : sources)
            {
                const auto* rig = source.GetData();
                const auto clips = !replacementClips.empty()
                                       ? replacementClips
                                       : (rig ? C::Span<const SkeletalAnimationClip>(rig->Geometry.Clips)
                                              : C::Span<const SkeletalAnimationClip>{});
                if (!rig || rig->Profile != profile || clips.empty())
                {
                    report.Status = RigV1Status::InvalidInput;
                    return false;
                }
                if (d->Snapshots.empty())
                {
                    d->Topology = rig->Topology;
                }
                else if (!SameRigTopology(d->Topology, rig->Topology))
                {
                    report.Status = RigV1Status::TopologyMismatch;
                    return false;
                }
                if (clips.size() > limits.MaxClips - d->Clips.size())
                {
                    report.Status = RigV1Status::LimitExceeded;
                    return false;
                }
                RigClipSnapshot snapshot;
                snapshot.Label = rig->SourceLabel;
                snapshot.ResolvedImportScale = rig->ResolvedImportScale;
                snapshot.RootFrame = rig->RootFrame;
                snapshot.Rest.reserve(rig->LocalRest.size());
                for (uint32_t index : rig->Topology.CanonicalToSource)
                {
                    snapshot.Rest.push_back(rig->LocalRest[index]);
                }
                snapshot.RestHash = RigRestHash({snapshot.Rest.data(), snapshot.Rest.size()});
                const auto snapshotIndex = static_cast<uint32_t>(d->Snapshots.size());
                d->Snapshots.push_back(std::move(snapshot));
                for (const auto& clip : clips)
                {
                    auto owned = clip;
                    if (analysisOptions)
                    {
                        RigClipAnalysis analysis;
                        if (!AnalyzeRigClip(source, clip, *analysisOptions, owned, analysis))
                        {
                            report.Status = RigV1Status::InvalidClip;
                            return false;
                        }
                        analysis.RootJoint = rig->Topology.SourceToCanonical[analysis.RootJoint];
                        d->Analyses.push_back(analysis);
                    }
                    for (auto& channel : owned.Channels)
                    {
                        if (channel.JointIndex >= rig->Topology.SourceToCanonical.size())
                        {
                            report.Status = RigV1Status::InvalidClip;
                            return false;
                        }
                        channel.JointIndex = rig->Topology.SourceToCanonical[channel.JointIndex];
                    }
                    if (owned.Metadata.Root.Joint != UINT32_MAX)
                    {
                        if (owned.Metadata.Root.Joint >= rig->Topology.SourceToCanonical.size())
                        {
                            report.Status = RigV1Status::InvalidClip;
                            return false;
                        }
                        owned.Metadata.Root.Joint = rig->Topology.SourceToCanonical[owned.Metadata.Root.Joint];
                    }
                    if (!owned.RootMotion.empty())
                    {
                        if (owned.RootMotionJoint >= rig->Topology.SourceToCanonical.size())
                        {
                            report.Status = RigV1Status::InvalidClip;
                            return false;
                        }
                        owned.RootMotionJoint = rig->Topology.SourceToCanonical[owned.RootMotionJoint];
                    }
                    d->Clips.push_back(std::move(owned));
                    d->ClipSnapshots.push_back(snapshotIndex);
                }
            }
            report.Status = ValidateData(*d, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            report.SkeletonId = d->Topology.SkeletonId;
            ClipBankV1 candidate;
            candidate.m_Data = std::move(d);
            out = std::move(candidate);
            return true;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool WriteClipBankV1(const ClipBankV1& bank, Bytes& out, RigV1Report& report, const RigV1Limits& limits,
                         RigImportProfile profile)
    {
        report = {};
        try
        {
            const auto* d = bank.GetData();
            if (!d || d->Profile != profile)
            {
                return false;
            }
            report.Status = ValidateData(*d, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            // section配列を確保する前に、指定されたwire上限を全表の積和で検査する。
            C::VariableArray<uint32_t> active;
            for (uint32_t i = 0; i < (IsStaticRootFrameProfile(profile) ? 8u : 7u); ++i)
            {
                active.push_back(i);
            }
            if (!d->Analyses.empty())
            {
                active.push_back(8);
            }
            uint64_t rootSamples = 0;
            for (const auto& clip : d->Clips)
            {
                rootSamples += clip.RootMotion.size();
            }
            if (rootSamples)
            {
                active.push_back(9);
            }
            bool hasMetadata = false;
            for (const auto& clip : d->Clips)
                hasMetadata |= !Animation::SameClipMetadata(clip.Metadata, Animation::ClipMetadata{});
            if (hasMetadata)
            {
                active.push_back(10);
                active.push_back(11);
                active.push_back(12);
            }
            const uint32_t sectionCount = uint32_t(active.size());
            uint64_t estimated[13]{};
            if (hasMetadata)
                estimated[12] = d->Clips.size() * 48;
            estimated[9] = rootSamples * 40;
            estimated[8] = d->Analyses.size() * 96;
            if (IsStaticRootFrameProfile(profile))
            {
                estimated[7] = d->Snapshots.size() * 64;
            }
            for (const auto& joint : d->Topology.Joints)
            {
                estimated[0] += joint.Name.size();
            }
            estimated[1] = d->Topology.Joints.size() * 24;
            estimated[2] = d->Snapshots.size() * 48;
            estimated[3] = d->Snapshots.size() * d->Topology.Joints.size() * 48;
            estimated[4] = d->Clips.size() * 48;
            for (const auto& snapshot : d->Snapshots)
            {
                estimated[0] += snapshot.Label.size();
            }
            for (const auto& clip : d->Clips)
            {
                const auto measured =
                    Asset::MeasureSkeletalNameEncoding<C::String::value_type>(2, {clip.Name.data(), clip.Name.size()});
                estimated[0] += measured.ByteCount;
                estimated[5] += clip.Channels.size() * 48;
                estimated[10] += clip.Metadata.Events.size() * 32;
                estimated[11] += clip.Metadata.Markers.size() * 16;
                const auto countName = [&](Identity identity) {
                    const auto text = identity.GetView();
                    estimated[0] +=
                        Asset::MeasureSkeletalNameEncoding<C::String::value_type>(2, {text.data(), text.size()})
                            .ByteCount;
                };
                for (const auto& event : clip.Metadata.Events)
                    countName(event.Name);
                for (const auto& marker : clip.Metadata.Markers)
                    countName(marker.Name);
                for (const auto& channel : clip.Channels)
                {
                    estimated[6] += channel.Samples.size() * 32;
                }
            }
            uint64_t estimatedTotal = 256 + sectionCount * 32;
            for (uint32_t i : active)
            {
                estimatedTotal = ((estimatedTotal + 15) & ~uint64_t{15}) + estimated[i];
            }
            if (estimatedTotal > limits.MaxWireBytes)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            Bytes sections[13];
            uint32_t counts[13]{};
            C::VariableArray<uint64_t> jointOffsets;
            const auto addString = [&](const C::AnsiString& name)
            {
                const uint64_t offset = sections[0].size();
                for (unsigned char c : name)
                {
                    sections[0].push_back(c);
                }
                return offset;
            };
            for (const auto& joint : d->Topology.Joints)
            {
                const auto offset = addString(joint.Name);
                jointOffsets.push_back(offset);
                const auto o = sections[1].size();
                sections[1].resize(o + 24, 0);
                W64(sections[1], o, offset);
                W32(sections[1], o + 8, uint32_t(joint.Name.size()));
                W32(sections[1], o + 12, uint32_t(joint.ParentIndex));
                W64(sections[1], o + 16, RigBytesHash(NameView(joint.Name)));
            }
            for (const auto& snapshot : d->Snapshots)
            {
                const auto o = sections[2].size();
                sections[2].resize(o + 48, 0);
                W32(sections[2], o, uint32_t(sections[3].size() / 48));
                W32(sections[2], o + 4, uint32_t(snapshot.Rest.size()));
                W64(sections[2], o + 8, addString(snapshot.Label));
                W32(sections[2], o + 16, uint32_t(snapshot.Label.size()));
                W32(sections[2], o + 20, uint32_t(profile));
                W64(sections[2], o + 24, snapshot.RestHash);
                W64(sections[2], o + 32, std::bit_cast<uint64_t>(snapshot.ResolvedImportScale));
                if (IsStaticRootFrameProfile(profile))
                {
                    const size_t f = sections[7].size();
                    sections[7].resize(f + 64, 0);
                    for (size_t i = 0; i < 16; ++i)
                    {
                        WF(sections[7], f + i * 4, snapshot.RootFrame[i]);
                    }
                }
                for (const auto& rest : snapshot.Rest)
                {
                    const auto r = sections[3].size();
                    sections[3].resize(r + 48, 0);
                    WriteRest(sections[3], r, rest);
                }
            }
            for (size_t n = 0; n < d->Clips.size(); ++n)
            {
                const auto& clip = d->Clips[n];
                C::AnsiString name;
                if (!Utf8Name(clip.Name, name, limits))
                {
                    report.Status = RigV1Status::InvalidName;
                    return false;
                }
                const auto o = sections[4].size();
                sections[4].resize(o + 48, 0);
                W64(sections[4], o, addString(name));
                W32(sections[4], o + 8, uint32_t(name.size()));
                W32(sections[4], o + 12, d->ClipSnapshots[n]);
                WF(sections[4], o + 16, clip.DurationSeconds);
                W32(sections[4], o + 20, uint32_t(sections[5].size() / 48));
                W32(sections[4], o + 24, uint32_t(clip.Channels.size()));
                W64(sections[4], o + 32, RigBytesHash(NameView(name)));
                for (const auto& channel : clip.Channels)
                {
                    const auto& joint = d->Topology.Joints[channel.JointIndex];
                    const auto c = sections[5].size();
                    sections[5].resize(c + 48, 0);
                    W64(sections[5], c, jointOffsets[channel.JointIndex]);
                    W32(sections[5], c + 8, uint32_t(joint.Name.size()));
                    W32(sections[5], c + 12, uint32_t(channel.Path));
                    W64(sections[5], c + 16, RigBytesHash(NameView(joint.Name)));
                    W32(sections[5], c + 24, uint32_t(channel.Interpolation));
                    W32(sections[5], c + 32, uint32_t(sections[6].size() / 32));
                    W32(sections[5], c + 36, uint32_t(channel.Samples.size()));
                    for (const auto& sample : channel.Samples)
                    {
                        const auto s = sections[6].size();
                        sections[6].resize(s + 32, 0);
                        WF(sections[6], s, sample.TimeSeconds);
                        WF(sections[6], s + 4, sample.Value.X);
                        WF(sections[6], s + 8, sample.Value.Y);
                        WF(sections[6], s + 12, sample.Value.Z);
                        WF(sections[6], s + 16, sample.Value.W);
                    }
                }
            }
            if (!d->Analyses.empty())
            {
                counts[8] = uint32_t(d->Analyses.size());
                sections[8].resize(d->Analyses.size() * 96, 0);
                for (size_t i = 0; i < d->Analyses.size(); ++i)
                {
                    const auto& a = d->Analyses[i];
                    const size_t o = i * 96;
                    W32(sections[8], o, a.RootJoint);
                    W32(sections[8], o + 4, (a.bLoopCandidate ? 1u : 0u) | (a.bLoop ? 2u : 0u));
                    const double values[] = {a.LoopError,
                                             a.DurationSeconds,
                                             a.SourceFps,
                                             a.TranslationX,
                                             a.TranslationZ,
                                             a.PlanarDistanceMeters,
                                             a.AverageSpeedMetersPerSecond,
                                             a.TotalYawRadians};
                    for (size_t k = 0; k < 8; ++k)
                    {
                        W64(sections[8], o + 8 + k * 8, std::bit_cast<uint64_t>(values[k]));
                    }
                }
            }
            for (size_t clipIndex = 0; clipIndex < d->Clips.size(); ++clipIndex)
            {
                const auto& clip = d->Clips[clipIndex];
                for (const auto& sample : clip.RootMotion)
                {
                    const size_t o = sections[9].size();
                    sections[9].resize(o + 40, 0);
                    W32(sections[9], o, uint32_t(clipIndex));
                    W32(sections[9], o + 4, clip.RootMotionJoint);
                    WF(sections[9], o + 8, sample.TimeSeconds);
                    W64(sections[9], o + 16, std::bit_cast<uint64_t>(sample.TranslationX));
                    W64(sections[9], o + 24, std::bit_cast<uint64_t>(sample.TranslationZ));
                    W64(sections[9], o + 32, std::bit_cast<uint64_t>(sample.YawRadians));
                }
            }
            if (hasMetadata)
            {
                for (const auto& clip : d->Clips)
                {
                    const auto& m = clip.Metadata;
                    const size_t o = sections[12].size();
                    sections[12].resize(o + 48, 0);
                    W32(sections[12], o, uint32_t(sections[10].size() / 32));
                    W32(sections[12], o + 4, uint32_t(m.Events.size()));
                    W32(sections[12], o + 8, uint32_t(sections[11].size() / 16));
                    W32(sections[12], o + 12, uint32_t(m.Markers.size()));
                    W32(sections[12], o + 16,
                        uint32_t(m.Root.Mode) | (m.Loop.bEnabled ? 4u : 0u) | (m.Root.bX ? 8u : 0u) |
                            (m.Root.bZ ? 16u : 0u) | (m.Root.bYaw ? 32u : 0u));
                    W32(sections[12], o + 20, m.Root.Joint);
                    WF(sections[12], o + 24, m.Loop.Start);
                    WF(sections[12], o + 28, m.Loop.End);
                    WF(sections[12], o + 32, m.Root.NominalSpeed);
                    WF(sections[12], o + 36, m.GroundOffset);
                    for (const auto& event : m.Events)
                    {
                        C::AnsiString name;
                        if (!Utf8Name(C::String(event.Name.GetView()), name, limits))
                        {
                            report.Status = RigV1Status::InvalidName;
                            return false;
                        }
                        const size_t e = sections[10].size();
                        sections[10].resize(e + 32, 0);
                        W64(sections[10], e, addString(name));
                        W32(sections[10], e + 8, uint32_t(name.size()));
                        WF(sections[10], e + 12, event.Time);
                        WF(sections[10], e + 16, event.EndTime);
                        WF(sections[10], e + 20, event.MinWeight);
                        WF(sections[10], e + 24, event.Value);
                        W32(sections[10], e + 28, std::bit_cast<uint32_t>(event.IntValue));
                    }
                    for (const auto& marker : m.Markers)
                    {
                        C::AnsiString name;
                        if (!Utf8Name(C::String(marker.Name.GetView()), name, limits))
                        {
                            report.Status = RigV1Status::InvalidName;
                            return false;
                        }
                        const size_t e = sections[11].size();
                        sections[11].resize(e + 16, 0);
                        W64(sections[11], e, addString(name));
                        W32(sections[11], e + 8, uint32_t(name.size()));
                        WF(sections[11], e + 12, marker.Time);
                    }
                }
            }
            uint64_t total = 256 + sectionCount * 32;
            for (uint32_t i : active)
            {
                counts[i] = uint32_t(sections[i].size() / RecordSizes[i]);
                total = (total + 15) & ~uint64_t{15};
                total += sections[i].size();
            }
            if (total > limits.MaxWireBytes)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            Bytes candidate(static_cast<size_t>(total), 0);
            std::memcpy(candidate.data(), "NVSKELv1", 8);
            W32(candidate, 8, 256);
            W32(candidate, 12, 1);
            W32(candidate, 16, 0x01020304);
            W32(candidate, 20, 3);
            W32(candidate, 28, sectionCount);
            W64(candidate, 32, 256);
            W64(candidate, 40, total);
            W64(candidate, 56, d->Topology.SkeletonId);
            W32(candidate, 64, uint32_t(profile));
            W32(candidate, 68, 1);
            size_t cursor = 256 + sectionCount * 32;
            for (size_t slot = 0; slot < active.size(); ++slot)
            {
                const size_t i = active[slot];
                cursor = (cursor + 15) & ~size_t{15};
                const size_t e = 256 + slot * 32;
                W32(candidate, e, Codes[i]);
                W32(candidate, e + 4, i >= 8 ? 0 : 1);
                W64(candidate, e + 8, cursor);
                W64(candidate, e + 16, sections[i].size());
                W32(candidate, e + 24, RecordSizes[i]);
                W32(candidate, e + 28, counts[i]);
                if (!sections[i].empty())
                {
                    std::memcpy(candidate.data() + cursor, sections[i].data(), sections[i].size());
                }
                cursor += sections[i].size();
            }
            report.BankPayloadHash = RigBytesHash({candidate.data() + 256, candidate.size() - 256});
            W64(candidate, 48, report.BankPayloadHash);
            report.SkeletonId = d->Topology.SkeletonId;
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
    bool ParseClipBankV1(View bytes, ClipBankV1& out, RigV1Report& report, const RigV1Limits& limits,
                         RigImportProfile profile)
    {
        report = {};
        try
        {
            const auto fail = [&](RigV1Status s)
            {
                report.Status = s;
                return false;
            };
            if (!IsSupportedRigImportProfile(profile) || !IsValidRigProfileLimits(profile, limits) || !bytes.data())
            {
                return false;
            }
            if (bytes.size() > limits.MaxWireBytes)
            {
                return fail(RigV1Status::LimitExceeded);
            }
            const uint32_t sectionCount = IsStaticRootFrameProfile(profile) ? 8 : 7;
            if (bytes.size() < 256 + sectionCount * 32 || std::memcmp(bytes.data(), "NVSKELv1", 8) != 0)
            {
                return fail(RigV1Status::BadWire);
            }
            if (U32(bytes, 12) != 1 || U32(bytes, 20) != 3 || U32(bytes, 64) != uint32_t(profile) ||
                U32(bytes, 68) != 1)
            {
                return fail(RigV1Status::UnsupportedVersion);
            }
            const uint32_t count = U32(bytes, 28);
            if (U32(bytes, 8) != 256 || U32(bytes, 16) != 0x01020304 || U32(bytes, 24) || count < sectionCount ||
                count > 16 || U64(bytes, 32) != 256 || U64(bytes, 40) != bytes.size() || !Zero(bytes, 72, 256) ||
                256ull + count * 32ull > bytes.size())
            {
                return fail(RigV1Status::BadWire);
            }
            const uint64_t payloadHash = RigBytesHash({bytes.data() + 256, bytes.size() - 256});
            if (U64(bytes, 48) != payloadHash)
            {
                return fail(RigV1Status::HashMismatch);
            }
            C::VariableArray<Section> directory;
            directory.reserve(count);
            Section known[8]{};
            Section analysisSection{};
            bool bAnalysis = false;
            Section motionSection{};
            bool bMotion = false;
            Section metadataSections[3]{};
            bool metadataFound[3]{};
            bool found[8]{};
            for (uint32_t i = 0; i < count; ++i)
            {
                const size_t e = 256 + i * 32;
                Section s;
                s.Code = U32(bytes, e);
                s.Flags = U32(bytes, e + 4);
                s.Offset = U64(bytes, e + 8);
                s.Size = U64(bytes, e + 16);
                s.Record = U32(bytes, e + 24);
                s.Count = U32(bytes, e + 28);
                if (s.Flags > 1 || s.Offset < 256ull + count * 32ull || s.Offset % 16 || s.Offset > bytes.size() ||
                    s.Size > bytes.size() - s.Offset || !s.Record || uint64_t(s.Record) * s.Count != s.Size)
                {
                    return fail(RigV1Status::BadWire);
                }
                for (const auto& old : directory)
                {
                    if (old.Code == s.Code)
                    {
                        return fail(RigV1Status::BadWire);
                    }
                }
                int index = -1;
                for (uint32_t n = 0; n < sectionCount; ++n)
                {
                    if (Codes[n] == s.Code)
                    {
                        index = n;
                    }
                }
                if (index >= 0)
                {
                    if (s.Flags != 1 || s.Record != RecordSizes[index])
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    known[index] = s;
                    found[index] = true;
                }
                else if (s.Code == Four('A', 'N', 'L', 'Y'))
                {
                    if (!IsStaticRootFrameProfile(profile) || s.Flags != 0 || s.Record != 96)
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    bAnalysis = true;
                    analysisSection = s;
                }
                else if (s.Code == Four('R', 'M', 'T', 'N'))
                {
                    if (!IsStaticRootFrameProfile(profile) || s.Flags != 0 || s.Record != 40 || s.Count == 0)
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    bMotion = true;
                    motionSection = s;
                }
                else if (s.Code == Codes[10] || s.Code == Codes[11] || s.Code == Codes[12])
                {
                    const unsigned slot = s.Code == Codes[10] ? 0 : (s.Code == Codes[11] ? 1 : 2);
                    if (s.Flags != 0 || s.Record != RecordSizes[10 + slot])
                        return fail(RigV1Status::BadWire);
                    metadataSections[slot] = s;
                    metadataFound[slot] = true;
                }
                else if ((s.Flags & 1) || s.Code == Four('S', 'O', 'C', 'K') ||
                         (IsStaticRootFrameProfile(profile) &&
                          (s.Code == Four('R', 'O', 'O', 'T') || s.Code == Four('S', 'R', 'E', 'F') ||
                           s.Code == Four('V', 'E', 'R', 'T') || s.Code == Four('I', 'N', 'D', 'X') ||
                           s.Code == Four('I', 'B', 'M', 'S') || s.Code == Four('M', 'N', 'G', 'T') ||
                           s.Code == Four('S', 'U', 'B', 'M') || s.Code == Four('M', 'S', 'L', 'T') ||
                           s.Code == Four('M', 'A', 'T', 'S'))))
                {
                    return fail(RigV1Status::UnsupportedSection);
                }
                directory.push_back(s);
            }
            for (uint32_t i = 0; i < sectionCount; ++i)
            {
                if (!found[i])
                {
                    return fail(RigV1Status::BadWire);
                }
            }
            std::sort(directory.begin(), directory.end(),
                      [](const Section& a, const Section& b) { return a.Offset < b.Offset; });
            uint64_t end = 256ull + count * 32ull;
            for (const auto& s : directory)
            {
                if (s.Offset < end || s.Offset - end >= 16 || !Zero(bytes, size_t(end), size_t(s.Offset)))
                {
                    return fail(RigV1Status::BadWire);
                }
                end = s.Offset + s.Size;
            }
            if (bytes.size() - end >= 16 || !Zero(bytes, size_t(end), bytes.size()))
            {
                return fail(RigV1Status::BadWire);
            }
            if (!known[0].Count || known[0].Count > limits.MaxStringBytes || !known[1].Count ||
                known[1].Count > limits.MaxJoints || !known[2].Count || known[2].Count > limits.MaxSnapshots ||
                known[3].Count != uint64_t(known[1].Count) * known[2].Count || !known[4].Count ||
                known[4].Count > limits.MaxClips || known[5].Count > limits.MaxChannels ||
                known[6].Count > limits.MaxSamples ||
                (bMotion && motionSection.Count > limits.MaxSamples - known[6].Count))
            {
                return fail(RigV1Status::LimitExceeded);
            }
            const bool hasMetadata = metadataFound[2];
            if (metadataFound[0] != hasMetadata || metadataFound[1] != hasMetadata ||
                (hasMetadata && metadataSections[2].Count != known[4].Count))
                return fail(RigV1Status::BadWire);
            if (metadataSections[0].Count > MaxBankEvents || metadataSections[1].Count > MaxBankMarkers)
                return fail(RigV1Status::LimitExceeded);
            if (IsStaticRootFrameProfile(profile))
            {
                if (known[7].Count != known[2].Count)
                {
                    return fail(RigV1Status::BadWire);
                }
                // 必須frameの件数と全値を可変長所有の確保前に検査する。
                for (uint32_t n = 0; n < known[7].Count; ++n)
                {
                    RigRootFrame frame;
                    for (size_t i = 0; i < 16; ++i)
                    {
                        frame[i] = F32(bytes, size_t(known[7].Offset) + n * 64 + i * 4);
                    }
                    if (!IsValidRigRootFrame(frame, profile))
                    {
                        return fail(RigV1Status::InvalidRest);
                    }
                }
            }
            const View strings{bytes.data() + known[0].Offset, size_t(known[0].Size)};
            if (!Asset::MeasureSkeletalNameDecoding<char>(2, strings).Succeeded())
            {
                return fail(RigV1Status::InvalidName);
            }
            const auto name = [&](uint64_t offset, uint32_t size, C::AnsiString& value)
            {
                if (!size || size > limits.MaxNameBytes || offset > strings.size() || size > strings.size() - offset)
                {
                    return false;
                }
                View view{strings.data() + offset, size};
                if (!Asset::MeasureSkeletalNameDecoding<char>(2, view).Succeeded())
                {
                    return false;
                }
                value = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(view.data()), view.size()));
                return true;
            };
            auto data = C::MakeShared<ClipBankV1Data>();
            data->Profile = profile;
            C::VariableArray<SkeletalJoint> joints(known[1].Count);
            C::AnsiString previous;
            for (uint32_t i = 0; i < known[1].Count; ++i)
            {
                const size_t o = size_t(known[1].Offset) + i * 24;
                C::AnsiString text;
                if (!name(U64(bytes, o), U32(bytes, o + 8), text) ||
                    U64(bytes, o + 16) != RigBytesHash(NameView(text)) || (i && !NameLess(previous, text)) ||
                    !NativeName(text, joints[i].Name))
                {
                    return fail(RigV1Status::InvalidName);
                }
                joints[i].ParentIndex = std::bit_cast<int32_t>(U32(bytes, o + 12));
                previous = std::move(text);
            }
            report.Status = BuildRigTopology({joints.data(), joints.size()}, limits, data->Topology);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            if (data->Topology.SkeletonId != U64(bytes, 56))
            {
                return fail(RigV1Status::HashMismatch);
            }
            data->Snapshots.reserve(known[2].Count);
            uint32_t restCursor = 0;
            for (uint32_t i = 0; i < known[2].Count; ++i)
            {
                const size_t o = size_t(known[2].Offset) + i * 48;
                RigClipSnapshot snapshot;
                if (U32(bytes, o) != restCursor || U32(bytes, o + 4) != joints.size() ||
                    U32(bytes, o + 20) != uint32_t(profile) || !Zero(bytes, o + 40, o + 48) ||
                    !name(U64(bytes, o + 8), U32(bytes, o + 16), snapshot.Label))
                {
                    return fail(RigV1Status::InvalidRest);
                }
                if (IsStaticRootFrameProfile(profile))
                {
                    for (size_t k = 0; k < 16; ++k)
                    {
                        snapshot.RootFrame[k] = F32(bytes, size_t(known[7].Offset) + i * 64 + k * 4);
                    }
                }
                snapshot.ResolvedImportScale = F64(bytes, o + 32);
                snapshot.RestHash = U64(bytes, o + 24);
                snapshot.Rest.reserve(joints.size());
                for (size_t j = 0; j < joints.size(); ++j)
                {
                    const size_t r = size_t(known[3].Offset) + size_t(restCursor++) * 48;
                    if (!Zero(bytes, r + 40, r + 48))
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    snapshot.Rest.push_back(ReadRest(bytes, r));
                }
                data->Snapshots.push_back(std::move(snapshot));
            }
            data->Clips.reserve(known[4].Count);
            data->ClipSnapshots.reserve(known[4].Count);
            uint32_t channelCursor = 0, sampleCursor = 0;
            for (uint32_t i = 0; i < known[4].Count; ++i)
            {
                const size_t o = size_t(known[4].Offset) + i * 48;
                C::AnsiString clipName;
                SkeletalAnimationClip clip;
                const auto channels = U32(bytes, o + 24);
                if (!name(U64(bytes, o), U32(bytes, o + 8), clipName) || !NativeName(clipName, clip.Name) ||
                    U64(bytes, o + 32) != RigBytesHash(NameView(clipName)) || U32(bytes, o + 20) != channelCursor ||
                    channelCursor > known[5].Count || channels > known[5].Count - channelCursor || U32(bytes, o + 28) ||
                    !Zero(bytes, o + 40, o + 48))
                {
                    return fail(RigV1Status::InvalidClip);
                }
                clip.DurationSeconds = F32(bytes, o + 16);
                clip.Channels.reserve(channels);
                data->ClipSnapshots.push_back(U32(bytes, o + 12));
                for (uint32_t n = 0; n < channels; ++n)
                {
                    const size_t c = size_t(known[5].Offset) + size_t(channelCursor++) * 48;
                    C::AnsiString jointName;
                    SkeletalAnimationChannel channel;
                    const auto samples = U32(bytes, c + 36);
                    if (!name(U64(bytes, c), U32(bytes, c + 8), jointName) ||
                        U64(bytes, c + 16) != RigBytesHash(NameView(jointName)) || U32(bytes, c + 28) ||
                        U32(bytes, c + 32) != sampleCursor || sampleCursor > known[6].Count ||
                        samples > known[6].Count - sampleCursor || !Zero(bytes, c + 40, c + 48))
                    {
                        return fail(RigV1Status::InvalidClip);
                    }
                    const auto index = FindJoint(data->Topology, jointName);
                    if (index < 0)
                    {
                        return fail(RigV1Status::InvalidClip);
                    }
                    channel.JointIndex = uint32_t(index);
                    channel.Path = static_cast<SkeletalAnimationPath>(U32(bytes, c + 12));
                    channel.Interpolation = static_cast<SkeletalAnimationInterpolation>(U32(bytes, c + 24));
                    channel.Samples.reserve(samples);
                    for (uint32_t k = 0; k < samples; ++k)
                    {
                        const size_t s = size_t(known[6].Offset) + size_t(sampleCursor++) * 32;
                        if (!Zero(bytes, s + 20, s + 32))
                        {
                            return fail(RigV1Status::BadWire);
                        }
                        channel.Samples.push_back(
                            {F32(bytes, s),
                             {F32(bytes, s + 4), F32(bytes, s + 8), F32(bytes, s + 12), F32(bytes, s + 16)}});
                    }
                    clip.Channels.push_back(std::move(channel));
                }
                data->Clips.push_back(std::move(clip));
            }
            if (restCursor != known[3].Count || channelCursor != known[5].Count || sampleCursor != known[6].Count)
            {
                return fail(RigV1Status::BadWire);
            }
            if (bMotion)
            {
                uint32_t previousClip = 0;
                for (uint32_t i = 0; i < motionSection.Count; ++i)
                {
                    const size_t o = size_t(motionSection.Offset) + size_t(i) * 40;
                    const auto clipIndex = U32(bytes, o), joint = U32(bytes, o + 4);
                    if (clipIndex >= data->Clips.size() || (i && clipIndex < previousClip) || U32(bytes, o + 12))
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    previousClip = clipIndex;
                    auto& clip = data->Clips[clipIndex];
                    if (!clip.RootMotion.empty() && clip.RootMotionJoint != joint)
                    {
                        return fail(RigV1Status::InvalidClip);
                    }
                    clip.RootMotionJoint = joint;
                    clip.RootMotion.push_back(
                        {F32(bytes, o + 8), F64(bytes, o + 16), F64(bytes, o + 24), F64(bytes, o + 32)});
                }
            }
            if (bAnalysis)
            {
                if (analysisSection.Count != data->Clips.size())
                {
                    return fail(RigV1Status::InvalidClip);
                }
                data->Analyses.resize(data->Clips.size());
                for (size_t i = 0; i < data->Analyses.size(); ++i)
                {
                    auto& a = data->Analyses[i];
                    const size_t o = size_t(analysisSection.Offset) + i * 96;
                    const uint32_t flags = U32(bytes, o + 4);
                    if (flags > 3 || !Zero(bytes, o + 72, o + 96))
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    a.RootJoint = U32(bytes, o);
                    a.bLoopCandidate = (flags & 1) != 0;
                    a.bLoop = (flags & 2) != 0;
                    double* values[] = {&a.LoopError,
                                        &a.DurationSeconds,
                                        &a.SourceFps,
                                        &a.TranslationX,
                                        &a.TranslationZ,
                                        &a.PlanarDistanceMeters,
                                        &a.AverageSpeedMetersPerSecond,
                                        &a.TotalYawRadians};
                    for (size_t k = 0; k < 8; ++k)
                    {
                        *values[k] = F64(bytes, o + 8 + k * 8);
                    }
                }
            }
            if (hasMetadata)
            {
                uint32_t nextEvent = 0, nextMarker = 0;
                for (size_t i = 0; i < data->Clips.size(); ++i)
                {
                    auto& m = data->Clips[i].Metadata;
                    const size_t o = size_t(metadataSections[2].Offset) + i * 48;
                    const uint32_t firstEvent = U32(bytes, o), events = U32(bytes, o + 4),
                                   firstMarker = U32(bytes, o + 8), markers = U32(bytes, o + 12),
                                   flags = U32(bytes, o + 16);
                    if (firstEvent != nextEvent || firstMarker != nextMarker || events > 4096 || markers > 256 ||
                        events > metadataSections[0].Count - nextEvent ||
                        markers > metadataSections[1].Count - nextMarker || flags > 63 || (flags & 3) > 2 ||
                        !Zero(bytes, o + 40, o + 48))
                        return fail(RigV1Status::BadWire);
                    nextEvent += events;
                    nextMarker += markers;
                    m.Root.Mode = Animation::RootMotionMode(flags & 3);
                    m.Loop.bEnabled = (flags & 4) != 0;
                    m.Root.bX = (flags & 8) != 0;
                    m.Root.bZ = (flags & 16) != 0;
                    m.Root.bYaw = (flags & 32) != 0;
                    m.Root.Joint = U32(bytes, o + 20);
                    m.Loop.Start = F32(bytes, o + 24);
                    m.Loop.End = F32(bytes, o + 28);
                    m.Root.NominalSpeed = F32(bytes, o + 32);
                    m.GroundOffset = F32(bytes, o + 36);
                    for (uint32_t n = 0; n < events; ++n)
                    {
                        const size_t e = size_t(metadataSections[0].Offset) + size_t(firstEvent + n) * 32;
                        C::AnsiString utf8;
                        C::String text;
                        if (!name(U64(bytes, e), U32(bytes, e + 8), utf8) || !NativeName(utf8, text))
                            return fail(RigV1Status::InvalidName);
                        m.Events.push_back({Identity(text), F32(bytes, e + 12), F32(bytes, e + 16), F32(bytes, e + 20),
                                            F32(bytes, e + 24), std::bit_cast<int32_t>(U32(bytes, e + 28))});
                    }
                    for (uint32_t n = 0; n < markers; ++n)
                    {
                        const size_t e = size_t(metadataSections[1].Offset) + size_t(firstMarker + n) * 16;
                        C::AnsiString utf8;
                        C::String text;
                        if (!name(U64(bytes, e), U32(bytes, e + 8), utf8) || !NativeName(utf8, text))
                            return fail(RigV1Status::InvalidName);
                        m.Markers.push_back({Identity(text), F32(bytes, e + 12)});
                    }
                }
                if (nextEvent != metadataSections[0].Count || nextMarker != metadataSections[1].Count)
                    return fail(RigV1Status::BadWire);
            }
            report.Status = ValidateData(*data, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            data->PayloadHash = payloadHash;
            data->bParsedFromWire = true;
            report.SkeletonId = data->Topology.SkeletonId;
            report.BankPayloadHash = payloadHash;
            ClipBankV1 candidate;
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
} // namespace NorvesLib::Core::Skeletal
