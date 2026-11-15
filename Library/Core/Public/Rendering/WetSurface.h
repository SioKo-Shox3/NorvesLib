// Assets/Shaders/Common/WetSurface.glsl の式を同じ演算順で CPU に写す。
#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Core::Rendering
{
    // 既定値は仮であり、色と明度のルールに合わせて後で調整する。
    // 水膜の smoothstep のため poolStart は poolEnd より小さくする。
    struct WetSurfaceTuning
    {
        float darkenDielectric = 0.55f;
        float darkenMetal = 0.9f;
        float roughnessScale = 0.45f;
        float poolRoughness = 0.04f;
        float poolStart = 0.55f;
        float poolEnd = 0.95f;
        float poolCavityBase = 0.6f;
        float poolCavityScale = 0.4f;
        float poolUpScale = 4.0f;
        float poolUpBias = 3.0f;
    };

    // AI 生成メッシュで高さ・AO がないときは一様の凹みを渡す。
    constexpr float WET_SURFACE_DEFAULT_CAVITY = 0.5f;

    void ApplyWetSurface(Math::Vector3& albedo, float& roughness, float& metallic, Math::Vector3& normal,
                         const Math::Vector3& geomNormal, float cavity, float wetness, float exposure,
                         float response, const WetSurfaceTuning& tuning = WetSurfaceTuning{});
} // namespace NorvesLib::Core::Rendering
