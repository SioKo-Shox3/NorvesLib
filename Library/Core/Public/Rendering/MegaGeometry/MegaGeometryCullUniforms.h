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
        uint32_t BvhDeferClusterCapacity; // 2 パス目へ回したクラスタの列の容量（要素。主の経路の cluster_bvh_cull.comp だけが読む）
        uint32_t PageRequestStamp; // この記録の印（ページの要求の重複を省く。0 にはならず、記録ごとに変わる）
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
        uint32_t BvhNodeCapacity;     // BVH のたどりの節の列 1 本の容量（要素。主の経路の cluster_bvh_cull.comp だけが読む）
        uint32_t BvhLeafCapacity;     // BVH のたどりの葉の列の容量（要素）
        uint32_t BvhDeferNodeCapacity; // 2 パス目へ回した節の列の容量（要素）
        uint32_t BvhRangeCount;       // 範囲（BVH のインスタンスを描く順に 64 席ずつ区切ったもの）の数。範囲の段の入力の数
        uint32_t bBvhSeatsFromRanges; // 1 なら席の段（段 0）は、範囲の段が積んだ範囲の一覧から席を引く（1 範囲 = 1 ワークグループ）
        float InstanceCullDistance;   // 0 より大きければ、締まった球（範囲は AABB）の最も近い点がこれより遠い席・範囲を落とす（m）
        float PixelCullWidth;         // 0 より大きければ、席の締まった球を包む矩形が画素の中心を 1 つも含まないとき落とす（描く範囲の幅。画素）
        float PixelCullHeight;        // 同じ判定の描く範囲の高さ（画素）
        uint32_t PrevHiZMipCount;     // 前のフレームの HZB のミップの数（寸法は HiZWidth・HiZHeight と同じことを契約で確かめてある）
        uint32_t bPrevHiZEnabled;     // 1 なら前のフレームの HZB を使う: 1 パス目は範囲・席・節・クラスタを判定して隠れたものを 2 パス目へ回し、
                                      // 2 パス目は 1 パス目が描いたかを判定し直す
        float PrevViewProjection[16]; // 前のフレームの HZB の深度を描いた VP（シェーダーの並び。TAA のジッタ込み）
        uint32_t BvhDeferCapacity;    // 2 パス目へ回した範囲の列の容量（要素）
    };
    // ソフトウェアラスタの 4 語は、行列 2 つ・視点・平面 6 つ・語 21 個の後ろに並ぶ（cluster_cull.comp の CullUniforms と同じ std140 の位置）。
    // 正射影の LOD の印は、その 4 語の直後
    static_assert(offsetof(CullUniformData, bSwRasterEnabled) == (16 + 16 + 4 + 24 + 21) * sizeof(uint32_t),
                  "cluster_cull.comp の CullUniforms と並びが一致しません");
    static_assert(offsetof(CullUniformData, OrthoLod) == (16 + 16 + 4 + 24 + 21 + 4) * sizeof(uint32_t),
                  "Common/MegaGeometryCull.glsl の CullUniforms と並びが一致しません");
    // 前のフレームの VP（mat4）は std140 で 16 バイトの境界に置かれる。直前の 2 語で境界にそろう
    static_assert(offsetof(CullUniformData, PrevViewProjection) == 96 * sizeof(uint32_t),
                  "Common/MegaGeometryCull.glsl の CullUniforms と並びが一致しません");
    static_assert(offsetof(CullUniformData, BvhDeferCapacity) == (96 + 16) * sizeof(uint32_t),
                  "Common/MegaGeometryCull.glsl の CullUniforms と並びが一致しません");
} // namespace NorvesLib::Core::Rendering::MegaGeometry
