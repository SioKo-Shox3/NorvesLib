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
        int64_t MaxIkFailedFrames = -1; // IK の失敗コマ数の上限
    };

    struct RetargetFootContactEntry
    {
        Container::AnsiString Leg; // 足先のロール名(UTF-8)
        Container::VariableArray<uint32_t> Ranges; // 接地区間の半開区間を [begin0, end0, begin1, end1, ...] で保持
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
        bool bFootMetricsMeasured = false; // 足の測定値と接地区間が有効か
        double FootSlideBeforeMeters = 0; // 再接地前の足滑り
        double FootSlideAfterMeters = 0; // 再接地後の足滑り
        double FootPenetrationMeters = 0; // 地面へのめり込み
        double FootFloatMeters = 0; // 地面からの浮き
        double IkAdjustmentDegrees = 0; // IK の関節調整量
        double PelvisDropMeters = 0; // 骨盤を下げた距離
        uint32_t IkFailedFrames = 0; // IK が失敗したコマ数
        Container::VariableArray<RetargetFootContactEntry> FootContacts; // 脚ごとの接地区間
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
        FootMetricsUnmeasured,
        FootSlideExceeded, // 再接地後の足滑りが上限を超えた
        FootPenetrationExceeded, // めり込みが上限を超えた
        IkFailedFramesExceeded // IK の失敗コマ数が上限を超えた
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
