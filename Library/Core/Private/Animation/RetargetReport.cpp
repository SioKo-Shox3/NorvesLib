#include "Animation/RetargetReport.h"

#include "Asset/CookedSkeletalNameCodec.h"
#include "Text/JsonUnicodeScalar.h"
#include "Text/JsonWriter.h"
#include "Text/UnicodeText.h"
#include <cmath>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        namespace C = Container;

        bool DecodeName(const C::AnsiString& name, C::String& out)
        {
            // デコード途中の再確保による埋め込み NUL 以降の欠落を防ぐ。
            out.reserve(name.size());
            const C::Span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(name.data()), name.size()};
            return TextDetail::ForEachUnicodeScalar<uint8_t>(bytes, [&out](uint32_t scalar)
            {
                const auto encoded = TextDetail::EncodeJsonUnicodeScalar<C::String::value_type>(scalar);
                for (size_t index = 0; index < encoded.Count; ++index)
                {
                    out.push_back(encoded.Units[index]);
                }
            });
        }

        void WriteLimit(JsonWriter& writer, const char* key, double limit)
        {
            if (limit < 0)
            {
                writer.WriteNull(key);
            }
            else
            {
                writer.WriteNumber(key, limit);
            }
        }

        bool WriteNames(JsonWriter& writer, const char* key, const C::VariableArray<C::AnsiString>& names)
        {
            writer.BeginArray(key);
            for (const auto& name : names)
            {
                C::String native;
                if (!DecodeName(name, native))
                {
                    return false;
                }
                writer.WriteStringElement(native);
            }
            writer.EndArray();
            return true;
        }

        template <typename TMetric>
        RetargetLimitCheck CheckFootLimit(const RetargetReportDocument& document, double limit,
                                           TMetric RetargetClipReportEntry::*metric,
                                           RetargetLimitStatus exceededStatus) noexcept
        {
            if (!(limit >= 0))
            {
                return {};
            }
            for (size_t index = 0; index < document.Clips.size(); ++index)
            {
                const auto& clip = document.Clips[index];
                if (!clip.bFootMetricsMeasured)
                {
                    return {RetargetLimitStatus::FootMetricsUnmeasured, index, 0, limit};
                }
                const double value = static_cast<double>(clip.*metric);
                if (value > limit)
                {
                    return {exceededStatus, index, value, limit};
                }
            }
            return {};
        }
    }

    RetargetLimitCheck CheckRetargetReportLimits(const RetargetReportDocument& document) noexcept
    {
        const auto& limits = document.Limits;
        auto footCheck = CheckFootLimit(document, limits.MaxFootSlideMeters,
                                        &RetargetClipReportEntry::FootSlideAfterMeters,
                                        RetargetLimitStatus::FootSlideExceeded);
        if (footCheck.Status != RetargetLimitStatus::Within)
        {
            return footCheck;
        }
        footCheck = CheckFootLimit(document, limits.MaxFootPenetrationMeters,
                                   &RetargetClipReportEntry::FootPenetrationMeters,
                                   RetargetLimitStatus::FootPenetrationExceeded);
        if (footCheck.Status != RetargetLimitStatus::Within)
        {
            return footCheck;
        }
        footCheck = CheckFootLimit(document, static_cast<double>(limits.MaxIkFailedFrames),
                                   &RetargetClipReportEntry::IkFailedFrames,
                                   RetargetLimitStatus::IkFailedFramesExceeded);
        if (footCheck.Status != RetargetLimitStatus::Within)
        {
            return footCheck;
        }
        if (limits.MaxJointErrorDegrees >= 0)
        {
            for (size_t index = 0; index < document.Clips.size(); ++index)
            {
                const auto& clip = document.Clips[index];
                if (!clip.bRotationErrorMeasured || std::isnan(clip.RotationErrorMaxDegrees))
                {
                    return {RetargetLimitStatus::JointErrorUnmeasured, index, 0, limits.MaxJointErrorDegrees};
                }
                if (clip.RotationErrorMaxDegrees > limits.MaxJointErrorDegrees)
                {
                    return {RetargetLimitStatus::JointErrorExceeded, index, clip.RotationErrorMaxDegrees,
                            limits.MaxJointErrorDegrees};
                }
            }
        }
        return {};
    }

    const char* RetargetLimitStatusCode(RetargetLimitStatus status) noexcept
    {
        switch (status)
        {
        case RetargetLimitStatus::Within:
            return "within";
        case RetargetLimitStatus::JointErrorExceeded:
            return "joint_error_exceeded";
        case RetargetLimitStatus::JointErrorUnmeasured:
            return "joint_error_unmeasured";
        case RetargetLimitStatus::FootMetricsUnmeasured:
            return "foot_metrics_unmeasured";
        case RetargetLimitStatus::FootSlideExceeded:
            return "foot_slide_exceeded";
        case RetargetLimitStatus::FootPenetrationExceeded:
            return "foot_penetration_exceeded";
        case RetargetLimitStatus::IkFailedFramesExceeded:
            return "ik_failed_frames_exceeded";
        }
        return "unknown";
    }

    bool WriteRetargetReportJson(const RetargetReportDocument& document, const RetargetLimitCheck& check,
                                 Container::VariableArray<uint8_t>& out)
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteUInt64("version", 1);

        constexpr char hex[] = "0123456789abcdef";
        char skeletonId[17] = {};
        for (size_t index = 0; index < 16; ++index)
        {
            skeletonId[index] = hex[(document.TargetSkeletonId >> ((15 - index) * 4)) & 0xf];
        }
        writer.WriteString("target_skeleton_id", skeletonId);

        writer.BeginObject("limits");
        WriteLimit(writer, "max_joint_error_deg", document.Limits.MaxJointErrorDegrees);
        WriteLimit(writer, "max_foot_slide_m", document.Limits.MaxFootSlideMeters);
        WriteLimit(writer, "max_foot_penetration_m", document.Limits.MaxFootPenetrationMeters);
        if (document.Limits.MaxIkFailedFrames < 0)
        {
            writer.WriteNull("max_ik_failed_frames");
        }
        else
        {
            writer.WriteInt64("max_ik_failed_frames", document.Limits.MaxIkFailedFrames);
        }
        writer.EndObject();

        writer.BeginObject("result");
        writer.WriteString("status", RetargetLimitStatusCode(check.Status));
        if (check.ClipIndex < document.Clips.size())
        {
            C::String native;
            if (!DecodeName(document.Clips[check.ClipIndex].Name, native))
            {
                return false;
            }
            writer.WriteString("clip", native);
        }
        else
        {
            writer.WriteNull("clip");
        }
        writer.WriteNumber("value", check.Status == RetargetLimitStatus::Within ? 0 : check.Value);
        writer.WriteNumber("limit", check.Status == RetargetLimitStatus::Within ? 0 : check.Limit);
        writer.EndObject();

        writer.BeginArray("clips");
        for (const auto& clip : document.Clips)
        {
            C::String native;
            if (!DecodeName(clip.Name, native))
            {
                return false;
            }
            writer.BeginObject();
            writer.WriteString("name", native);
            if (clip.bRotationErrorMeasured)
            {
                writer.BeginObject("rotation_retarget_error_deg");
                writer.WriteNumber("max", clip.RotationErrorMaxDegrees);
                writer.WriteNumber("mean", clip.RotationErrorMeanDegrees);
                writer.EndObject();
            }
            else
            {
                writer.WriteNull("rotation_retarget_error_deg");
            }
            writer.WriteNumber("root_scale", clip.RootScale);
            writer.BeginObject("root_motion_distance_m");
            writer.WriteNumber("source", clip.SourceRootDistanceMeters);
            writer.WriteNumber("target", clip.TargetRootDistanceMeters);
            writer.EndObject();
            if (clip.SourceLegLengthMeters == 0 && clip.TargetLegLengthMeters == 0)
            {
                writer.WriteNull("leg_length_m");
            }
            else
            {
                writer.BeginObject("leg_length_m");
                writer.WriteNumber("source", clip.SourceLegLengthMeters);
                writer.WriteNumber("target", clip.TargetLegLengthMeters);
                writer.EndObject();
            }
            if (clip.bFootMetricsMeasured)
            {
                writer.BeginObject("foot_slide_m");
                writer.WriteNumber("before", clip.FootSlideBeforeMeters);
                writer.WriteNumber("after", clip.FootSlideAfterMeters);
                writer.EndObject();
                writer.WriteNumber("foot_penetration_m", clip.FootPenetrationMeters);
                writer.WriteNumber("foot_float_m", clip.FootFloatMeters);
                writer.WriteNumber("ik_adjustment_deg", clip.IkAdjustmentDegrees);
                writer.WriteUInt64("ik_failed_frames", clip.IkFailedFrames);
                writer.WriteNumber("pelvis_drop_m", clip.PelvisDropMeters);
                writer.BeginArray("foot_contacts");
                for (const auto& contact : clip.FootContacts)
                {
                    C::String leg;
                    if (contact.Ranges.size() % 2 != 0 || !DecodeName(contact.Leg, leg))
                    {
                        return false;
                    }
                    writer.BeginObject();
                    writer.WriteString("leg", leg);
                    writer.BeginArray("frames");
                    for (size_t index = 0; index < contact.Ranges.size(); index += 2)
                    {
                        writer.BeginArray();
                        writer.WriteNumberElement(static_cast<double>(contact.Ranges[index]));
                        writer.WriteNumberElement(static_cast<double>(contact.Ranges[index + 1]));
                        writer.EndArray();
                    }
                    writer.EndArray();
                    writer.EndObject();
                }
                writer.EndArray();
            }
            else
            {
                writer.WriteNull("foot_slide_m");
                writer.WriteNull("foot_penetration_m");
                writer.WriteNull("foot_float_m");
                writer.WriteNull("ik_adjustment_deg");
                writer.WriteNull("ik_failed_frames");
                writer.WriteNull("pelvis_drop_m");
                writer.WriteNull("foot_contacts");
            }
            writer.BeginObject("unmapped_joints");
            if (!WriteNames(writer, "source", clip.UnmappedSourceJoints) ||
                !WriteNames(writer, "target", clip.UnmappedTargetJoints))
            {
                return false;
            }
            writer.EndObject();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
        if (!writer.IsComplete())
        {
            return false;
        }

        const auto json = writer.ToString();
        using Unit = C::String::value_type;
        const C::Span<const Unit> units{json.data(), json.size()};
        const auto size = Asset::MeasureSkeletalNameEncoding(2, units);
        if (!size.Succeeded())
        {
            return false;
        }
        C::VariableArray<uint8_t> bytes(size.ByteCount);
        if (!Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
        {
            return false;
        }
        out.swap(bytes);
        return true;
    }
} // namespace NorvesLib::Core::Animation
