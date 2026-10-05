#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ビジビリティバッファの使い方（起動引数 --visibility-buffer=off|on|debug）
     *
     * Off は今の GBuffer の描画だけで、ビジビリティバッファの資源もパスも作らない（On が使えない装置の予備と同じ描画）。
     * On（既定）は不透明の描画（MegaGeometry のクラスタ・手続きメッシュの塊・スキニングの塊）を VisBuffer.Id と GBuffer.Depth へ描き、
     * 幾何の解決（VisibilityResolvePass）が ID から GBuffer の Albedo・Normal・Velocity を書く。GBufferPass・MegaGeometryPass は
     * GBuffer の描画を止める（装置が対応しない・パイプラインが作れないときは従来どおり描き、
     * VISBUFFER_FALLBACK reason=<理由> を 1 回ログへ出す）。Material・Emissive は、材質の解決が対応するまで書かれない。
     * Debug は今の GBuffer の描画を残したまま、VisBuffer.Id と GBuffer.Depth へも描き、ID を色にして画面へ表示する（検証用）。
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
