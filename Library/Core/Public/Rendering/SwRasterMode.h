#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ソフトウェアラスタの使い方（起動引数 --sw-raster=off|on）
     *
     * Off（既定）は今のハードのラスタだけで、64bit のバッファ・その埋めと合流のパスも作らない。
     * On は MegaGeometry のクラスタのうち画面上で小さいもの（半径が SwRasterMaxPixels 以下で、近平面と交わらないもの）を、
     * カリングがパスごとのソフトの一覧へ振り分ける。ソフトのラスタが無い間は、On でもハードがすべてのクラスタを描き、
     * 一覧の数だけをログ（SW_RASTER_BIN）へ出す。64bit アトミックが使えない・ビジビリティバッファが無いときは振り分けず、
     * SW_RASTER_FALLBACK reason=<理由> を 1 回ログへ出す。
     */
    enum class SwRasterMode : uint32_t
    {
        Off = 0,
        On = 1
    };

    /** @brief ソフトウェアラスタの対象にする、クラスタの画面上の半径（画素）の既定のしきい値（起動引数 --sw-raster-max-px） */
    inline constexpr float DefaultSwRasterMaxPixels = 8.0f;
} // namespace NorvesLib::Core::Rendering
