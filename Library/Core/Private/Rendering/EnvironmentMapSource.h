// HDR正距円筒の環境マップを読み、ラスタのIBLとパストレーサーが共有する放射輝度へ換算する。
#pragma once

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ファイル値に輝度倍率を掛けて放射輝度（nits）にする。
     *
     * 非有限・負の値と、float16で表せない値（65504以上）は拒否する。
     */
    bool TryScaleEnvironmentSourceValue(float fileValue, double luminanceScale,
                                        float& outScaledValue);

    /**
     * @brief Assets相対パスを解決してHDRを読み、放射輝度のRGBA（A=1）配列にする。
     *
     * ラスタのLightingPassとパストレーサーは同じ関数で読み、同じ値の環境を使う。
     * 読み込み失敗・寸法不正・倍率不正・値の範囲外は偽を返す。
     */
    bool LoadEnvironmentRadianceSource(const Container::String& path, float luminanceScaleNits,
                                       Container::VariableArray<float>& outRgba,
                                       uint32_t& outWidth, uint32_t& outHeight);
}
