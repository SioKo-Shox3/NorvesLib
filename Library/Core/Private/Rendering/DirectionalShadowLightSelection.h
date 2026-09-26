#pragma once

// CSMとRT影を掛ける方向光の選び方。影の行列を作る側（ShadowMapPass・RayTracingShadowPass）と、
// 影の係数を掛ける灯に印を付ける側（LightingPass）が同じ規則を共有する。

#include "Rendering/SceneProxy.h"
#include "Container/Containers.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief CSMとRT影を掛ける方向光を1つ選ぶ
     *
     * 空の太陽（SkySunLight.h）が影を落とせればそれを選び、他の方向光は影なしで照らす。
     * 空の太陽がなければ、表示される方向光がちょうど1つで、その灯が影を落とすときだけ
     * 選ぶ（同じ影を複数の方向光へ掛けない）。
     */
    const LightProxy* SelectShadowedDirectionalLight(
        const Container::VariableArray<LightProxy>* lightProxies);
} // namespace NorvesLib::Core::Rendering
