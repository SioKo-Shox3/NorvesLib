#pragma once

namespace NorvesLib::Core::Rendering
{
    // 太陽の影の設定上書き。各値の 0 は ShadowMapPass の設定のままにする。
    struct SunShadowParameters
    {
        // 太陽の影を受ける最大のカメラからの距離（m）。0 は設定のまま（既定 80 m）。
        float MaxDistance = 0.0f;
        // CSM の分割の対数と一様の混ぜ（0〜1）。0 は設定のまま（既定 0.5）。
        float SplitLambda = 0.0f;
    };

    // 非有限・0 以下を「設定のまま」の 0 にし、SplitLambda の上限を 1 にする。
    SunShadowParameters SanitizeSunShadowParameters(const SunShadowParameters& parameters);
} // namespace NorvesLib::Core::Rendering
