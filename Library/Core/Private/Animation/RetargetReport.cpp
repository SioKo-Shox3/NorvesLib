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
    }

    RetargetLimitCheck CheckRetargetReportLimits(const RetargetReportDocument& document) noexcept
    {
        const auto& limits = document.Limits;
        if (limits.MaxFootSlideMeters >= 0)
        {
            return {RetargetLimitStatus::FootMetricsUnmeasured, SIZE_MAX, 0, limits.MaxFootSlideMeters};
        }
        if (limits.MaxFootPenetrationMeters >= 0)
        {
            return {RetargetLimitStatus::FootMetricsUnmeasured, SIZE_MAX, 0, limits.MaxFootPenetrationMeters};
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
