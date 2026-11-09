#pragma once

#include "Container/Containers.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    // 負の上限値は指定なしを表す。
    struct RetargetReportLimits
    {
        double MaxJointErrorDegrees = -1;
        double MaxFootSlideMeters = -1;
        double MaxFootPenetrationMeters = -1;
    };

    struct RetargetClipReportEntry
    {
        Container::AnsiString Name; // UTF-8
        bool bRotationErrorMeasured = false;
        double RotationErrorMaxDegrees = 0;
        double RotationErrorMeanDegrees = 0;
        double RootScale = 1;
        double SourceRootDistanceMeters = 0;
        double TargetRootDistanceMeters = 0;
        double SourceLegLengthMeters = 0; // 0 は未計測
        double TargetLegLengthMeters = 0; // 0 は未計測
        Container::VariableArray<Container::AnsiString> UnmappedSourceJoints; // UTF-8
        Container::VariableArray<Container::AnsiString> UnmappedTargetJoints; // UTF-8
    };

    struct RetargetReportDocument
    {
        uint64_t TargetSkeletonId = 0;
        RetargetReportLimits Limits;
        Container::VariableArray<RetargetClipReportEntry> Clips;
    };

    enum class RetargetLimitStatus : uint8_t
    {
        Within,
        JointErrorExceeded,
        JointErrorUnmeasured,
        FootMetricsUnmeasured
    };

    struct RetargetLimitCheck
    {
        RetargetLimitStatus Status = RetargetLimitStatus::Within;
        size_t ClipIndex = SIZE_MAX;
        double Value = 0;
        double Limit = 0;
    };

    [[nodiscard]] RetargetLimitCheck CheckRetargetReportLimits(const RetargetReportDocument& document) noexcept;
    [[nodiscard]] const char* RetargetLimitStatusCode(RetargetLimitStatus status) noexcept;

    // 成功時のみ、out を UTF-8 の JSON に置き換える。
    [[nodiscard]] bool WriteRetargetReportJson(const RetargetReportDocument& document, const RetargetLimitCheck& check,
                                               Container::VariableArray<uint8_t>& out);
} // namespace NorvesLib::Core::Animation
