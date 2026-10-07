#include "Asset/RigSplitWire.h"
#include "Animation/RigRootFrame.h"
#include "Asset/RigSplitAllocationTestAccess.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <iterator>
namespace NorvesLib::Core::Skeletal::SplitWire
{
    namespace C = Container;
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
        for (unsigned i = 0; i < 4; ++i)
        {
            b[o + i] = uint8_t(v >> (8 * i));
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
        const int order = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
        return order < 0 || (order == 0 && a.size() < b.size());
    }
    bool NativeName(const C::AnsiString& name, C::String& out)
    {
        using Char = C::String::value_type;
        const auto m = Asset::MeasureSkeletalNameDecoding<Char>(2, NameView(name));
        if (!m.Succeeded())
        {
            return false;
        }
        C::VariableArray<Char> units(m.CodeUnitCount);
        if (!Asset::DecodeSkeletalWireName(2, NameView(name), C::Span<Char>{units.data(), units.size()}).Succeeded())
        {
            return false;
        }
        out = C::String(C::StringView(units.data(), units.size()));
        return true;
    }
    bool ValidName(const C::AnsiString& name, const RigV1Limits& limits)
    {
        return !name.empty() && name.size() <= limits.MaxNameBytes &&
               Asset::MeasureSkeletalNameDecoding<char>(2, NameView(name)).Succeeded();
    }
    bool ReadName(View strings, uint64_t offset, uint32_t size, C::AnsiString& out, const RigV1Limits& limits)
    {
        if (!size || size > limits.MaxNameBytes || offset > strings.size() || size > strings.size() - offset)
        {
            return false;
        }
        const View part{strings.data() + offset, size};
        if (!Asset::MeasureSkeletalNameDecoding<char>(2, part).Succeeded())
        {
            return false;
        }
        out = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(part.data()), part.size()));
        return true;
    }
    RigV1Status PreflightName(View strings, uint64_t offset, uint32_t size, const RigV1Limits& limits,
                              uint64_t& remaining, bool bAllowEmpty)
    {
        if (!size)
        {
            return bAllowEmpty && offset == 0 ? RigV1Status::Success : RigV1Status::InvalidName;
        }
        if (size > limits.MaxNameBytes || size > remaining)
        {
            return RigV1Status::LimitExceeded;
        }
        if (offset > strings.size() || size > strings.size() - offset ||
            !Asset::MeasureSkeletalNameDecoding<char>(2, {strings.data() + offset, size}).Succeeded())
        {
            return RigV1Status::InvalidName;
        }
        remaining -= size;
        return RigV1Status::Success;
    }
    uint64_t AppendName(Bytes& bytes, const C::AnsiString& name)
    {
        const uint64_t offset = bytes.size();
        for (unsigned char c : name)
        {
            bytes.push_back(c);
        }
        return offset;
    }
    void WriteRest(Bytes& b, size_t o, const SkeletalRestTransform& r)
    {
        const float f[] = {r.Translation.X, r.Translation.Y, r.Translation.Z, r.Rotation.X, r.Rotation.Y,
                           r.Rotation.Z,    r.Rotation.W,    r.Scale.X,       r.Scale.Y,    r.Scale.Z};
        for (size_t i = 0; i < 10; ++i)
        {
            WF(b, o + i * 4, f[i]);
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
    namespace
    {
        bool KnownRoleTable(uint32_t code, RigImportProfile profile)
        {
            constexpr uint32_t codes[] = {
                Four('S', 'T', 'R', 'S'), Four('T', 'J', 'N', 'T'), Four('R', 'S', 'E', 'T'), Four('A', 'R', 'S', 'T'),
                Four('R', 'O', 'O', 'T'), Four('S', 'R', 'E', 'F'), Four('V', 'E', 'R', 'T'), Four('I', 'N', 'D', 'X'),
                Four('I', 'B', 'M', 'S'), Four('M', 'N', 'G', 'T'), Four('S', 'U', 'B', 'M'), Four('M', 'S', 'L', 'T'),
                Four('M', 'A', 'T', 'S'), Four('C', 'L', 'I', 'P'), Four('C', 'H', 'A', 'N'), Four('S', 'A', 'M', 'P')};
            return (IsStaticRootFrameProfile(profile) && code == Four('A', 'F', 'R', 'M')) ||
                   std::find(std::begin(codes), std::end(codes), code) != std::end(codes);
        }
    } // namespace
    RigV1Status ReadEnvelope(View b, uint32_t role, C::Span<Section> expected, const RigV1Limits& limits,
                             RigImportProfile profile)
    {
        if (!IsValidRigProfileLimits(profile, limits) || !b.data() || expected.empty() || expected.size() > 16 ||
            (role != 1 && role != 2) || !IsSupportedRigImportProfile(profile))
        {
            return RigV1Status::InvalidInput;
        }
        if (b.size() > limits.MaxWireBytes)
        {
            return RigV1Status::LimitExceeded;
        }
        if (b.size() < 256 || std::memcmp(b.data(), "NVSKELv1", 8) != 0)
        {
            return RigV1Status::BadWire;
        }
        if (U32(b, 12) != 1 || U32(b, 20) != role || U32(b, 64) != uint32_t(profile) || U32(b, 68) != 1)
        {
            return RigV1Status::UnsupportedVersion;
        }
        const uint32_t count = U32(b, 28);
        if (U32(b, 8) != 256 || U32(b, 16) != 0x01020304 || U32(b, 24) || count < expected.size() || count > 16 ||
            U64(b, 32) != 256 || U64(b, 40) != b.size() || !Zero(b, 72, 256) || 256ull + count * 32ull > b.size())
        {
            return RigV1Status::BadWire;
        }
        if (U64(b, 48) != RigBytesHash({b.data() + 256, b.size() - 256}))
        {
            return RigV1Status::HashMismatch;
        }
        Section directory[16]{}, resolved[16]{};
        bool found[16]{};
        bool bUnsupported = false;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t o = 256 + i * 32;
            Section s{U32(b, o), U32(b, o + 24), U32(b, o + 28), U64(b, o + 8), U64(b, o + 16)};
            const auto flags = U32(b, o + 4);
            if (flags > 1 || s.Offset < 256ull + count * 32ull || s.Offset % 16 || s.Offset > b.size() ||
                s.Size > b.size() - s.Offset || !s.Record || uint64_t(s.Record) * s.Count != s.Size)
            {
                return RigV1Status::BadWire;
            }
            for (uint32_t j = 0; j < i; ++j)
            {
                if (directory[j].Code == s.Code)
                {
                    return RigV1Status::BadWire;
                }
            }
            size_t index = expected.size();
            for (size_t j = 0; j < expected.size(); ++j)
            {
                if (s.Code == expected[j].Code)
                {
                    index = j;
                    break;
                }
            }
            if (index < expected.size())
            {
                if (flags != 1 || s.Record != expected[index].Record)
                {
                    return RigV1Status::BadWire;
                }
                resolved[index] = s;
                found[index] = true;
            }
            else
            {
                bUnsupported = bUnsupported || flags == 1 || KnownRoleTable(s.Code, profile);
            }
            directory[i] = s;
        }
        // 未知optionalも整列・padding・重複・範囲を先に確認する。
        std::sort(directory, directory + count, [](const Section& a, const Section& c)
                  { return a.Offset < c.Offset || (a.Offset == c.Offset && a.Size < c.Size); });
        uint64_t end = 256ull + count * 32ull;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& s = directory[i];
            if (s.Offset < end || s.Offset - end >= 16 || !Zero(b, size_t(end), size_t(s.Offset)))
            {
                return RigV1Status::BadWire;
            }
            end = s.Offset + s.Size;
        }
        if (b.size() - end >= 16 || !Zero(b, size_t(end), b.size()))
        {
            return RigV1Status::BadWire;
        }
        if (bUnsupported)
        {
            return RigV1Status::UnsupportedSection;
        }
        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (!found[i])
            {
                return RigV1Status::BadWire;
            }
        }
        for (size_t i = 0; i < expected.size(); ++i)
        {
            expected[i] = resolved[i];
        }
        return RigV1Status::Success;
    }
    RigV1Status WriteEnvelope(uint32_t role, uint64_t skeletonId, C::Span<const OutputSection> sections, Bytes& out,
                              const RigV1Limits& limits, RigImportProfile profile)
    {
        if (!IsSupportedRigImportProfile(profile) || !IsValidRigProfileLimits(profile, limits) || sections.empty() ||
            sections.size() > 16 || (role != 1 && role != 2))
        {
            return RigV1Status::InvalidInput;
        }
        uint64_t total = 256 + sections.size() * 32;
        for (const auto& s : sections)
        {
            if (!s.Record || s.Data.size() % s.Record || s.Data.size() / s.Record > UINT32_MAX)
            {
                return RigV1Status::InvalidInput;
            }
            total = ((total + 15) & ~uint64_t{15}) + s.Data.size();
            if (total > limits.MaxWireBytes)
            {
                return RigV1Status::LimitExceeded;
            }
        }
        Bytes bytes(size_t(total), 0);
        std::memcpy(bytes.data(), "NVSKELv1", 8);
        W32(bytes, 8, 256);
        W32(bytes, 12, 1);
        W32(bytes, 16, 0x01020304);
        W32(bytes, 20, role);
        W32(bytes, 28, uint32_t(sections.size()));
        W64(bytes, 32, 256);
        W64(bytes, 40, total);
        W64(bytes, 56, skeletonId);
        W32(bytes, 64, uint32_t(profile));
        W32(bytes, 68, 1);
        size_t cursor = 256 + sections.size() * 32;
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto& s = sections[i];
            cursor = (cursor + 15) & ~size_t{15};
            const size_t o = 256 + i * 32;
            W32(bytes, o, s.Code);
            W32(bytes, o + 4, 1);
            W64(bytes, o + 8, cursor);
            W64(bytes, o + 16, s.Data.size());
            W32(bytes, o + 24, s.Record);
            W32(bytes, o + 28, uint32_t(s.Data.size() / s.Record));
            if (!s.Data.empty())
            {
                std::memcpy(bytes.data() + cursor, s.Data.data(), s.Data.size());
            }
            cursor += s.Data.size();
        }
        W64(bytes, 48, RigBytesHash({bytes.data() + 256, bytes.size() - 256}));
        out = std::move(bytes);
        return RigV1Status::Success;
    }
    RigV1Status ReadTopology(View b, View strings, const Section& section, const RigV1Limits& limits, RigTopology& out)
    {
        if (!section.Count || section.Count > limits.MaxJoints)
        {
            return RigV1Status::LimitExceeded;
        }
        uint64_t remaining = limits.MaxStringBytes;
        for (uint32_t i = 0; i < section.Count; ++i)
        {
            const size_t o = size_t(section.Offset) + i * 24;
            const auto status = PreflightName(strings, U64(b, o), U32(b, o + 8), limits, remaining);
            if (status != RigV1Status::Success)
            {
                return status;
            }
        }
        Detail::ObserveSplitAllocation("topology_owned");
        C::VariableArray<SkeletalJoint> joints(section.Count);
        C::AnsiString previous;
        uint32_t roots = 0;
        for (uint32_t i = 0; i < section.Count; ++i)
        {
            const size_t o = size_t(section.Offset) + i * 24;
            C::AnsiString name;
            if (!ReadName(strings, U64(b, o), U32(b, o + 8), name, limits) || (i && !NameLess(previous, name)) ||
                U64(b, o + 16) != RigBytesHash(NameView(name)) || !NativeName(name, joints[i].Name))
            {
                return RigV1Status::InvalidName;
            }
            joints[i].ParentIndex = std::bit_cast<int32_t>(U32(b, o + 12));
            if (joints[i].ParentIndex == -1)
            {
                ++roots;
            }
            previous = std::move(name);
        }
        if (roots != 1)
        {
            return RigV1Status::UnsupportedProfile;
        }
        RigTopology candidate;
        const auto status = BuildRigTopology({joints.data(), joints.size()}, limits, candidate);
        if (status != RigV1Status::Success)
        {
            return status;
        }
        if (candidate.SkeletonId != U64(b, 56))
        {
            return RigV1Status::HashMismatch;
        }
        out = std::move(candidate);
        return RigV1Status::Success;
    }
    void WriteTopology(const RigTopology& topology, Bytes& strings, Bytes& joints)
    {
        joints.resize(topology.Joints.size() * 24, 0);
        for (size_t i = 0; i < topology.Joints.size(); ++i)
        {
            const auto& j = topology.Joints[i];
            const size_t o = i * 24;
            W64(joints, o, AppendName(strings, j.Name));
            W32(joints, o + 8, uint32_t(j.Name.size()));
            W32(joints, o + 12, uint32_t(j.ParentIndex));
            W64(joints, o + 16, RigBytesHash(NameView(j.Name)));
        }
    }
    uint64_t RootIdentityHash()
    {
        uint8_t bytes[64]{};
        // 1.0fのLE表現。未初期化paddingやhost endianをhashしない。
        for (size_t i = 0; i < 4; ++i)
        {
            bytes[i * 20 + 2] = 0x80;
            bytes[i * 20 + 3] = 0x3f;
        }
        return RigBytesHash({bytes, 64});
    }
} // namespace NorvesLib::Core::Skeletal::SplitWire
