#pragma once

#include "RenderExtension/RenderExtensionData.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    /// Wind.h の WindClockSeconds と同じ 4096 秒単位の時刻基準。
    inline constexpr double VertexAnimationClockWrapSeconds = 4096.0;

    /// Common/VertexAnimation.glsl の試験の揺れと同じ振幅と周波数。
    inline constexpr float VertexAnimationTestSineAmplitudeMeters = 0.5f;
    inline constexpr float VertexAnimationTestSineFrequencyHz = 2.0f;

    /// 試験の正弦の揺れを選ぶ shadingParams[2] の値。
    inline constexpr float VertexAnimationKindTestSineParam = 1.0f;

    /// 最後に描画されたフレームから現在までの経過時間を倍精度の差分から求める。
    inline float VertexAnimationPreviousDeltaSeconds(const RenderExtensionFrameInfo& frame)
    {
        return static_cast<float>(frame.GameTotalTime - frame.PreviousRenderedTotalTime);
    }

    /// 現在時刻、前回描画時刻、試験の揺れの許可、0 を順に組み立てる。
    inline void MakeVertexAnimationTimeConstants(const RenderExtensionFrameInfo& frame, bool bTestDisplacement,
                                                 float outValues[4])
    {
        float time = static_cast<float>(std::fmod(frame.GameTotalTime, VertexAnimationClockWrapSeconds));
        if (time < 0.0f)
        {
            time += static_cast<float>(VertexAnimationClockWrapSeconds);
        }

        outValues[0] = time;
        outValues[1] = time - VertexAnimationPreviousDeltaSeconds(frame);
        outValues[2] = bTestDisplacement ? 1.0f : 0.0f;
        outValues[3] = 0.0f;
    }
} // namespace NorvesLib::Core::Rendering
