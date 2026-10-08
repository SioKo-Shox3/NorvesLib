#pragma once

// MegaGeometry のクラスタカリングの定数バッファ（Common/MegaGeometryCull.glsl の CullUniforms。std140）の CPU 側の並び。
// 主の経路（MegaGeometryPass）と、VSM の MegaGeometry の投影物のカリング（VirtualShadowMapMegaCull）が同じ構造を使う。

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /**
     * @brief カリング用ユニフォームデータ（GPU送信用。Common/MegaGeometryCull.glsl の CullUniforms と一致）
     *
     * インスタンスごとに変わる値（ワールド変換・LODの球・クラスタ数）はインスタンスの表にあり、ここには無い。
     */
    struct alignas(16) CullUniformData
    {
        float ViewMatrix[16];
        float ProjectionMatrix[16];
        float CameraPosition[4];   // xyz + pad
        float FrustumPlanes[6][4]; // 錐台の 6 平面。各平面は (nx, ny, nz, d)
        uint32_t InstanceCount;    // インスタンスの表の要素数
        uint32_t TotalGroupCount;  // 全インスタンスのワークグループ（64クラスタ）の数
        float LODBias;
        float ScreenHeight;     // スクリーン高さ（ピクセル）
        float ProjectionFactor; // screenHeight / (2 * tan(fov/2))
        uint32_t HiZWidth;      // Hi-Zの元になった深度の幅（Hi-Zのミップ0はその半分）
        uint32_t HiZHeight;     // Hi-Zの元になった深度の高さ（Hi-Zのミップ0はその半分）
        uint32_t HiZMipCount;   // ミップレベル数
        uint32_t bHiZEnabled;   // Hi-Z有効フラグ（1=有効, 0=無効）
        uint32_t DebugPayloadMode; // firstInstanceへ書き込むデバッグpayload種別
        uint32_t CullPass;      // 0=従来（遮蔽の判定なし）, 1=1パス目, 2=2パス目
        uint32_t bStatsEnabled; // 1なら統計バッファへ数える
        uint32_t SectionBase;   // 区間の表・カウンタのうちこのパスの先頭（1パス目は0、2パス目は区間の数）
        uint32_t VisibleReadStamp;  // 1パス目が「前のフレームで見えた」とみなす印の値（前のフレームの2パス目が書いた値）
        uint32_t VisibleWriteStamp; // 2パス目が見えたクラスタへ書く印の値
        uint32_t BvhStage;      // BVH のたどり: 節の判定の段の番号（BvhStageClusters なら葉のクラスタの判定。平らな判定では使わない）
        uint32_t BvhInputBase;  // この段の入力の列の先頭（要素）
        uint32_t BvhNextBase;   // 次の段の列の先頭
        uint32_t BvhLeafBase;   // 葉の列の先頭
        uint32_t BvhRootCount;  // BVH を持つインスタンスの数（インスタンスの表の先頭からその数。段0の入力の数）
        uint32_t PageRequestCapacity; // ページの要求の列の容量（0 ならこのフレームは要求を書かない）
        uint32_t bSwRasterEnabled;    // 1 ならソフトウェアラスタの一覧へ積む（ハードも描く）。2 なら積めたクラスタのハードのコマンドを空振りにする
        uint32_t SwRasterCapacity;    // パスごとのソフトの一覧の容量（クラスタ数）
        float SwRasterMaxPixels;      // 振り分ける画面上の半径（画素）のしきい値
        float SwRasterNearPlane;      // 近平面までの距離
        uint32_t OrthoLod;            // 1 なら LOD を正射影で選ぶ（VSM の影）。0 なら透視（主の経路）
        uint32_t OrthoReserved[3];    // 予約（std140 で 16 バイトの境まで詰める）
    };
    // ソフトウェアラスタの 4 語は、行列 2 つ・視点・平面 6 つ・語 21 個の後ろに並ぶ（cluster_cull.comp の CullUniforms と同じ std140 の位置）。
    // 正射影の LOD の印は、その 4 語の直後
    static_assert(offsetof(CullUniformData, bSwRasterEnabled) == (16 + 16 + 4 + 24 + 21) * sizeof(uint32_t),
                  "cluster_cull.comp の CullUniforms と並びが一致しません");
    static_assert(offsetof(CullUniformData, OrthoLod) == (16 + 16 + 4 + 24 + 21 + 4) * sizeof(uint32_t),
                  "Common/MegaGeometryCull.glsl の CullUniforms と並びが一致しません");
} // namespace NorvesLib::Core::Rendering::MegaGeometry
