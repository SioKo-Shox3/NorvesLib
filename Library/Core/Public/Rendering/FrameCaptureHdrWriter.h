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
     *   <名前>.aov.json          … 大きさ・フレーム番号・各画像の意味・カメラ（位置・向き・画角）
     * 撮影の起動引数（--capture-aov）のための開発ビルドの機能で、開発ビルドでないときは何も書かず false を返す。
     * @return AOV が読み出せていて、対応する形式で、すべて書けたら true
     */
    bool SaveCapturedAov(const CapturedFrame& frame, const Container::String& pngPath);
} // namespace NorvesLib::Core::Rendering
