// 1 画素未満の毛や草などの細い帯を広げ、ジオメトリのエイリアシングを抑える。
// 元の幅に応じた被覆率を返し、広げた帯の平均の被覆を保つ。
#ifndef NORVES_THIN_GEOMETRY_GLSL
#define NORVES_THIN_GEOMETRY_GLSL

// 帯の投影幅の下限（画素）。
const float THIN_GEOMETRY_MIN_PIXEL_WIDTH = 1.0;

// worldWidth は帯本来のワールド幅（m）。
// viewDepth はカメラの前方向への正のビュー深度（m）。
// projFactor はビュー深度 1 m での 1 m 当たりの画素数。
// 透視投影では 0.5 * 描画高さ（画素） * projection[1][1] を使う。
// 直交投影では viewDepth に 1、projFactor に同じ式の値を渡す。
// 戻り値の x は広げたワールド幅（m）、y は元の幅 / 広げた幅の被覆率。
// worldWidth が正でないか NaN のときは (0, 0) を返す。
// viewDepth または projFactor が正でないか NaN のときは (worldWidth, 1) を返す。
//
// GR34 のファー（シェル）と GR43 の草の頂点シェーダーは x の幅で頂点を置き、
// y を varying でフラグメントシェーダーへ渡す。
// フラグメントシェーダーは y を AlphaClipShouldDiscard の coverage に掛け、
// ALPHA_CLIP_MODE_HASH を使うことで元の幅と同じ平均の被覆を保つ。
// ALPHA_CLIP_MODE_TEST は cutoff と比べるため、細い帯が丸ごと消える。
// Hash の雑音は TAA がフレームにわたって平均する前提であり、TAA のない
// 経路（FXAA）では点描に見える。Hash と TAA の相性は G5-GR30-TAA-THIN-CHECK で測る。
// velocity は現在と前フレームの頂点をどちらも同じ式で広げてから求める。
// 幅の広げ方が異なると、動いていない帯にも速度が生じる。
vec2 ClampStrandWidth(float worldWidth, float viewDepth, float projFactor)
{
    if (!(worldWidth > 0.0))
    {
        return vec2(0.0, 0.0);
    }
    if (!(viewDepth > 0.0) || !(projFactor > 0.0))
    {
        return vec2(worldWidth, 1.0);
    }

    float minWorldWidth = THIN_GEOMETRY_MIN_PIXEL_WIDTH * viewDepth / projFactor;
    float clampedWidth = max(worldWidth, minWorldWidth);
    return vec2(clampedWidth, worldWidth / clampedWidth);
}

#endif
