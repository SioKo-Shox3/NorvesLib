#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 太陽の影の方式（起動引数 --shadow-method=csm|vsm）
     *
     * Csm（既定）は今のカスケードシャドウマップだけ。
     * Vsm は太陽のクリップマップ（VirtualShadowMapClipmap）を毎フレーム CSM の行列と同じ場所で作って
     * PhysicalLighting へ公開する。段8の途中では描画は CSM のままで、見た目は変わらない。
     */
    enum class ShadowMethod : uint32_t
    {
        Csm = 0,
        Vsm = 1
    };
} // namespace NorvesLib::Core::Rendering
