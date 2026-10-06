#include "SkeletalRoleProfileCook.h"
#include <charconv>
#include <type_traits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace A = Core::Animation;
        namespace C = Core::Container;
        void Integer(uint64_t& hash, uint64_t value)
        {
            for (unsigned i = 0; i < 8; ++i)
            {
                hash ^= (value >> (i * 8)) & 255;
                hash *= 1099511628211ull;
            }
        }
        void Bytes(uint64_t& hash, C::Span<const uint8_t> bytes)
        {
            for (uint8_t b : bytes)
            {
                hash ^= b;
                hash *= 1099511628211ull;
            }
        }
        C::AnsiString Number(uint64_t value)
        {
            char text[32]{};
            const auto result = std::to_chars(text, text + sizeof(text) - 1, value);
            return result.ec == std::errc{} ? C::AnsiString(text) : C::AnsiString("?");
        }
        bool CheckSource(const SkeletalRoleProfileCookRequest& request, SkeletalRoleProfileCookResult& candidate,
                         C::AnsiString& error)
        {
            // 余分なsource roleも同じ実入力で検査する。この所有decodeはbridge呼出前に破棄する。
            Core::Bvh::BvhDocument source;
            const auto decoded = Core::Bvh::DecodeBvh(request.BvhBytes, request.DecodeLimits, source);
            if (!decoded.Succeeded())
            {
                error = "Profile sourceのBVH解析に失敗しました status=" + Number(static_cast<uint64_t>(decoded.Status));
                return false;
            }
            C::VariableArray<A::SkeletalJointNameView> names(source.Joints.size());
            for (size_t i = 0; i < source.Joints.size(); ++i)
            {
                names[i].Utf8 = {source.Joints[i].Name.data(), source.Joints[i].Name.size()};
            }
            A::SkeletalJointIndex index;
            const auto& l = request.ClipLimits.Source;
            const A::SkeletalJointIndexLimits limits{l.MaxJoints, l.MaxNameBytes, l.MaxTotalNameBytes};
            if (!A::BuildSkeletalJointIndex({names.data(), names.size()}, limits, index).Succeeded())
            {
                error = "Profile sourceの関節名索引を構築できません";
                return false;
            }
            for (size_t role = 0; role < A::SkeletalRoleCount; ++role)
            {
                const auto& entries = candidate.Profile.Source[role];
                for (size_t ordinal = 0; ordinal < entries.size(); ++ordinal)
                {
                    uint32_t found = UINT32_MAX;
                    const auto lookup =
                        A::FindSkeletalJointIndex(index, {entries[ordinal].data(), entries[ordinal].size()}, found);
                    if (!lookup.Succeeded())
                    {
                        error = C::AnsiString("Profile source roleの実関節名を解決できません role=") +
                                A::SkeletalRoleName(static_cast<A::SkeletalRole>(role)) +
                                " ordinal=" + Number(ordinal) +
                                " status=" + Number(static_cast<uint64_t>(lookup.Status));
                        return false;
                    }
                }
                if (!entries.empty() && candidate.Profile.Target[role].empty())
                {
                    candidate.SourceOnlyRoles.push_back(static_cast<A::SkeletalRole>(role));
                }
            }
            return true;
        }
    } // namespace
    bool CookGltfWithRoleProfileToNvskelNativePath(const uint8_t* sourceBytes, size_t sourceSize,
                                                   Core::Container::AnsiStringView format,
                                                   const std::filesystem::path& sourcePath,
                                                   const SkeletalRoleProfileCookRequest& request,
                                                   SkeletalRoleProfileCookResult& outResult,
                                                   Core::Container::AnsiString& error,
                                                   const Core::AssetImport::ImportSettingsFileOptions* importOptions,
                                                   const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions)
    {
        SkeletalRoleProfileCookResult candidate;
        const auto parsed = A::ParseSkeletalRoleProfile(request.ProfileBytes, request.ProfileLimits, candidate.Profile);
        if (!parsed.Succeeded())
        {
            error = "骨格role Profileが不正です field=" + parsed.Field +
                    " status=" + Number(static_cast<uint64_t>(parsed.Status));
            if (parsed.RoleIndex < A::SkeletalRoleCount)
            {
                error += " role=";
                error += A::SkeletalRoleName(static_cast<A::SkeletalRole>(parsed.RoleIndex));
            }
            if (parsed.ElementIndex != SIZE_MAX)
            {
                error += " ordinal=" + Number(parsed.ElementIndex);
            }
            return false;
        }
        if (candidate.Profile.ExpandedMappings > request.MappingLimits.MaxMappings)
        {
            error = "Profileの展開数がmapping予算を超えました";
            return false;
        }
        if (!CheckSource(request, candidate, error))
        {
            return false;
        }
        C::VariableArray<A::SkeletalJointMappingNameView> pairs;
        C::VariableArray<Core::Bvh::Matrix3d> corrections;
        pairs.reserve(candidate.Profile.ExpandedMappings);
        corrections.reserve(candidate.Profile.ExpandedMappings);
        candidate.ExpandedRoles.reserve(candidate.Profile.ExpandedMappings);
        for (uint32_t role = 0; role < A::SkeletalRoleCount; ++role)
        {
            const auto& target = candidate.Profile.Target[role];
            const auto& source = candidate.Profile.Source[role];
            for (uint32_t ordinal = 0; ordinal < target.size(); ++ordinal)
            {
                pairs.push_back({{source[ordinal].data(), source[ordinal].size()},
                                 {target[ordinal].Name.data(), target[ordinal].Name.size()}});
                corrections.push_back(target[ordinal].Correction);
                candidate.ExpandedRoles.push_back({static_cast<A::SkeletalRole>(role), ordinal});
            }
        }
        SkeletalBvhCookRequest typed;
        typed.BvhBytes = request.BvhBytes;
        typed.Mappings = {pairs.data(), pairs.size()};
        typed.Corrections = {corrections.data(), corrections.size()};
        // rootは正準順の先頭で、parserが両側長さ1を保証する。
        typed.Root = pairs[0];
        typed.ClipName = request.ClipName;
        typed.Operation = request.Operation;
        typed.Settings = candidate.Profile.Settings;
        typed.ClipLimits = request.ClipLimits;
        typed.MappingLimits = request.MappingLimits;
        typed.DecodeLimits = request.DecodeLimits;
        typed.MaxNvskelBytes = request.MaxNvskelBytes;
        if (!CookGltfWithBvhToNvskelNativePath(sourceBytes, sourceSize, format, sourcePath, typed, candidate.Cooked,
                                               error, importOptions, decodeOptions))
        {
            return false;
        }
        candidate.RawProfileBytes = request.ProfileBytes.size();
        candidate.RawProfileHash = 14695981039346656037ull;
        Bytes(candidate.RawProfileHash, request.ProfileBytes);
        auto& hash = candidate.Cooked.Cook.SourceHash;
        Integer(hash, 0x524f4c4550524f46ull);
        Integer(hash, candidate.ParserRevision);
        Integer(hash, candidate.VocabularyRevision);
        Integer(hash, request.ProfileBytes.size());
        Bytes(hash, request.ProfileBytes);
        const auto& l = request.ProfileLimits;
        const uint64_t limits[] = {l.MaxInputBytes, l.MaxDepth,     l.MaxSyntaxTokens,  l.MaxSourceElements,
                                   l.MaxMappings,   l.MaxNameBytes, l.MaxTotalNameBytes};
        for (uint64_t value : limits)
        {
            Integer(hash, value);
        }
        static_assert(std::is_nothrow_move_assignable_v<SkeletalRoleProfileCookResult>);
        outResult = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
