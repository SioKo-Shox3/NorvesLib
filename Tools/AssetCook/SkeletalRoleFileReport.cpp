#include "SkeletalRoleFileReport.h"
#include "SingleAssetCook.h"
#include "Asset/AssetManifest.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace C = Core::Container;
        namespace A = Core::Animation;
        struct Json
        {
            C::AnsiString Text;
            bool bValid = true;
            void Append(const char* bytes, size_t count)
            {
                constexpr size_t Maximum = 4u * 1024u * 1024u;
                if (count > Maximum - Text.size())
                {
                    bValid = false;
                    return;
                }
                Text.append(bytes, count);
            }
            void Raw(const char* text)
            {
                Append(text, std::strlen(text));
            }
            void Quote(C::AnsiStringView value)
            {
                Raw("\"");
                constexpr char Hex[] = "0123456789abcdef";
                for (unsigned char c : value)
                {
                    if (c == '"' || c == '\\')
                    {
                        const char escaped[] = {'\\', static_cast<char>(c)};
                        Append(escaped, 2);
                    }
                    else if (c < 32)
                    {
                        const char escaped[] = {'\\', 'u', '0', '0', Hex[c >> 4], Hex[c & 15]};
                        Append(escaped, 6);
                    }
                    else
                    {
                        const char unit = static_cast<char>(c);
                        Append(&unit, 1);
                    }
                }
                Raw("\"");
            }
            void Unsigned(uint64_t value)
            {
                char bytes[32];
                const auto r = std::to_chars(bytes, bytes + sizeof(bytes), value);
                if (r.ec != std::errc{})
                {
                    bValid = false;
                    return;
                }
                Append(bytes, static_cast<size_t>(r.ptr - bytes));
            }
            void Number(double value)
            {
                if (!std::isfinite(value))
                {
                    bValid = false;
                    return;
                }
                char bytes[64];
                const auto r = std::to_chars(bytes, bytes + sizeof(bytes), value, std::chars_format::general,
                                             std::numeric_limits<double>::max_digits10);
                if (r.ec != std::errc{})
                {
                    bValid = false;
                    return;
                }
                Append(bytes, static_cast<size_t>(r.ptr - bytes));
            }
            void Flag(bool value)
            {
                Raw(value ? "true" : "false");
            }
            void Frame(uint32_t value)
            {
                if (value == UINT32_MAX)
                {
                    Raw("null");
                }
                else
                {
                    Unsigned(value);
                }
            }
            void Range(const A::SkeletalBvhClipRange& range)
            {
                if (!range.bHasValue)
                {
                    Raw("null");
                    return;
                }
                Raw("[");
                Number(range.Minimum);
                Raw(",");
                Number(range.Maximum);
                Raw("]");
            }
            void Ranges(const C::FixedArray<A::SkeletalBvhClipRange, 3>& ranges)
            {
                Raw("[");
                for (size_t i = 0; i < 3; ++i)
                {
                    if (i)
                    {
                        Raw(",");
                    }
                    Range(ranges[i]);
                }
                Raw("]");
            }
        };
        bool Name(const C::String& name, C::AnsiString& out)
        {
            const C::Span<const C::String::value_type> units{name.data(), name.size()};
            const auto size = Core::Asset::MeasureSkeletalNameEncoding(2, units);
            if (!size.Succeeded())
            {
                return false;
            }
            C::VariableArray<uint8_t> bytes(size.ByteCount);
            if (!Core::Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
            {
                return false;
            }
            out = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return true;
        }
    } // namespace
    bool BuildSkeletalRoleFileReport(const SingleAssetCookRequest& request, const SkeletalRoleProfileCookResult& result,
                                     uint64_t dependencyHash, C::AnsiString& outJson, C::AnsiString& error)
    {
        const auto& r = result.Cooked.Report;
        if (result.ExpandedRoles.size() != r.Mappings.size())
        {
            error = "role_report: mapping_count";
            return false;
        }
        C::AnsiString clipName;
        if (!Name(request.RoleProfile.ClipName, clipName))
        {
            error = "role_report: clip_name";
            return false;
        }
        Json j;
        j.Raw("{\"version\":1,\"operation\":");
        j.Quote(request.RoleProfile.Operation == SkeletalBvhClipOperation::Add ? "add" : "replace");
        j.Raw(",\"clip_name\":");
        j.Quote(clipName);
        j.Raw(",\"clip_index\":");
        j.Unsigned(result.Cooked.ClipIndex);
        j.Raw(",\"source_hash\":");
        j.Quote(Core::Asset::FormatAssetHashHex(result.Cooked.Cook.SourceHash));
        j.Raw(",\"dependency_hash\":");
        j.Quote(Core::Asset::FormatAssetHashHex(dependencyHash));
        j.Raw(",\"profile\":{\"parser_revision\":");
        j.Unsigned(result.ParserRevision);
        j.Raw(",\"vocabulary_revision\":");
        j.Unsigned(result.VocabularyRevision);
        j.Raw(",\"bytes\":");
        j.Unsigned(result.RawProfileBytes);
        j.Raw(",\"hash\":");
        j.Quote(Core::Asset::FormatAssetHashHex(result.RawProfileHash));
        j.Raw("},\"expanded_roles\":[");
        for (size_t i = 0; i < result.ExpandedRoles.size(); ++i)
        {
            if (i)
            {
                j.Raw(",");
            }
            j.Raw("{\"role\":");
            j.Quote(A::SkeletalRoleName(result.ExpandedRoles[i].Role));
            j.Raw(",\"ordinal\":");
            j.Unsigned(result.ExpandedRoles[i].Ordinal);
            j.Raw(",\"source_joint\":");
            j.Unsigned(r.Mappings[i].SourceIndex);
            j.Raw(",\"target_joint\":");
            j.Unsigned(r.Mappings[i].TargetIndex);
            j.Raw("}");
        }
        j.Raw("],\"source_only_roles\":[");
        for (size_t i = 0; i < result.SourceOnlyRoles.size(); ++i)
        {
            if (i)
            {
                j.Raw(",");
            }
            j.Quote(A::SkeletalRoleName(result.SourceOnlyRoles[i]));
        }
        j.Raw("],\"source_frames\":");
        j.Unsigned(r.SourceFrames);
        j.Raw(",\"source_joints\":");
        j.Unsigned(r.SourceJoints);
        j.Raw(",\"target_joints\":");
        j.Unsigned(r.TargetJoints);
        j.Raw(",\"output_keys\":");
        j.Unsigned(r.OutputKeys);
        j.Raw(",\"time\":{\"mode\":");
        j.Quote(r.Settings.TimeMode == A::SkeletalBvhClipTimeMode::HeaderFrameTime ? "header_frame_time"
                                                                                   : "override_fps");
        j.Raw(",\"header_interval\":");
        j.Number(r.HeaderFrameTimeSeconds);
        j.Raw(",\"selected_interval\":");
        j.Number(r.SelectedIntervalSeconds);
        j.Raw(",\"stored_duration\":");
        j.Number(r.StoredDurationSeconds);
        j.Raw(",\"max_rounding_error\":");
        j.Number(r.MaximumTimeRoundingErrorSeconds);
        j.Raw("},\"stored_keys_validated\":");
        j.Flag(r.bStoredKeysValidated);
        j.Raw(",\"continuous_curve_validated\":");
        j.Flag(r.bContinuousCurveValidated);
        j.Raw(
            ",\"rotation_only\":true,\"rotation_policy\":\"preserve_heading_hold_translations\",\"root_motion_generated\":false");
        j.Raw(",\"max_key_rotation_error_radians\":");
        j.Number(r.MaximumKeyRotationErrorRadians);
        j.Raw(",\"hemisphere_flips\":");
        j.Unsigned(r.HemisphereFlips);
        j.Raw(",\"joint_frames\":");
        j.Unsigned(r.JointFrames);
        j.Raw(",\"work_units\":");
        j.Unsigned(r.WorkUnits);
        j.Raw(",\"planned_owned_bytes\":");
        j.Unsigned(r.PlannedOwnedBytes);
        j.Raw(",\"position_convention\":");
        j.Quote(r.Settings.Translation == Core::Bvh::TranslationConvention::OffsetPlusChannels ? "additive"
                                                                                               : "absolute");
        j.Raw(",\"root_position\":{\"original_offset\":[");
        j.Number(r.RootPosition.OriginalOffset.X);
        j.Raw(",");
        j.Number(r.RootPosition.OriginalOffset.Y);
        j.Raw(",");
        j.Number(r.RootPosition.OriginalOffset.Z);
        j.Raw("],\"position_mask\":");
        j.Unsigned(r.RootPosition.PositionMask);
        j.Raw(",\"position_before_rotations\":");
        j.Flag(r.RootPosition.bPositionBeforeRotations);
        j.Raw(",\"raw\":");
        j.Ranges(r.RootPosition.Raw);
        j.Raw(",\"delta\":");
        j.Ranges(r.RootPosition.Delta);
        j.Raw(",\"canonical_delta\":");
        j.Ranges(r.RootPosition.CanonicalDelta);
        j.Raw(",\"unavailable_delta_frames\":");
        j.Unsigned(r.RootPosition.UnavailableDeltaFrames);
        j.Raw(",\"first_unavailable_delta_frame\":");
        j.Frame(r.RootPosition.FirstUnavailableDeltaFrame);
        j.Raw(",\"first_delta_issue\":");
        j.Unsigned(static_cast<uint8_t>(r.RootPosition.FirstDeltaIssue));
        j.Raw(",\"unavailable_canonical_frames\":");
        j.Unsigned(r.RootPosition.UnavailableCanonicalFrames);
        j.Raw(",\"first_unavailable_canonical_frame\":");
        j.Frame(r.RootPosition.FirstUnavailableCanonicalFrame);
        j.Raw(",\"conversion_failure_frames\":");
        j.Unsigned(r.RootPosition.ConversionFailureFrames);
        j.Raw(",\"first_conversion_failure_frame\":");
        j.Frame(r.RootPosition.FirstConversionFailureFrame);
        j.Raw(",\"first_conversion_issue\":");
        j.Unsigned(static_cast<uint8_t>(r.RootPosition.FirstConversionIssue));
        j.Raw("},\"ignored_non_root_positions\":[");
        for (size_t i = 0; i < r.IgnoredNonRootPositions.size(); ++i)
        {
            if (i)
            {
                j.Raw(",");
            }
            const auto& p = r.IgnoredNonRootPositions[i];
            j.Raw("{\"joint\":");
            j.Unsigned(p.JointIndex);
            j.Raw(",\"channel\":");
            j.Unsigned(p.OriginalChannelIndex);
            j.Raw(",\"kind\":");
            j.Unsigned(static_cast<uint8_t>(p.Channel));
            j.Raw(",\"range\":");
            j.Range(p.Raw);
            j.Raw("}");
        }
        j.Raw("],\"publication\":\"standalone_nontransactional\"}");
        if (!j.bValid)
        {
            error = "role_report: size_or_nonfinite";
            return false;
        }
        outJson = std::move(j.Text);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
