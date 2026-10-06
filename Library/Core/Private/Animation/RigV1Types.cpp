#include "Animation/RigV1Types.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Math/MathTypes.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iterator>
namespace NorvesLib::Core::Skeletal
{
    namespace C = Container;
    namespace
    {
        constexpr uint64_t Offset = 14695981039346656037ull, Prime = 1099511628211ull;
        void Append32(C::VariableArray<uint8_t>& bytes, uint32_t value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<uint8_t>(value >> shift));
            }
        }
        bool Less(const C::AnsiString& a, const C::AnsiString& b)
        {
            const int order = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
            return order < 0 || (order == 0 && a.size() < b.size());
        }
        bool Encode(const C::String& name, C::AnsiString& out, const RigV1Limits& limits)
        {
            const C::Span<const C::String::value_type> input{name.data(), name.size()};
            const auto measured = Asset::MeasureSkeletalNameEncoding(2, input);
            if (!measured.Succeeded() || measured.ByteCount == 0 || measured.ByteCount > limits.MaxNameBytes)
            {
                return false;
            }
            C::VariableArray<uint8_t> bytes(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, input, {bytes.data(), bytes.size()}).Succeeded())
            {
                return false;
            }
            out = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return true;
        }
    } // namespace
    bool IsRepresentableSkeletalRotation(const SkeletalValue& value) noexcept
    {
        const float norm = value.X * value.X + value.Y * value.Y + value.Z * value.Z + value.W * value.W;
        return std::isfinite(norm) && norm > Math::Constants::EPSILON;
    }
    bool IsValidSkeletalRestTransform(const SkeletalRestTransform& v) noexcept
    {
        if (!std::isfinite(v.Translation.X) || !std::isfinite(v.Translation.Y) || !std::isfinite(v.Translation.Z) ||
            !std::isfinite(v.Scale.X) || !std::isfinite(v.Scale.Y) || !std::isfinite(v.Scale.Z) || v.Scale.X <= 0 ||
            v.Scale.Y <= 0 || v.Scale.Z <= 0)
        {
            return false;
        }
        return IsRepresentableSkeletalRotation(v.Rotation);
    }
    bool IsValidRigV1Limits(const RigV1Limits& v) noexcept
    {
        const RigV1Limits hard;
        return v.MaxJoints && v.MaxJoints <= hard.MaxJoints && v.MaxClips && v.MaxClips <= hard.MaxClips &&
               v.MaxSnapshots && v.MaxSnapshots <= hard.MaxSnapshots && v.MaxChannels &&
               v.MaxChannels <= hard.MaxChannels && v.MaxSamples && v.MaxSamples <= hard.MaxSamples && v.MaxNameBytes &&
               v.MaxNameBytes <= hard.MaxNameBytes && v.MaxStringBytes && v.MaxStringBytes <= hard.MaxStringBytes &&
               v.MaxWireBytes >= 480 && v.MaxWireBytes <= hard.MaxWireBytes && v.MaxSourceBytes &&
               v.MaxSourceBytes <= hard.MaxSourceBytes && v.MaxBufferBytes && v.MaxBufferBytes <= hard.MaxBufferBytes &&
               v.MaxNodes && v.MaxNodes <= hard.MaxNodes && v.MaxAccessors && v.MaxAccessors <= hard.MaxAccessors &&
               v.MaxBuffers && v.MaxBuffers <= hard.MaxBuffers && v.MaxVertices && v.MaxVertices <= hard.MaxVertices &&
               v.MaxIndices && v.MaxIndices <= hard.MaxIndices;
    }
    bool IsValidRigBindingPolicy(const RigBindingPolicy& policy) noexcept
    {
        const auto& t = policy.Tolerance;
        return std::isfinite(t.TranslationMeters) && t.TranslationMeters >= 0 && std::isfinite(t.RotationRadians) &&
               t.RotationRadians >= 0 && std::isfinite(t.LogScale) && t.LogScale >= 0;
    }
    uint64_t RigBytesHash(C::Span<const uint8_t> bytes) noexcept
    {
        uint64_t hash = Offset;
        for (uint8_t b : bytes)
        {
            hash = (hash ^ b) * Prime;
        }
        return hash;
    }
    RigV1Status BuildRigTopology(C::Span<const SkeletalJoint> source, const RigV1Limits& limits, RigTopology& out)
    {
        if (!IsValidRigV1Limits(limits) || source.empty() || !source.data())
        {
            return RigV1Status::InvalidInput;
        }
        if (source.size() > limits.MaxJoints)
        {
            return RigV1Status::LimitExceeded;
        }
        RigTopology candidate;
        C::VariableArray<C::AnsiString> names(source.size());
        size_t nameBytes = 0;
        candidate.CanonicalToSource.resize(source.size());
        candidate.SourceToCanonical.resize(source.size());
        for (size_t i = 0; i < source.size(); ++i)
        {
            if (!Encode(source[i].Name, names[i], limits))
            {
                return RigV1Status::InvalidName;
            }
            if (names[i].size() > limits.MaxStringBytes - nameBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            nameBytes += names[i].size();
            candidate.CanonicalToSource[i] = static_cast<uint32_t>(i);
            const auto p = source[i].ParentIndex;
            if (p < -1 || p >= static_cast<int32_t>(source.size()) || p == static_cast<int32_t>(i))
            {
                return RigV1Status::InvalidTopology;
            }
        }
        // 最大128の有限walkでcycleも親後置も扱う。
        for (size_t i = 0; i < source.size(); ++i)
        {
            int32_t cursor = static_cast<int32_t>(i);
            for (size_t n = 0; cursor >= 0; ++n)
            {
                if (n >= source.size())
                {
                    return RigV1Status::InvalidTopology;
                }
                cursor = source[static_cast<size_t>(cursor)].ParentIndex;
            }
        }
        std::sort(candidate.CanonicalToSource.begin(), candidate.CanonicalToSource.end(),
                  [&](uint32_t a, uint32_t b) { return Less(names[a], names[b]); });
        for (size_t i = 0; i < source.size(); ++i)
        {
            const auto index = candidate.CanonicalToSource[i];
            candidate.SourceToCanonical[index] = static_cast<uint32_t>(i);
            if (i && names[index] == names[candidate.CanonicalToSource[i - 1]])
            {
                return RigV1Status::InvalidName;
            }
        }
        static constexpr uint8_t prefix[] = {'N', 'V', 'S', 'K', 'E', 'L', '_', 'T',
                                             'O', 'P', 'O', 'L', 'O', 'G', 'Y', 0};
        candidate.CanonicalBytes.insert(candidate.CanonicalBytes.end(), std::begin(prefix), std::end(prefix));
        Append32(candidate.CanonicalBytes, 1);
        Append32(candidate.CanonicalBytes, static_cast<uint32_t>(source.size()));
        candidate.Joints.reserve(source.size());
        for (uint32_t index : candidate.CanonicalToSource)
        {
            const auto p = source[index].ParentIndex;
            const int32_t parent =
                p < 0 ? -1 : static_cast<int32_t>(candidate.SourceToCanonical[static_cast<size_t>(p)]);
            RigTopologyJoint joint;
            joint.Name = names[index];
            joint.ParentIndex = parent;
            Append32(candidate.CanonicalBytes, static_cast<uint32_t>(joint.Name.size()));
            for (unsigned char c : joint.Name)
            {
                candidate.CanonicalBytes.push_back(c);
            }
            Append32(candidate.CanonicalBytes, static_cast<uint32_t>(parent));
            candidate.Joints.push_back(std::move(joint));
        }
        candidate.SkeletonId = RigBytesHash({candidate.CanonicalBytes.data(), candidate.CanonicalBytes.size()});
        out = std::move(candidate);
        return RigV1Status::Success;
    }
    bool SameRigTopology(const RigTopology& a, const RigTopology& b) noexcept
    {
        return !a.Joints.empty() && !b.Joints.empty() && !a.CanonicalBytes.empty() && a.SkeletonId == b.SkeletonId &&
               a.CanonicalBytes == b.CanonicalBytes;
    }
    uint64_t RigRestHash(C::Span<const SkeletalRestTransform> values) noexcept
    {
        uint64_t hash = Offset;
        const auto add = [&](float value)
        {
            const uint32_t bits = std::bit_cast<uint32_t>(value);
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                hash = (hash ^ static_cast<uint8_t>(bits >> shift)) * Prime;
            }
        };
        for (const auto& v : values)
        {
            add(v.Translation.X);
            add(v.Translation.Y);
            add(v.Translation.Z);
            add(v.Rotation.X);
            add(v.Rotation.Y);
            add(v.Rotation.Z);
            add(v.Rotation.W);
            add(v.Scale.X);
            add(v.Scale.Y);
            add(v.Scale.Z);
        }
        return hash;
    }
} // namespace NorvesLib::Core::Skeletal
