#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct SkyAtmosphereParameters;

    // 太陽の影の設定上書き。MaxDistance と SplitLambda の 0 は ShadowMapPass の設定のままにする。
    struct SunShadowParameters
    {
        // 太陽の影を受ける最大のカメラからの距離（m）。0 は設定のまま（既定 80 m）。
        float MaxDistance = 0.0f;
        // CSM の分割の対数と一様の混ぜ（0〜1）。0 は設定のまま（既定 0.5）。
        float SplitLambda = 0.0f;
        // false なら CSM・VSM の太陽の影を描かず、照明は影の可視を 1 として扱う。
        bool bEnabled = true;
        // 空の太陽の地表照度がこの値未満なら影を省く（lux）。0 は自動停止しない。
        float AutoDisableBelowSunIlluminanceLux = 0.0f;
        // CSM・点光源キューブへ描く MegaGeometry 投影物 1 つの三角形数の上限。超える物は描かず、0 は上限なし。
        uint32_t MaxShadowCasterTriangles = 150000;
    };

    // MaxDistance・SplitLambda の非有限・0 以下を 0 にし、SplitLambda の上限を 1 にする。
    // 自動停止の照度が非有限・負なら 0 にし、残りの欄はそのまま写す。
    SunShadowParameters SanitizeSunShadowParameters(const SunShadowParameters& parameters);

    // このフレームの太陽の影の状態。
    struct SunShadowFrameState
    {
        bool bActive = true;           // CSM と VSM の太陽の影を描く。
        bool bAutoDisabled = false;    // 日差しが弱いので止めた。
        float SunIlluminanceLux = 0.0f; // 判定に使った空の太陽の地表照度。求めなかったときは 0。
    };

    // 設定と空の照度から、このフレームに太陽の影を描くか判定する。
    SunShadowFrameState EvaluateSunShadowFrameState(const SunShadowParameters& shadow,
                                                   const SkyAtmosphereParameters& sky);
} // namespace NorvesLib::Core::Rendering
