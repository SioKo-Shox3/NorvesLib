#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 太陽の影の方式（起動引数 --shadow-method=csm|vsm）
     *
     * Csm はカスケードシャドウマップだけ。SceneView・RenderWorld の構造体の既定で、検証シーン（golden）はこちら。
     * Vsm は太陽のクリップマップ（VirtualShadowMapClipmap）を毎フレーム CSM の行列と同じ場所で作り、
     * 物理ページへ影を描いて照明が読む。Game の既定（BootConfig::DefaultSunShadowMethod。--shadow-method=csm で CSM へ戻せる）。
     */
    enum class ShadowMethod : uint32_t
    {
        Csm = 0,
        Vsm = 1
    };
} // namespace NorvesLib::Core::Rendering
