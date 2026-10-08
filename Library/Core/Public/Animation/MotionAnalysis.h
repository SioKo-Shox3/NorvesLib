#pragma once
#include "Animation/ClipMetadata.h"
#include "Animation/SkeletalPoseBuilder.h"
namespace NorvesLib::Core::Animation
{
    struct FootContactSpec
    {
        uint32_t Joint = UINT32_MAX;
        Identity Name;
    };
    struct FootContactOptions
    {
        double SampleRate = 60;
        float HeightThreshold = .03f, VerticalSpeedThreshold = .15f, MinimumWindowSeconds = .05f;
        uint32_t MaximumSamples = 65536;
    };
    struct FootContactWindow
    {
        Identity Name;
        float Start = 0, End = 0, Confidence = 0;
    };
    struct FootContactReport
    {
        Container::VariableArray<FootContactWindow> Windows;
        float GroundOffset = 0, Confidence = 0;
        uint32_t Samples = 0;
        // 手動確認用の下書き。Resource・ファイルには反映しない。
        ClipMetadata Draft;
        Container::String DraftJson;
    };
    struct CycleDetectionOptions
    {
        double SampleRate = 30, MinimumPeriod = .15, MaximumPeriod = 3;
        double RmsThresholdRadians = .05, MinimumMotionRadians = .05;
        uint32_t MaximumSamples = 65536, MaximumPoseEvaluations = 262144;
    };
    struct CycleDetectionReport
    {
        bool bDetected = false;
        double Start = 0, End = 0, Period = 0, RmsRadians = 0, Confidence = 0;
        ClipMetadata Draft;
        Container::String DraftJson;
    };
    // モデル空間Yの高さと鉛直速度から候補を得る。水平足滑りや地形接触は判定しない。
    [[nodiscard]] bool AnalyzeFootContacts(const SkeletalPoseContext&, const AnimationClipResource&,
                                           Container::Span<const FootContactSpec>, const FootContactOptions&,
                                           FootContactReport&);
    // G2の共通周期検出を利用する。関節の局所回転の再帰性であり、並進だけの周期は対象外。
    [[nodiscard]] bool DetectCycle(const SkeletonResource&, const SkeletalPoseContext&, const AnimationClipResource&,
                                   const CycleDetectionOptions&, CycleDetectionReport&);
} // namespace NorvesLib::Core::Animation
