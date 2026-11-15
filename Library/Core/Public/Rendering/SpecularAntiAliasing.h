// 鏡面の AA の強さと GBuffer の書き手へ渡す定数を定義する。
#pragma once

namespace NorvesLib::Core::Rendering
{
    /// 鏡面の AA の強さ（σ² に掛ける係数）。0 で無効、1 で Tokuyoshi-Kaplanyan の式どおり。
    /// 既定値は仮で、段階的に導入するために変更できる。
    /// κ は Common/SpecularAntiAliasing.glsl の SPECULAR_AA_MAX_ALPHA_SQUARED_ADD にある。
    inline constexpr float SpecularAAStrength = 1.0f;

    /// GBuffer と MegaGeometry の UBO の vec4 を組み立てる。x は強さ、y・z・w は予約の 0。
    inline void MakeSpecularAAConstants(float outValues[4])
    {
        outValues[0] = SpecularAAStrength;
        outValues[1] = 0.0f;
        outValues[2] = 0.0f;
        outValues[3] = 0.0f;
    }
}
