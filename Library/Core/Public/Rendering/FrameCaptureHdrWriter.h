#pragma once

#include "Container/String.h"

namespace NorvesLib::Core::Rendering
{
    struct CapturedFrame;

    /**
     * @brief 撮影で同じフレームから読み出したシーンの色（ブルーム前）を、放射輝度の HDR として書き出す
     *
     * FrameCaptureRequest::bIncludeSceneColor で得た CapturedFrame::SceneColor の画素をプリエクスポージャで割り、
     * 線形の Rec.709（白色点 D65）の放射輝度 cd/m² にする。pngPath の拡張子を除いた名前へ 2 つ書く。
     *   <名前>.hdr.pfm  … RGB の 32 bit 浮動小数（PFM。見出しは "PF"・幅 高さ・-1.0 の 3 行で、画素は下の行から・リトルエンディアン）
     *   <名前>.hdr.json … 大きさ・単位・プリエクスポージャ・フレーム番号・同じフレームの PNG の名前・表示変換の差し替え
     * 撮影の起動引数（--capture-hdr）のための開発ビルドの機能で、開発ビルドでないときは何も書かず false を返す。
     * @return シーンの色が読み出せていて、対応する形式で、2 つとも書けたら true
     */
    bool SaveCapturedSceneColorHdr(const CapturedFrame& frame, const Container::String& pngPath);

    /**
     * @brief 撮影で同じフレームから読み出した GBuffer の生の値（AOV）を、浮動小数の画像として書き出す
     *
     * FrameCaptureRequest::bIncludeAov で得た CapturedFrame::Aov を、pngPath の拡張子を除いた名前へ書く。
     * 画像はどれも PFM（32 bit 浮動小数・下の行から・リトルエンディアン）で、空の画素の深度は +∞。
     *   <名前>.aov.albedo.pfm    … RGB。GBuffer のアルベド（線形。照明が読む値）
     *   <名前>.aov.normal.pfm    … RGB。ワールドの法線（+Y が上。GBuffer の値のまま）
     *   <名前>.aov.roughness.pfm … 1 チャンネル（"Pf"）。粗さ
     *   <名前>.aov.depth.pfm     … 1 チャンネル（"Pf"）。カメラの前方向への距離 m（視線方向の深度）
     *   <名前>.aov.sun_visibility.pfm … 1 チャンネル（"Pf"）。照明が求めた太陽の可視率（0〜1。n·l は掛けない。空は 0、照明の出力が無い構成は -1）
     *   <名前>.aov.json          … 大きさ・フレーム番号・各画像の意味・カメラ（位置・向き・画角）
     * 撮影の起動引数（--capture-aov）のための開発ビルドの機能で、開発ビルドでないときは何も書かず false を返す。
     * @return AOV が読み出せていて、対応する形式で、すべて書けたら true
     */
    bool SaveCapturedAov(const CapturedFrame& frame, const Container::String& pngPath);

    /**
     * @brief 撮影で同じフレームから読み出した大気の書き出し（--atmosphere-dump）と、そのフレームの空の LUT・前計算の表を書き出す
     *
     * FrameCaptureRequest::bIncludeAtmosphere で得た CapturedFrame::Atmosphere を、pngPath の拡張子を除いた名前へ書く。
     * 数のファイルはどれも 32 bit 浮動小数（リトルエンディアン）の生の並びで、形・軸・単位は JSON に書く。
     *   <名前>.atmos.probe.f32               … 決めた視線の高さフォグと空気遠近（GPU が volumetrics.frag と同じ式で評価した値）
     *   <名前>.atmos.transmittance_lut.f32   … 空の透過率 LUT（GPU へ上げた半精度の値）
     *   <名前>.atmos.radiance_lut.f32        … 空の放射輝度 LUT（cd/m²。GPU へ上げた半精度の値）
     *   <名前>.atmos.transmittance_table.f32 … 透過率の前計算の表（光学的深さ）
     *   <名前>.atmos.multiple_scattering.f32 … 多重散乱の表 Ψ_ms（太陽の照度 1 lux あたりの cd/m²）
     *   <名前>.atmos.sky_view.f32            … sky-view の表（cd/m²）
     *   <名前>.atmos.json                    … 上の形・軸・単位、空・空気遠近・高さフォグのパラメータ、視線の一覧、地表の太陽と空の照度（lux）
     * LUT と表は撮影のフレームの空のパラメータから SkyAtmospherePass と同じ関数で作り直す（CPU で決まる値なので同じになる）。
     * 撮影の起動引数（--atmosphere-dump）のための開発ビルドの機能で、開発ビルドでないときは何も書かず false を返す。
     * @return 大気の書き出しが読み出せていて、すべて書けたら true
     */
    bool SaveCapturedAtmosphere(const CapturedFrame& frame, const Container::String& pngPath);

    /**
     * @brief 撮影で同じフレームから読み出した雲の書き出し（--cloud-dump）と、雲の密度場・照明の入力を書き出す
     *
     * FrameCaptureRequest::bIncludeClouds で得た CapturedFrame::Clouds を、pngPath の拡張子を除いた名前へ書く。
     *   <名前>.cloud.probe.f32           … 決めた画素の視線の雲（GPU が cloud_layer.frag と同じ式で評価した値。32 bit 浮動小数）
     *   <名前>.cloud.shape_noise.u8      … 密度場の基本の形の 3D ノイズ（RGBA8。CloudLayerPass が GPU へ上げる値）
     *   <名前>.cloud.detail_noise.u8     … 密度場の細部の 3D ノイズ（RGBA8）
     *   <名前>.cloud.sky_radiance_lut.f32 … 空の放射輝度 LUT（cd/m²。雲の環境光と雲の後ろの空を引く表）
     *   <名前>.cloud.json                … 上の形・軸、cloud_layer.frag の定数、格子の世界の座標・大きさ・補間・風のずれ・時刻、
     *                                     照明の式と係数（太陽・空・位相関数・多重散乱の近似・背景）、視線の画素の一覧
     * ノイズと LUT は撮影のフレームのパラメータから CloudLayerPass・SkyAtmospherePass と同じ関数で作り直す（CPU で決まる値）。
     * 撮影の起動引数（--cloud-dump）のための開発ビルドの機能で、開発ビルドでないときは何も書かず false を返す。
     * @return 雲の書き出しが読み出せていて、すべて書けたら true
     */
    bool SaveCapturedClouds(const CapturedFrame& frame, const Container::String& pngPath);
} // namespace NorvesLib::Core::Rendering
