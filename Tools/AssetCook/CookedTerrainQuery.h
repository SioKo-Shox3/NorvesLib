#pragma once

// ---------------------------------------------------------------------------
// 焼いた地形の区画への問い合わせ（--sample-cooked-terrain）
//
// 崖の細部の焼き（TerrainCliffDetail.cpp）は面の法線の向きに押し出して X/Z も動かすので、焼いた面は元の座標の DEM の高さでは
// なく、張り出しでは同じ X/Z に面が複数ある。置いた物の接地を焼いた面で測るために、点ごとにその X/Z を含む DEM の区画の
// クック済みのパッケージを読み、最も細かい段の三角形と鉛直の線の交点をすべて返す。押し出しは区画の境の頂点も水平へ動かす
// （境の頂点は両側の区画で同じ位置）ので、境の近くの点は隣の区画の三角形とも交わりを調べて合わせる。クックの出力は読むだけで変えない。
// Scripts/Terrain/CheckCookedTerrain.py が読む。
// ---------------------------------------------------------------------------

namespace NorvesLib::Tools::AssetCook
{
    /// AssetCook --sample-cooked-terrain --manifest <manifest.json> --points <点の一覧の JSON> [--horizon-level <段>]
    ///                       [--boundary-edges]
    /// --horizon-level を渡すと、DEM の区画の代わりに遠景のその段（例: castle）の区画のすべてで交点を調べる
    /// --boundary-edges を渡すと、読んだ区画ごとに最も細かい段の開いた縁の辺（1 つの三角形だけが使う辺）の両端の位置も書く
    /// argv[1] が --sample-cooked-terrain でなければ false を返して何もしない。結果の JSON は標準出力へ書く
    bool RunCookedTerrainSampleCommand(int argc, char **argv, int &outExitCode);
} // namespace NorvesLib::Tools::AssetCook
