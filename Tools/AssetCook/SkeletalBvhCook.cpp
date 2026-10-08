#include "SkeletalBvhCook.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include <bit>
#include <type_traits>

namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace
    {
        namespace A = Core::Animation;
        namespace C = Core::Container;
        template <class T> bool Valid(C::Span<T> span)
        {
            return A::Detail::RetargetValidSpan(span);
        }
        void AddInteger(uint64_t& hash, uint64_t value)
        {
            for (unsigned i = 0; i < 8; ++i)
            {
                hash ^= (value >> (i * 8)) & 255;
                hash *= 1099511628211ull;
            }
        }
        void AddBytes(uint64_t& hash, C::Span<const uint8_t> bytes)
        {
            AddInteger(hash, bytes.size());
            for (uint8_t value : bytes)
            {
                hash ^= value;
                hash *= 1099511628211ull;
            }
        }
        bool EncodeName(const C::String& name, C::VariableArray<uint8_t>& bytes)
        {
            using Char = std::remove_cv_t<std::remove_pointer_t<decltype(name.data())>>;
            const C::Span<const Char> chars{name.data(), name.size()};
            const auto measured = Core::Asset::MeasureSkeletalNameEncoding(2, chars);
            if (!measured.Succeeded())
            {
                return false;
            }
            bytes.resize(measured.ByteCount);
            return Core::Asset::EncodeSkeletalWireName(2, chars, {bytes.data(), bytes.size()}).Succeeded();
        }
        bool EqualFloat(float a, float b)
        {
            return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
        }
    } // namespace
    bool ApplyBvhCookRequest(const SkeletalBvhCookRequest& request, Core::Skeletal::SkeletalGltfData& target,
                             A::SkeletalBvhClipReport& report, uint32_t& clipIndex, C::AnsiString& error)
    {
        if ((request.Operation != SkeletalBvhClipOperation::Add &&
             request.Operation != SkeletalBvhClipOperation::Replace) ||
            request.ClipName.empty() || request.MaxNvskelBytes == 0 || !Valid(request.BvhBytes) ||
            !Valid(request.Mappings) || !Valid(request.Corrections) || !Valid(request.Root.SourceName) ||
            !Valid(request.Root.TargetName) || request.Mappings.empty() ||
            request.Mappings.size() != request.Corrections.size())
        {
            error = "BVH cookの明示要求が不正です";
            return false;
        }
        size_t matches = 0, selected = target.Clips.size();
        for (size_t i = 0; i < target.Clips.size(); ++i)
        {
            if (target.Clips[i].Name == request.ClipName)
            {
                ++matches;
                selected = i;
            }
        }
        if ((request.Operation == SkeletalBvhClipOperation::Add && matches != 0) ||
            (request.Operation == SkeletalBvhClipOperation::Replace && matches != 1) || selected >= UINT32_MAX)
        {
            error = "BVH clip名の追加/置換先が一意に決まりません";
            return false;
        }
        Core::Bvh::BvhDocument source;
        if (!Core::Bvh::DecodeBvh(request.BvhBytes, request.DecodeLimits, source).Succeeded())
        {
            error = "BVH sourceの解析に失敗しました";
            return false;
        }
        C::VariableArray<A::SkeletalJointNameView> sourceNames(source.Joints.size());
        for (size_t i = 0; i < source.Joints.size(); ++i)
        {
            sourceNames[i].Utf8 = {source.Joints[i].Name.data(), source.Joints[i].Name.size()};
        }
        A::SkeletalJointIndex sourceIndex, targetIndex;
        const auto& limits = request.ClipLimits.Source;
        const A::SkeletalJointIndexLimits sourceNameLimits{limits.MaxJoints, limits.MaxNameBytes,
                                                           limits.MaxTotalNameBytes};
        if (!A::BuildSkeletalJointIndex({sourceNames.data(), sourceNames.size()}, sourceNameLimits, sourceIndex)
                 .Succeeded() ||
            !A::BuildSkeletalJointIndexFromJoints({target.Joints.data(), target.Joints.size()},
                                                  request.ClipLimits.TargetNames, targetIndex)
                 .Succeeded())
        {
            error = "BVH cookの関節名索引が不正です";
            return false;
        }
        A::SkeletalJointMappingRoot root;
        A::SkeletalJointMappingSet mapping;
        if (!A::FindSkeletalJointIndex(sourceIndex, request.Root.SourceName, root.SourceIndex).Succeeded() ||
            !A::FindSkeletalJointIndex(targetIndex, request.Root.TargetName, root.TargetIndex).Succeeded() ||
            !A::ResolveSkeletalJointMappings(sourceIndex, targetIndex, request.Mappings, root,
                                             request.Settings.SourceReuse, request.MappingLimits, mapping)
                 .Succeeded())
        {
            error = "BVH cookの明示関節対応/root指定を解決できません";
            return false;
        }
        const auto& v = target.MeshNodeGlobalTransform;
        const Math::Matrix4x4 mesh(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12],
                                   v[13], v[14], v[15]);
        A::SkeletalBvhClipOutput imported;
        if (!A::ImportSkeletalBvhRotationClip(source, {target.Joints.data(), target.Joints.size()}, mesh, mapping,
                                              request.Corrections, request.ClipName, request.Settings,
                                              request.ClipLimits, imported)
                 .Succeeded())
        {
            error = "BVHの回転clip生成/格納key検証に失敗しました";
            return false;
        }
        if (request.Operation == SkeletalBvhClipOperation::Add)
        {
            target.Clips.push_back(std::move(imported.Clip));
        }
        else
        {
            target.Clips[selected] = std::move(imported.Clip);
        }
        report = std::move(imported.Report);
        clipIndex = static_cast<uint32_t>(selected);
        return true;
    }
    bool AppendBvhCookHash(uint64_t seed, const SkeletalBvhCookRequest& r, uint64_t& outHash)
    {
        C::VariableArray<uint8_t> clipName;
        if (!EncodeName(r.ClipName, clipName))
        {
            return false;
        }
        uint64_t hash = seed;
        // 形式tagと算法版。全scalarは明示little-endian、名前/bytesは長さ付き。
        AddInteger(hash, 0x425648434f4f4bull);
        AddInteger(hash, 1);
        AddBytes(hash, r.BvhBytes);
        AddInteger(hash, static_cast<uint8_t>(r.Operation));
        AddBytes(hash, {clipName.data(), clipName.size()});
        AddBytes(hash, r.Root.SourceName);
        AddBytes(hash, r.Root.TargetName);
        AddInteger(hash, r.Mappings.size());
        for (const auto& pair : r.Mappings)
        {
            AddBytes(hash, pair.SourceName);
            AddBytes(hash, pair.TargetName);
        }
        AddInteger(hash, r.Corrections.size());
        for (const auto& correction : r.Corrections)
        {
            for (double value : correction.Values)
            {
                AddInteger(hash, std::bit_cast<uint64_t>(value));
            }
        }
        const auto& s = r.Settings;
        AddInteger(hash, static_cast<uint8_t>(s.Up));
        AddInteger(hash, static_cast<uint8_t>(s.Forward));
        AddInteger(hash, static_cast<uint8_t>(s.Handedness));
        AddInteger(hash, std::bit_cast<uint64_t>(s.PositionScale));
        AddInteger(hash, static_cast<uint8_t>(s.Translation));
        AddInteger(hash, static_cast<uint8_t>(s.TimeMode));
        AddInteger(hash, std::bit_cast<uint64_t>(s.SourceFps));
        AddInteger(hash, static_cast<uint8_t>(s.SourceReuse));
        AddInteger(hash, static_cast<uint8_t>(s.Rotation));
        const auto& l = r.ClipLimits;
        const uint64_t values[] = {l.Source.MaxJoints,
                                   l.Source.MaxDepth,
                                   l.Source.MaxFrames,
                                   l.Source.MaxValues,
                                   l.Source.MaxNameBytes,
                                   l.Source.MaxTotalNameBytes,
                                   l.TargetNames.MaxJoints,
                                   l.TargetNames.MaxNameBytes,
                                   l.TargetNames.MaxTotalBytes,
                                   l.MaxClipNameBytes,
                                   l.MaxOutputKeys,
                                   l.MaxJointFrames,
                                   l.MaxWorkUnits,
                                   l.MaxOwnedBytes,
                                   r.MappingLimits.MaxCatalogJoints,
                                   r.MappingLimits.MaxMappings,
                                   r.MappingLimits.MaxTotalNameBytes,
                                   r.DecodeLimits.MaxInputBytes,
                                   r.DecodeLimits.MaxJoints,
                                   r.DecodeLimits.MaxDepth,
                                   r.DecodeLimits.MaxFrames,
                                   r.DecodeLimits.MaxValues,
                                   r.DecodeLimits.MaxNameBytes,
                                   r.DecodeLimits.MaxNumberBytes,
                                   r.MaxNvskelBytes};
        for (uint64_t value : values)
        {
            AddInteger(hash, value);
        }
        outHash = hash;
        return true;
    }
    bool EqualBvhCookClip(const Core::Skeletal::SkeletalAnimationClip& a,
                          const Core::Skeletal::SkeletalAnimationClip& b)
    {
        if (a.Name != b.Name || !EqualFloat(a.DurationSeconds, b.DurationSeconds) ||
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
            for (size_t j = 0; j < x.Samples.size(); ++j)
            {
                const auto& p = x.Samples[j];
                const auto& q = y.Samples[j];
                if (!EqualFloat(p.TimeSeconds, q.TimeSeconds) || !EqualFloat(p.Value.X, q.Value.X) ||
                    !EqualFloat(p.Value.Y, q.Value.Y) || !EqualFloat(p.Value.Z, q.Value.Z) ||
                    !EqualFloat(p.Value.W, q.Value.W))
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
