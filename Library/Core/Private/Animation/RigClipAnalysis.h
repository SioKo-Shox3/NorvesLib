#pragma once
// cook時の下書き解析。ゲーム側のイベント/接地/ルート移動の適用は行わない。
#include "Resource/RigAuthoring.h"
namespace NorvesLib::Core::Skeletal
{
    enum class RigClipLoopMode : uint8_t
    {
        Auto,
        Loop,
        Once
    };
    struct RigClipAnalysisOptions
    {
        RigClipLoopMode Loop = RigClipLoopMode::Auto;
        double LoopThreshold = 0.01;
        // 0なら作者restのjoint位置の対角、退化骨格なら1mを使う。
        double ReferenceLengthMeters = 0;
        // 未指定は骨格根の最初の子。子がなければ根。作者source順の番号。
        uint32_t RootJoint = UINT32_MAX;
        double SampleRate = 30;
        uint32_t MaximumSamples = 4096;
        // 時刻倍率 = TimeScale * AuthoredFps / SourceFps。fps補正には両方の明示が必要。
        double TimeScale = 1;
        double AuthoredFps = 0, SourceFps = 0;
    };
    struct RigClipAnalysis
    {
        uint32_t RootJoint = 0;
        bool bLoopCandidate = false, bLoop = false;
        double LoopError = 0, DurationSeconds = 0, SourceFps = 0;
        double TranslationX = 0, TranslationZ = 0;
        double PlanarDistanceMeters = 0, AverageSpeedMetersPerSecond = 0, TotalYawRadians = 0;
    };
    // clipは作者source順。成功時だけownedClip/analysisを置換する。
    [[nodiscard]] bool AnalyzeRigClip(const RigAuthoringCpu& author, const SkeletalAnimationClip& clip,
                                      const RigClipAnalysisOptions& options, SkeletalAnimationClip& ownedClip,
                                      RigClipAnalysis& analysis);
} // namespace NorvesLib::Core::Skeletal
