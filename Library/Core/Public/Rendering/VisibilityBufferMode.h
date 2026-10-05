#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ビジビリティバッファの使い方（起動引数 --visibility-buffer=off|on|debug）
     *
     * Off（既定）は今の GBuffer の描画だけで、ビジビリティバッファの資源もパスも作らない。
     * On は不透明の描画（MegaGeometry のクラスタ・手続きメッシュの塊・スキニングの塊）を、今の GBuffer の描画に加えて
     * VisBuffer.Id と GBuffer.Depth へ描く。Debug は On に加えて、ID を色にして画面へ表示する（検証用）。
     */
    enum class VisibilityBufferMode : uint32_t
    {
        Off = 0,
        On = 1,
        Debug = 2
    };

    /** @brief ビジビリティバッファの描画（ID の書き込み）を行うモードか */
    constexpr bool IsVisibilityBufferActive(VisibilityBufferMode mode)
    {
        return mode != VisibilityBufferMode::Off;
    }
} // namespace NorvesLib::Core::Rendering
