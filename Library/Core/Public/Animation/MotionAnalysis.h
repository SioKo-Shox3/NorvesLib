#pragma once
#include "Animation/ClipMetadata.h"
#include "Animation/SkeletalPoseBuilder.h"
namespace NorvesLib::Core::Animation
{
    struct FootContactSpec
    {
        uint32_t Joint = UINT32_MAX;
        /// 脚の記号(例 LF)。窓の名前になり、下書きの名前は Foot.<Name>.Down/Up。
        Identity Name;
    };
    struct FootContactOptions
    {
        double SampleRate = 60;
        float HeightThreshold = .03f, VerticalSpeedThreshold = .15f, MinimumWindowSeconds = .05f;
        /// 窓の中で高さと鉛直速度のしきい値に掛ける倍率。
        float Hysteresis = 1.5f;
        uint32_t MaximumSamples = 65536;
    };
    /// 等間隔の高さ標本に対する接地窓の半開区間 [Begin, End) と信頼度。
    struct FootContactSampleWindow
    {
        uint32_t Begin = 0, End = 0;
        float Confidence = 0;
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
    /// 1本の足の最低を基準に、高さと鉛直速度をヒステリシス付きで判定する。
    /// 最短時間未満の窓は除き、信頼度は標本ごとの高さ・速度の余裕を [0, 1] に制限した平均。
    /// 引数が不正なら false を返して out を保ち、成功時は採用した窓で置き換える。
    [[nodiscard]] bool DetectFootContactWindows(Container::Span<const float> heights, double sampleInterval,
                                               const FootContactOptions&,
                                               Container::VariableArray<FootContactSampleWindow>& out);
    /// 脚の記号から Foot.<leg>.Down または Foot.<leg>.Up を作る。
    [[nodiscard]] Identity FootContactEventName(Identity leg, bool bDown);
    /// 窓の時刻と脚の名前を検証し、不正なら出力を変えず false を返す。
    /// ループ時は既存 marker がない場合だけ、脚順に継ぎ目を考慮した最長窓の marker を生成し、
    /// 同時刻の2つ目以降を省いてその数を返す。非ループ時は窓ごとの点イベントを足し、終端の Up は省く。
    /// 生成した列は時刻で安定ソートする。窓イベントは生成しない。
    [[nodiscard]] bool AppendFootContactMetadata(Container::Span<const FootContactWindow> windows,
                                               Container::Span<const Identity> legs, float duration, bool bLoop,
                                               ClipMetadata& metadata, uint32_t& coincidentMarkersOmitted);
    // モデル空間Yの足ごとの最低を基準にヒステリシス付きで判定する。水平足滑りや地形接触は判定しない。
    [[nodiscard]] bool AnalyzeFootContacts(const SkeletalPoseContext&, const AnimationClipResource&,
                                           Container::Span<const FootContactSpec>, const FootContactOptions&,
                                           FootContactReport&);
    // G2の共通周期検出を利用する。関節の局所回転の再帰性であり、並進だけの周期は対象外。
    [[nodiscard]] bool DetectCycle(const SkeletonResource&, const SkeletalPoseContext&, const AnimationClipResource&,
                                   const CycleDetectionOptions&, CycleDetectionReport&);
} // namespace NorvesLib::Core::Animation
