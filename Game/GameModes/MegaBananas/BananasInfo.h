#pragma once

// ---------------------------------------------------------------------------
// mega-bananas の情報表示（H キー）に出す数値
//
// Routine が毎フレーム書き、MegaBananasInfoView（ImGui）が読む。どちらも主スレッドで触るので同期は要らない。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace Game::GameModes
{

    struct BananasInfo
    {
        /// 置いたバナナの本数
        uint32_t BananaCount = 0;
        /// そのうち手前の山に積んだ本数
        uint32_t PileCount = 0;
        /// 1 本の三角形数（最も細かい段）
        uint32_t TrianglesPerBanana = 0;
        /// 元の三角形の総数（1 本の三角形数 × 本数）
        uint64_t SourceTriangleTotal = 0;
        /// 1 本のメッシュが持つクラスタの総数（全ての段）と、段の数
        uint32_t MeshClusterCount = 0;
        uint32_t MeshLodLevelCount = 0;
        /// バナナを置けなかった（資産が無い・壊れている）
        bool bAssetMissing = false;
        /// MegaGeometry の GPU 時間[ms]（直近に完了したフレーム）。GPU の計測が使えるビルド・環境だけ有効
        bool bGpuTimeValid = false;
        float MegaGeometryGpuMs = 0.0f;
        /// 描いたクラスタ数（1 パス目 + 2 パス目）。MegaGeometry の統計のログ（MEGA_OCCLUSION）から読む。開発ビルドだけ有効
        bool bDrawnClustersValid = false;
        uint32_t DrawnClusters = 0;
    };

} // namespace Game::GameModes
