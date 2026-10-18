# 荒野の地形を Blender へ持ち込み、投影を合わせる

荒野の正解の絵（SCN-WLR-*）で、エンジンが描く地形と同じメッシュを Blender の Cycles で描き、6 視点の投影がエンジンと
重なることを確かめた記録。判定日: 2026-10-18。ブランチ `feature/scene-launcher`。タスク SCN-WLR-BLENDER-TERRAIN。

## 道具

| 何 | どこ | 何をするか |
|---|---|---|
| `--wasteland-finest-lod` | `Game/GameApplicationHandler.cpp`（開発ビルドだけ） | 仮想化ジオメトリの LOD を選ぶ誤差の閾値を 1e-6 画素にし、誤差 0 の段 0 だけを描かせる。見える範囲の段 0 とその親のページが常駐しきるよう、VRAM の予算のジオメトリの取り分を VT 3 : ジオメトリ 255 にし（`NORVES_VRAM_GEOMETRY_SHARE`。荒野は VT を使わない）、ジオメトリの塊に各メッシュの全部のページの置き場を見込む（`NORVES_GEOMETRY_PAGE_ROOM_DIVISOR=1`）。ログに `WASTELAND_FINEST_LOD` |
| `--wasteland-export-terrain=<フォルダ>` | `Game/GameModes/Wasteland/WastelandTerrainExport.*`（開発ビルドだけ） | 置いた地形の区画（近景の DEM 100 区画と遠景の段 98 区画。崖の細部は区画に焼き込まれている）ごとに、クック済みメッシュの LOD 0 のクラスタを、エンジンのワールド座標のまま PLY（binary_little_endian、位置と法線、三角形）で書き、`manifest.json` に区画の一覧（頂点と三角形の数・境界の箱）を書く。間引かず、溶接もしない |
| `Scripts/Terrain/BlenderReference.py --depth` | ふつうの Python から視点ごとに Blender 5.2.2 をヘッドレスで起動 | 撮影の `<視点>.aov.json` のカメラを置き、地形の深度を Cycles の 1 サンプルで描いて `<撮影>/blender/` に PFM で書く。入力の記録 `blender/inputs.json` |
| `Scripts/Terrain/ReferenceCheck.py --projection` | | エンジンの深度と Blender の深度の輪郭・稜線・深度の値を照らす |

## 座標の変換

`BlenderReference.py` の「座標の変換」の節だけで行う。

- エンジン: m、+X 東・+Y 上・+Z 南の右手系。X = east − 604500.5、Z = −(north − 4127000.5)（UTM 12N）、Y = NAVD88 − 1452.05 m
  （`Assets/Terrain/ValleyOfTheGods/height.json` の `origin_utm`・`height_offset_m`）。
- Blender: m、+X 東・+Y 北・+Z 上の右手系。原点と標高の基準はエンジンと同じ（x = X、y = −Z、z = Y）。
- カメラ: エンジンの right・up・−forward を Blender のカメラの +X・+Y・+Z の軸にし、縦の画角（`sensor_fit = VERTICAL`）・
  縦横比（画素数）・近い面をそのまま置く。ずらし（shift）は 0。
