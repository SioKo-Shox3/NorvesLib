#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ソフトウェアラスタの使い方（起動引数 --sw-raster=off|on）
     *
     * Off は今のハードのラスタだけで、64bit のバッファ・その埋めと合流のパスも作らない。
     * On（既定）は MegaGeometry のクラスタのうち画面上で小さいもの（半径が SwRasterMaxPixels 以下で、近平面と交わらないもの）を、
     * カリングがパスごとのソフトの一覧へ振り分ける。ソフトのラスタが無い間は、On でもハードがすべてのクラスタを描き、
     * 一覧の数だけをログ（SW_RASTER_BIN）へ出す。64bit アトミックが使えない・ビジビリティバッファが無いときは振り分けず、
     * SW_RASTER_FALLBACK reason=<理由> を 1 回ログへ出す。
     */
    enum class SwRasterMode : uint32_t
    {
        Off = 0,
        On = 1
    };

    /**
     * @brief ソフトウェアラスタの対象にする、クラスタの画面上の半径（画素）の既定のしきい値（起動引数 --sw-raster-max-px）
     *
     * RelWithDebInfo の GPU 時間（負荷モード 300 個と起動画面の各 3 視点）で 8・16・32・64 画素を比べ、
     * 負荷モードで 64 画素と同等に速く（off より速い）、起動画面では off と同じで VisibilityRasterPass が増えなかった 32 画素にした（64 画素は起動画面で VisibilityRasterPass が増える）。
     */
    inline constexpr float DefaultSwRasterMaxPixels = 32.0f;
} // namespace NorvesLib::Core::Rendering
