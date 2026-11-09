#pragma once

#include "Animation/SkeletalRoleProfile.h"

namespace NorvesLib::Core::Animation
{
    struct RetargetFootPlantingSettings
    {
        bool bEnabled = false;
        double HeightThresholdMeters = 0.03;
        double SpeedThresholdMetersPerSecond = 0.15;
        uint32_t MinStanceFrames = 3;
        uint32_t BlendFrames = 3;
        double GroundHeightMeters = 0;
    };

    struct RetargetFootPlantingLeg
    {
        // 語彙の足先の欄を脚の識別と報告に使う。
        SkeletalRole PawRole = SkeletalRole::Count;
        uint32_t Upper = UINT32_MAX;
        uint32_t Lower = UINT32_MAX;
        // UINT32_MAX は 2 ボーン、それ以外は 3 ボーンの脚。
        uint32_t Hock = UINT32_MAX;
        uint32_t Paw = UINT32_MAX;
    };

    // 各フレームの、ルート親空間からワールド空間への変換。
    struct RetargetFootPlantingRootParent
    {
        Bvh::Matrix3d Rotation;
        Bvh::Vector3d Translation;
    };

    struct RetargetFootPlantingInput
    {
        Container::Span<const int32_t> Parents;
        Container::Span<const Skeletal::SkeletalRestTransform> LocalRest;
        uint32_t RootJoint = 0;
        double FrameScale = 1;
        Container::Span<const RetargetFootPlantingRootParent> RootParents;
        Container::Span<const RetargetFootPlantingLeg> Legs;
        // 元の足先のワールド位置を [フレーム * 脚数 + 脚] に格納する。
        Container::Span<const Bvh::Vector3d> SourceFeet;
        double SourceFrameIntervalSeconds = 0;
    };

    struct RetargetFootContactRange
    {
        uint32_t Begin = 0, End = 0;
    };

    struct RetargetFootPlantingLegReport
    {
        // 語彙の足先の欄。
        SkeletalRole PawRole = SkeletalRole::Count;
        Container::VariableArray<RetargetFootContactRange> Contacts;
    };

    struct RetargetFootPlantingReport
    {
        bool bMeasured = false;
        double FootSlideBeforeMeters = 0, FootSlideAfterMeters = 0;
        double FootPenetrationMeters = 0, FootFloatMeters = 0;
        double IkAdjustmentDegrees = 0;
        uint32_t IkFailedFrames = 0;
        double PelvisDropMeters = 0;
        Container::VariableArray<RetargetFootPlantingLegReport> Legs;
    };

    // 全チャンネルのキー時刻を昇順・重複なしに集める。
    [[nodiscard]] bool CollectRetargetFootPlantingTimes(const Skeletal::SkeletalAnimationClip& clip,
                                                       Container::VariableArray<float>& times);

    // 成功時だけクリップと報告を書き換え、失敗時は機械照合用のエラーコードを返す。
    [[nodiscard]] bool ApplyRetargetFootPlanting(const RetargetFootPlantingInput& input,
                                                const RetargetFootPlantingSettings& settings,
                                                Skeletal::SkeletalAnimationClip& clip,
                                                RetargetFootPlantingReport& report, Container::AnsiString& error);
} // namespace NorvesLib::Core::Animation
