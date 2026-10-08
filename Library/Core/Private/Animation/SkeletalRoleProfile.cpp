#include "SkeletalRoleProfile.h"
#include "SkeletalRotationRetargetMath.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <cstring>
#include <type_traits>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Status = SkeletalRoleProfileStatus;
        using Result = SkeletalRoleProfileResult;
        constexpr const char* RoleNames[] = {
            "root",          "pelvis",           "spine",         "neck",          "head",        "jaw",
            "tail",          "front_L_clavicle", "front_L_upper", "front_L_lower", "front_L_paw", "front_R_clavicle",
            "front_R_upper", "front_R_lower",    "front_R_paw",   "hind_L_thigh",  "hind_L_shin", "hind_L_hock",
            "hind_L_paw",    "hind_R_thigh",     "hind_R_shin",   "hind_R_hock",   "hind_R_paw"};
        static_assert(sizeof(RoleNames) / sizeof(RoleNames[0]) == SkeletalRoleCount);
        Result Fail(Status status, const char* field, uint32_t role = UINT32_MAX, size_t element = SIZE_MAX)
        {
            return {status, field, role, element};
        }
        bool Equal(const Container::String& value, const char* expected)
        {
            const size_t size = std::strlen(expected);
            if (value.size() != size)
            {
                return false;
            }
            for (size_t i = 0; i < size; ++i)
            {
                if (value[i] != expected[i])
                {
                    return false;
                }
            }
            return true;
        }
        bool StringIs(const JsonValue& value, const char* expected)
        {
            return value.IsString() && Equal(value.AsString(), expected);
        }
        uint32_t FindRole(const Container::String& name)
        {
            for (uint32_t i = 0; i < SkeletalRoleCount; ++i)
            {
                if (Equal(name, RoleNames[i]))
                {
                    return i;
                }
            }
            return UINT32_MAX;
        }
        template <size_t N> Result Object(const JsonValue& value, const char* const (&keys)[N], const char* field)
        {
            if (!value.IsObject())
            {
                return Fail(Status::InvalidType, field);
            }
            uint64_t mask = 0;
            static_assert(N <= 64);
            for (size_t i = 0; i < value.GetObjectSize(); ++i)
            {
                size_t key = 0;
                while (key < N && !Equal(value.GetMemberName(i), keys[key]))
                {
                    ++key;
                }
                if (key == N)
                {
                    return Fail(Status::UnknownField, field);
                }
                const uint64_t bit = uint64_t{1} << key;
                if ((mask & bit) != 0)
                {
                    return Fail(Status::DuplicateField, field);
                }
                mask |= bit;
            }
            return {Status::Success};
        }
        Result Preflight(Container::Span<const uint8_t> bytes, const SkeletalRoleProfileLimits& limits)
        {
            if (!Detail::RetargetValidSpan(bytes) || bytes.empty() || limits.MaxDepth == 0 || limits.MaxDepth > 64)
            {
                return Fail(Status::InvalidInput, "profile");
            }
            if (bytes.size() > limits.MaxInputBytes)
            {
                return Fail(Status::LimitExceeded, "profile.bytes");
            }
            uint32_t depth = 0;
            size_t tokens = 0;
            bool bString = false, bEscaped = false;
            for (uint8_t c : bytes)
            {
                if (bString)
                {
                    if (bEscaped)
                    {
                        bEscaped = false;
                    }
                    else if (c == '\\')
                    {
                        bEscaped = true;
                    }
                    else if (c == '"')
                    {
                        bString = false;
                    }
                    continue;
                }
                if (c == '"')
                {
                    bString = true;
                }
                if (c == '{' || c == '[')
                {
                    if (++depth > limits.MaxDepth)
                    {
                        return Fail(Status::LimitExceeded, "profile.depth");
                    }
                }
                else if (c == '}' || c == ']')
                {
                    if (depth == 0)
                    {
                        return Fail(Status::InvalidJson, "profile");
                    }
                    --depth;
                }
                if (c == '{' || c == '[' || c == ':' || c == ',')
                {
                    if (tokens == limits.MaxSyntaxTokens)
                    {
                        return Fail(Status::LimitExceeded, "profile.tokens");
                    }
                    ++tokens;
                }
            }
            if (bString || depth != 0)
            {
                return Fail(Status::InvalidJson, "profile");
            }
            return {Status::Success};
        }
        Result Name(const JsonValue& value, const SkeletalRoleProfileLimits& limits, uint64_t& total,
                    Container::VariableArray<uint8_t>& out, const char* field, uint32_t role, size_t element)
        {
            if (!value.IsString())
            {
                return Fail(Status::InvalidType, field, role, element);
            }
            const auto& name = value.AsString();
            using Char = std::remove_cv_t<std::remove_pointer_t<decltype(name.data())>>;
            const Container::Span<const Char> chars{name.data(), name.size()};
            const auto measured = Asset::MeasureSkeletalNameEncoding(2, chars);
            if (!measured.Succeeded() || measured.ByteCount == 0)
            {
                return Fail(Status::InvalidName, field, role, element);
            }
            if (measured.ByteCount > limits.MaxNameBytes || total > limits.MaxTotalNameBytes ||
                measured.ByteCount > limits.MaxTotalNameBytes - total)
            {
                return Fail(Status::LimitExceeded, field, role, element);
            }
            out.resize(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, chars, {out.data(), out.size()}).Succeeded())
            {
                return Fail(Status::InvalidName, field, role, element);
            }
            total += measured.ByteCount;
            return {Status::Success};
        }
        Result Settings(const JsonValue& root, SkeletalBvhClipSettings& out)
        {
            constexpr const char* axesKeys[] = {"up", "forward", "handedness"};
            constexpr const char* unitsKeys[] = {"position_scale"};
            constexpr const char* timeKeys[] = {"mode", "source_fps"};
            const auto axes = root.FindMember("axes"), units = root.FindMember("units"), time = root.FindMember("time");
            for (auto check :
                 {Object(axes, axesKeys, "axes"), Object(units, unitsKeys, "units"), Object(time, timeKeys, "time")})
            {
                if (!check.Succeeded())
                {
                    return check;
                }
            }
            constexpr const char* names[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            for (uint8_t i = 0; i < 6; ++i)
            {
                if (StringIs(axes.FindMember("up"), names[i]))
                {
                    out.Up = static_cast<AssetImport::SignedAxis>(i);
                }
                if (StringIs(axes.FindMember("forward"), names[i]))
                {
                    out.Forward = static_cast<AssetImport::SignedAxis>(i);
                }
            }
            if (StringIs(axes.FindMember("handedness"), "right"))
            {
                out.Handedness = SkeletalSourceHandedness::Right;
            }
            else if (StringIs(axes.FindMember("handedness"), "left"))
            {
                out.Handedness = SkeletalSourceHandedness::Left;
            }
            const auto scale = units.FindMember("position_scale");
            if (!scale.IsNumber() || !std::isfinite(scale.AsNumber()) || scale.AsNumber() <= 0)
            {
                return Fail(Status::InvalidSettings, "units.position_scale");
            }
            out.PositionScale = scale.AsNumber();
            SkeletalCoordinateConversion conversion;
            if (BuildSkeletalCoordinateConversion(out.Up, out.Forward, out.Handedness, out.PositionScale, conversion) !=
                SkeletalCoordinateStatus::Success)
            {
                return Fail(Status::InvalidSettings, "axes");
            }
            if (StringIs(root.FindMember("position_convention"), "additive"))
            {
                out.Translation = Bvh::TranslationConvention::OffsetPlusChannels;
            }
            else if (StringIs(root.FindMember("position_convention"), "absolute"))
            {
                out.Translation = Bvh::TranslationConvention::AbsoluteLocalChannels;
            }
            else
            {
                return Fail(Status::InvalidSettings, "position_convention");
            }
            if (StringIs(time.FindMember("mode"), "header_frame_time") && !time.HasMember("source_fps"))
            {
                out.TimeMode = SkeletalBvhClipTimeMode::HeaderFrameTime;
            }
            else if (StringIs(time.FindMember("mode"), "override_fps") && time.FindMember("source_fps").IsNumber())
            {
                out.TimeMode = SkeletalBvhClipTimeMode::OverrideFps;
                out.SourceFps = time.FindMember("source_fps").AsNumber();
                if (!std::isfinite(out.SourceFps) || out.SourceFps <= 0 || !std::isfinite(1 / out.SourceFps) ||
                    1 / out.SourceFps <= 0)
                {
                    return Fail(Status::InvalidSettings, "time.source_fps");
                }
            }
            else
            {
                return Fail(Status::InvalidSettings, "time");
            }
            out.SourceReuse = SkeletalSourceReusePolicy::Reject;
            out.Rotation = SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations;
            return {Status::Success};
        }
        Result ExtendedSettings(const JsonValue& root, SkeletalRoleProfile& out)
        {
            if (root.HasMember("rest_pose"))
            {
                const auto rest = root.FindMember("rest_pose");
                constexpr const char* keys[] = {"mode", "up_hint", "maximum_error_degrees"};
                auto result = Object(rest, keys, "rest_pose");
                if (!result.Succeeded())
                {
                    return result;
                }
                const auto mode = rest.FindMember("mode");
                if (StringIs(mode, "explicit"))
                {
                    out.RestMode = SkeletalRestCorrectionMode::Explicit;
                }
                else if (StringIs(mode, "match"))
                {
                    out.RestMode = SkeletalRestCorrectionMode::Match;
                }
                else if (StringIs(mode, "align_bones"))
                {
                    out.RestMode = SkeletalRestCorrectionMode::AlignBones;
                }
                else
                {
                    return Fail(Status::InvalidSettings, "rest_pose.mode");
                }
                if (rest.HasMember("up_hint"))
                {
                    const auto hint = rest.FindMember("up_hint");
                    if (!hint.IsArray() || hint.GetArraySize() != 3)
                    {
                        return Fail(Status::InvalidSettings, "rest_pose.up_hint");
                    }
                    double values[3];
                    for (size_t i = 0; i < 3; ++i)
                    {
                        const auto v = hint.GetArrayElement(i);
                        if (!v.IsNumber() || !std::isfinite(v.AsNumber()))
                        {
                            return Fail(Status::InvalidSettings, "rest_pose.up_hint");
                        }
                        values[i] = v.AsNumber();
                    }
                    const double length = std::hypot(values[0], values[1], values[2]);
                    if (!std::isfinite(length) || length < 1e-10)
                    {
                        return Fail(Status::InvalidSettings, "rest_pose.up_hint");
                    }
                    out.RestUpHint = {values[0] / length, values[1] / length, values[2] / length};
                }
                if (rest.HasMember("maximum_error_degrees"))
                {
                    const auto v = rest.FindMember("maximum_error_degrees");
                    if (!v.IsNumber() || !std::isfinite(v.AsNumber()) || v.AsNumber() < 0 || v.AsNumber() > 180)
                    {
                        return Fail(Status::InvalidSettings, "rest_pose.maximum_error_degrees");
                    }
                    out.MaximumRestErrorRadians = v.AsNumber() * 3.14159265358979323846 / 180;
                }
            }
            if (root.HasMember("root_scale"))
            {
                const auto value = root.FindMember("root_scale");
                if (StringIs(value, "auto_height"))
                {
                    out.bAutoRootHeight = true;
                }
                else if (value.IsNumber() && std::isfinite(value.AsNumber()) && value.AsNumber() > 0)
                {
                    out.RootScale = value.AsNumber();
                }
                else
                {
                    return Fail(Status::InvalidSettings, "root_scale");
                }
            }
            if (root.HasMember("processing"))
            {
                const auto processing = root.FindMember("processing");
                constexpr const char* keys[] = {"output_fps",          "loop_mode",      "start_seconds",
                                                "end_seconds",         "minimum_period", "maximum_period",
                                                "extract_root_motion", "exclude_roles"};
                auto result = Object(processing, keys, "processing");
                if (!result.Succeeded())
                {
                    return result;
                }
                out.bHasProcessing = true;
                const auto read = [&](const char* key, double& number)
                {
                    if (!processing.HasMember(key))
                    {
                        return true;
                    }
                    const auto value = processing.FindMember(key);
                    if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
                    {
                        return false;
                    }
                    number = value.AsNumber();
                    return true;
                };
                auto& p = out.Processing;
                if (!read("output_fps", p.OutputFps) || !read("start_seconds", p.RangeStart) ||
                    !read("end_seconds", p.RangeEnd) || !read("minimum_period", p.MinimumPeriod) ||
                    !read("maximum_period", p.MaximumPeriod) || p.OutputFps <= 0 || p.OutputFps > 1000 ||
                    p.MinimumPeriod <= 0 || p.MaximumPeriod <= p.MinimumPeriod)
                {
                    return Fail(Status::InvalidSettings, "processing");
                }
                if (processing.HasMember("loop_mode"))
                {
                    const auto mode = processing.FindMember("loop_mode");
                    if (StringIs(mode, "auto"))
                    {
                        p.Loop = SkeletalLoopSelection::Auto;
                    }
                    else if (StringIs(mode, "none"))
                    {
                        p.Loop = SkeletalLoopSelection::None;
                    }
                    else if (StringIs(mode, "range"))
                    {
                        p.Loop = SkeletalLoopSelection::Range;
                    }
                    else
                    {
                        return Fail(Status::InvalidSettings, "processing.loop_mode");
                    }
                }
                if ((p.Loop == SkeletalLoopSelection::Range && (p.RangeStart < 0 || p.RangeEnd <= p.RangeStart)) ||
                    (p.Loop != SkeletalLoopSelection::Range &&
                     (processing.HasMember("start_seconds") || processing.HasMember("end_seconds"))))
                {
                    return Fail(Status::InvalidSettings, "processing.range");
                }
                if (processing.HasMember("exclude_roles"))
                {
                    const auto roles = processing.FindMember("exclude_roles");
                    if (!roles.IsArray() || roles.GetArraySize() > SkeletalRoleCount)
                    {
                        return Fail(Status::InvalidSettings, "processing.exclude_roles");
                    }
                    for (size_t i = 0; i < roles.GetArraySize(); ++i)
                    {
                        const auto value = roles.GetArrayElement(i);
                        if (!value.IsString())
                        {
                            return Fail(Status::InvalidSettings, "processing.exclude_roles");
                        }
                        const auto role = FindRole(value.AsString());
                        if (role == UINT32_MAX || (out.LoopExcludedRoles & (uint32_t{1} << role)))
                        {
                            return Fail(Status::InvalidSettings, "processing.exclude_roles");
                        }
                        out.LoopExcludedRoles |= uint32_t{1} << role;
                    }
                }
                if (processing.HasMember("extract_root_motion"))
                {
                    const auto value = processing.FindMember("extract_root_motion");
                    if (!value.IsBoolean())
                    {
                        return Fail(Status::InvalidSettings, "processing.extract_root_motion");
                    }
                    p.bExtractRootMotion = value.AsBool();
                }
            }
            return {Status::Success};
        }
        Result Roles(const JsonValue& object, bool bTarget, const SkeletalRoleProfileLimits& limits,
                     SkeletalRoleProfile& profile)
        {
            const char* field = bTarget ? "target_roles" : "source_roles";
            if (!object.IsObject())
            {
                return Fail(Status::InvalidType, field);
            }
            uint32_t seen = 0;
            uint64_t totalElements = 0;
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto role = FindRole(object.GetMemberName(i));
                if (role == UINT32_MAX)
                {
                    return Fail(Status::UnknownRole, field);
                }
                const uint32_t bit = uint32_t{1} << role;
                if ((seen & bit) != 0)
                {
                    return Fail(Status::DuplicateField, field, role);
                }
                seen |= bit;
                const auto array = object.GetMemberValue(i);
                if (!array.IsArray())
                {
                    return Fail(Status::InvalidType, field, role);
                }
                const auto count = array.GetArraySize();
                if (count == 0 || (!IsSkeletalChainRole(static_cast<SkeletalRole>(role)) && count != 1))
                {
                    return Fail(Status::InvalidChain, field, role);
                }
                const auto maximum = bTarget ? limits.MaxMappings : limits.MaxSourceElements;
                if (totalElements > maximum || count > maximum - totalElements)
                {
                    return Fail(Status::LimitExceeded, field, role);
                }
                totalElements += count;
                for (size_t j = 0; j < count; ++j)
                {
                    const auto element = array.GetArrayElement(j);
                    Container::VariableArray<uint8_t> name;
                    if (!bTarget)
                    {
                        auto parsed = Name(element, limits, profile.NameBytes, name, field, role, j);
                        if (!parsed.Succeeded())
                        {
                            return parsed;
                        }
                        profile.Source[role].push_back(std::move(name));
                        continue;
                    }
                    constexpr const char* targetKeys[] = {"joint", "C"};
                    auto parsed = Object(element, targetKeys, field);
                    if (!parsed.Succeeded())
                    {
                        parsed.RoleIndex = role;
                        parsed.ElementIndex = j;
                        return parsed;
                    }
                    SkeletalRoleTarget target;
                    parsed = Name(element.FindMember("joint"), limits, profile.NameBytes, target.Name, field, role, j);
                    if (!parsed.Succeeded())
                    {
                        return parsed;
                    }
                    if (profile.RestMode != SkeletalRestCorrectionMode::Explicit)
                    {
                        if (element.HasMember("C"))
                        {
                            return Fail(Status::InvalidCorrection, field, role, j);
                        }
                        profile.Target[role].push_back(std::move(target));
                        continue;
                    }
                    const auto correction = element.FindMember("C");
                    if (!correction.IsArray() || correction.GetArraySize() != 9)
                    {
                        return Fail(Status::InvalidCorrection, field, role, j);
                    }
                    for (size_t k = 0; k < 9; ++k)
                    {
                        const auto number = correction.GetArrayElement(k);
                        if (!number.IsNumber() || !std::isfinite(number.AsNumber()))
                        {
                            return Fail(Status::InvalidCorrection, field, role, j);
                        }
                        target.Correction.Values[k] = number.AsNumber();
                    }
                    Bvh::Matrix3d projected;
                    if (!Detail::RetargetProjectRotation(target.Correction, projected))
                    {
                        return Fail(Status::InvalidCorrection, field, role, j);
                    }
                    profile.Target[role].push_back(std::move(target));
                }
            }
            if ((seen & 1) == 0)
            {
                return Fail(Status::MissingRole, field, 0);
            }
            if (bTarget)
            {
                profile.ExpandedMappings = static_cast<uint32_t>(totalElements);
            }
            return {Status::Success};
        }
    } // namespace
    const char* SkeletalRoleName(SkeletalRole role) noexcept
    {
        const auto index = static_cast<size_t>(role);
        return index < SkeletalRoleCount ? RoleNames[index] : "";
    }
    bool IsSkeletalChainRole(SkeletalRole role) noexcept
    {
        return role == SkeletalRole::Spine || role == SkeletalRole::Neck || role == SkeletalRole::Tail;
    }
    SkeletalRoleProfileResult ParseSkeletalRoleProfile(Container::Span<const uint8_t> bytes,
                                                       const SkeletalRoleProfileLimits& limits,
                                                       SkeletalRoleProfile& out)
    {
        auto result = Preflight(bytes, limits);
        if (!result.Succeeded())
        {
            return result;
        }
        JsonDocument document;
        const bool bBom = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf;
        const Container::Span<const uint8_t> json{bytes.data() + (bBom ? 3 : 0), bytes.size() - (bBom ? 3 : 0)};
        if (!JsonDocument::TryParseUtf8(json, document))
        {
            return Fail(Status::InvalidJson, "profile");
        }
        const auto root = document.GetRoot();
        constexpr const char* rootKeys[] = {
            "version",      "vocabulary",   "axes",           "units",     "position_convention", "time",
            "source_roles", "target_roles", "required_roles", "rest_pose", "root_scale",          "processing"};
        result = Object(root, rootKeys, "profile");
        if (!result.Succeeded())
        {
            return result;
        }
        const auto version = root.FindMember("version");
        if (!version.IsIntegerLiteral() || version.AsNumber() != 1)
        {
            return Fail(Status::UnsupportedVersion, "version");
        }
        if (!StringIs(root.FindMember("vocabulary"), "quadruped_v1"))
        {
            return Fail(Status::UnknownVocabulary, "vocabulary");
        }
        SkeletalRoleProfile candidate;
        result = Settings(root, candidate.Settings);
        if (!result.Succeeded())
        {
            return result;
        }
        result = ExtendedSettings(root, candidate);
        if (!result.Succeeded())
        {
            return result;
        }
        result = Roles(root.FindMember("source_roles"), false, limits, candidate);
        if (!result.Succeeded())
        {
            return result;
        }
        result = Roles(root.FindMember("target_roles"), true, limits, candidate);
        if (!result.Succeeded())
        {
            return result;
        }
        for (uint32_t i = 0; i < SkeletalRoleCount; ++i)
        {
            if (!candidate.Target[i].empty() && candidate.Target[i].size() != candidate.Source[i].size())
            {
                return Fail(Status::InvalidChain, "target_roles", i);
            }
        }
        const auto required = root.FindMember("required_roles");
        if (root.HasMember("required_roles"))
        {
            if (!required.IsArray() || required.GetArraySize() > SkeletalRoleCount)
            {
                return Fail(Status::InvalidType, "required_roles");
            }
            for (size_t i = 0; i < required.GetArraySize(); ++i)
            {
                const auto value = required.GetArrayElement(i);
                if (!value.IsString())
                {
                    return Fail(Status::InvalidType, "required_roles", UINT32_MAX, i);
                }
                const auto role = FindRole(value.AsString());
                if (role == UINT32_MAX)
                {
                    return Fail(Status::UnknownRole, "required_roles", UINT32_MAX, i);
                }
                const uint32_t bit = uint32_t{1} << role;
                if ((candidate.RequiredMask & bit) != 0)
                {
                    return Fail(Status::DuplicateField, "required_roles", role, i);
                }
                if (candidate.Target[role].empty())
                {
                    return Fail(Status::MissingRole, "required_roles", role, i);
                }
                candidate.RequiredMask |= bit;
            }
        }
        static_assert(std::is_nothrow_move_assignable_v<SkeletalRoleProfile>);
        out = std::move(candidate);
        return {Status::Success};
    }
} // namespace NorvesLib::Core::Animation
