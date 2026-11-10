#pragma once

// MegaGeometry のクラスタカリングの定数バッファ（Common/MegaGeometryCull.glsl の CullUniforms。std140）の CPU 側の並び。
// 主の経路（MegaGeometryPass）と、VSM の MegaGeometry の投影物のカリング（VirtualShadowMapMegaCull）が同じ構造を使う。

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /** @brief カリングのシェーダーと照合する並び・印・束縛の番号 */
    namespace BvhCullLayout
    {
        // cluster_cull.comp の cullPass（CULL_PASS_* と同じ値）
        inline constexpr uint32_t CULL_PASS_SINGLE = 0u; // 従来の1回の判定（遮蔽の判定なし）
        inline constexpr uint32_t CULL_PASS_FIRST = 1u;  // 1パス目: 前のフレームの HZB に隠れていないものを描き、隠れたものを 2 パス目へ回す
        inline constexpr uint32_t CULL_PASS_SECOND = 2u; // 2パス目: 回されたものを今のフレームの HZB で判定して描く

        // 統計（cluster_cull.comp の stats[12]）のバイト数。0〜3 は pass1・pass2_tested・pass2_drawn・occluded、
        // 4〜7 はソフトウェアラスタの振り分け（pass1 の一覧・pass2 の一覧・ハードだけ・一覧が満杯で積めなかった数）、
        // 8 はハードのコマンドを空振りにした数、9・10 は visbuffer_sw_raster.comp が数える（矩形の上限を超えた三角形・走ったワークグループ）、
        // 11 は区間の描画コマンドの上限（MaxDrawCount）を超えて積めなかった可視クラスタの数（両パスの合計）、
        // 12 は BVH のたどりの列の容量を超えて落ちた要素の数（両パスの合計）、13 は 2 パス目へ回した列（範囲・節・クラスタ）のどれかの容量を超えたか（1 なら超えた）、
        // 14・15 は BVH の節の列の 1 段・葉の列へ積もうとした数の最大（容量を超えた分も含む）。
        // 16 からは可視クラスタの binning（Common/MegaGeometryCull.glsl の STAT_BIN_*）: 16 は全体の列へ積もうとした数、
        // 17〜30 は新旧の照合（17 = 旧のコマンドの数、18・19 = 新・旧のソフトの数、20 = 範囲の外の要素、21 = 詰めた位置の重なり、
        // 22 = 数の違う区間、23・24 / 25・26 = 新・旧の集合の hash の和、27・28 / 29・30 = 新・旧のソフトの集合の hash の和）。
        // 31〜41 は前のフレームの HZB による 1 パス目の判定（Common/MegaGeometryCull.glsl の STAT_TWOPASS_*）
        inline constexpr uint32_t StatsBufferWords = 48u;
        inline constexpr uint32_t StatsBufferBytes = StatsBufferWords * sizeof(uint32_t);

        // グループの BVH のたどり（cluster_bvh_cull.comp）。BvhStageClusters は葉のクラスタの判定の段の印、
        // BvhCounterCount はカウンタのバッファの語数（添え字 s は段 s の入力の数（段 0 は範囲の段が積んだ範囲の数）、BvhLeafCounter は葉の列の数。
        // BvhArgsBaseWords からは段 s と葉の段の間接 dispatch の引数が BvhArgsStrideWords 語ずつ並ぶ。
        // Common/MegaGeometryCull.glsl の BVH_LEAF_COUNTER・BVH_ARGS_BASE・BVH_ARGS_STRIDE と一致）
        inline constexpr uint32_t BvhStageClusters = 0xFFFFFFFFu;
        inline constexpr uint32_t BvhLeafCounter = 16;
        inline constexpr uint32_t BvhArgsBaseWords = 32;
        inline constexpr uint32_t BvhArgsStrideWords = 4;
        inline constexpr uint32_t BvhCounterCount = 128;
        inline constexpr uint32_t BvhCounterBytes = BvhCounterCount * sizeof(uint32_t);
        static_assert(BvhArgsBaseWords + (BvhLeafCounter + 1u) * BvhArgsStrideWords <= BvhCounterCount,
                      "BVH の間接 dispatch の引数がカウンタのバッファに収まりません");

        // 2 パス目へ回す列（cluster_bvh_cull.comp の binding 14）。前のフレームの HZB を使う 1 パス目が、隠れた範囲（席の印）・節・
        // クラスタを積み、2 パス目がそれだけを今のフレームの HZB で判定する。頭の BvhPass2HeaderWords 語は、全部の根を見直す
        // dispatch の引数（どれかの列が溢れたときだけ 0 でなくなる）・溢れた数、各列の間接 dispatch の引数（BvhDefer*ArgsWord から 3 語）と
        // 積もうとした数。頭の後ろに範囲の列（1 要素 BvhDeferEntryWords 語: 範囲・席 0〜31 の印・席 32〜63 の印・予約）、
        // 節の列（1 要素 BvhDeferPairWords 語: インスタンス・節）、クラスタの列（同じく インスタンス・クラスタの番号）が並ぶ。
        // 頭の [0..2] は、全部の根を見直す dispatch の引数。
        // 各パスの頭で 0 にするカウンタとは別に持ち、1 パス目の前に頭だけを 0 にする
        inline constexpr uint32_t BvhPass2HeaderWords = 32;
        inline constexpr uint32_t BvhDeferArgsWord = 8;
        inline constexpr uint32_t BvhDeferNodeArgsWord = 12;
        inline constexpr uint32_t BvhDeferClusterArgsWord = 16;
        inline constexpr uint32_t BvhDeferEntryWords = 4;
        inline constexpr uint32_t BvhDeferPairWords = 2;
        inline constexpr uint32_t BvhPass2FallbackWord = 4u;
        inline constexpr uint32_t BvhDeferCountWord = 11u;
        inline constexpr uint32_t BvhDeferNodeCountWord = 15u;
        inline constexpr uint32_t BvhDeferClusterCountWord = 19u;

        /** @brief 2 パス目へ回す範囲・節・クラスタの列と頭に必要な語数を、容量から求める */
        constexpr uint64_t BvhPass2BufferWords(uint32_t rangeCapacity, uint32_t nodeCapacity, uint32_t clusterCapacity)
        {
            return static_cast<uint64_t>(BvhPass2HeaderWords)
                + static_cast<uint64_t>(rangeCapacity) * BvhDeferEntryWords
                + static_cast<uint64_t>(nodeCapacity) * BvhDeferPairWords
                + static_cast<uint64_t>(clusterCapacity) * BvhDeferPairWords;
        }

        // 席の段（段 0）の入力（cluster_bvh_cull.comp の SEATS_FROM_*）: 全部の根（溢れたときの 2 パス目）・範囲の段が積んだ範囲の一覧・回した範囲の列
        inline constexpr uint32_t BvhSeatsFromRoots = 0;
        inline constexpr uint32_t BvhSeatsFromRanges = 1;
        inline constexpr uint32_t BvhSeatsFromDeferred = 2;
        // 2 パス目へ回した節の段・クラスタの段の印（cluster_bvh_cull.comp の BVH_STAGE_DEFERRED_*）
        inline constexpr uint32_t BvhStageDeferredClusters = 0xFFFFFFFDu;
        inline constexpr uint32_t BvhStageDeferredNodes = 0xFFFFFFFCu;
        // 前のフレームの HZB（Common/MegaGeometryCull.glsl の binding 26。前の HZB を使うフレームの両パスが読む）
        inline constexpr uint32_t PrevHiZBinding = 26;

        // インスタンスの判定の範囲（cluster_bvh_cull.comp の範囲の段・mega_scene_scatter.comp の段 2）。BVH を持つインスタンスを
        // 描く順に BvhRangeSeats 席ずつ区切り、範囲ごとの AABB（vec4 × 2 = RangeBoundsBytes）を常駐の表に持つ。範囲の段は
        // 通った範囲を節の列 0 へ積み、席の段は 1 範囲 = 1 ワークグループ（64 スレッド）で席を判定する。
        // BvhStageRanges は範囲の段の印、RangeBoundsBinding はカリングのディスクリプタセットの binding
        inline constexpr uint32_t BvhRangeSeats = 64;
        inline constexpr uint32_t BvhStageRanges = 0xFFFFFFFEu;
        inline constexpr uint32_t RangeBoundsBytes = 8u * sizeof(float);
        inline constexpr uint32_t RangeBoundsBinding = 15;

        // 範囲の印（cluster_bvh_cull.comp の RANGE_FLAG_*）: 行を持つ・締まった球を持たない行がある
        inline constexpr uint32_t RangeFlagHasRows = 1u;
        inline constexpr uint32_t RangeFlagUnbounded = 2u;
        // 節・葉の列の要素の y: 最上位ビットは1パス目も到達した印、下位31ビットは節の番号
        inline constexpr uint32_t BvhEntryPass1Reached = 0x80000000u;
        inline constexpr uint32_t BvhEntryNodeMask = 0x7FFFFFFFu;

        // 可視クラスタの全体の列（Common/MegaGeometryCull.glsl の binding 23）
        inline constexpr uint32_t VisibleListBinding = 23;

        // binning の数え・接頭和・照合の引数（Common/MegaGeometryCull.glsl の binding 24）
        inline constexpr uint32_t BinCounterBinding = 24;

        // 照合の印（Common/MegaGeometryCull.glsl の binding 25）
        inline constexpr uint32_t BinCheckMarkBinding = 25;

        // メッシュの表の binding（Common/MegaInstanceTable.glsl の MEGA_MESH_BINDING。カリングと描画のセットで同じ番号）
        inline constexpr uint32_t MegaMeshTableBindingIndex = 22;

        // カリングの記述子セットの束縛の番号（Common/MegaGeometryCull.glsl・cluster_bvh_cull.comp と一致）
        inline constexpr uint32_t CullUniformBinding = 0u;
        inline constexpr uint32_t CullInstanceTableBinding = 1u;
        inline constexpr uint32_t CullIndirectDrawBinding = 2u;
        inline constexpr uint32_t CullDrawCountBinding = 3u;
        inline constexpr uint32_t CullHiZBinding = 4u;
        inline constexpr uint32_t CullStatsBinding = 6u;
        inline constexpr uint32_t CullSectionBinding = 7u;
        inline constexpr uint32_t CullDrawInfoBinding = 8u;
        inline constexpr uint32_t BvhQueueBinding = 9u;
        inline constexpr uint32_t BvhCounterBinding = 10u;
        inline constexpr uint32_t CullPageTableBinding = 11u;
        inline constexpr uint32_t CullPageRequestBinding = 12u;
        inline constexpr uint32_t CullSwRasterBinding = 13u;
        inline constexpr uint32_t BvhPass2Binding = 14u;
    } // namespace BvhCullLayout

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
