# 荒野の地形を Blender へ持ち込み、投影を合わせる

荒野の正解の絵（SCN-WLR-*）で、エンジンが描く地形と同じメッシュを Blender の Cycles で描き、6 視点の投影がエンジンと
重なることを確かめた記録。判定日: 2026-10-18（6 視点の撮り直し 2026-10-19）。ブランチ `feature/scene-launcher`。タスク SCN-WLR-BLENDER-TERRAIN。

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

## LOD の固定

`--wasteland-finest-lod` の撮影（6 視点、RelWithDebInfo）は、`CaptureScene.ps1 -LaunchPerView` で視点ごとに Game を起動して撮る。
各視点を撮った時点のページは読み込みきっていた（`GEOMETRY_PAGES` の wanted・reading・ready・uploading が 0、落ち着きの基準から
保存までの追い出し 0。常駐は 0.8〜4.1 GB、1 視点 80〜106 秒）。`ReferenceCheck.py --projection` はこれを視点ごとに確かめ、
満たさない撮影を拒む。

1 回の起動で 6 視点を続けて撮ると、前の視点のページが常駐に残り、3 視点目で常駐が置き場の上限（約 7.6 GB。ページはメッシュと同じ
1 GB の塊にしか置けない）に当たる。以後の視点では追い出しと読み直しが続き、ページの流しはそれを空回りとみなして落ち着き待ちを
止めないので（`GeometryPageStreamer::HasPendingWork`）、ref_ground_01 は 1700 ページ余りを読み残したまま撮れた（2026-10-19、
ほかのアプリの VRAM 約 2.4 GB）。置き場の見込みを広げる前は、5 視点で数千ページを読み残したまま撮れていた。

## 投影の照合（6 視点）

撮影: `CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Hdr -Aov -LaunchPerView -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod`。
Blender は視錐台に掛かる区画（48〜76 区画、1610 万〜6530 万三角形）を読み、CPU の Cycles で 1 視点 14〜133 秒。

| 視点 | 輪郭の画素の 2 画素以内（エンジン→Blender / 逆） | 稜線の距離の最大 | 空の分かれ方の食い違い | 深度の相対差 中央値 / 95 % |
|---|---|---|---|---|
| ref_far_01 | 100 % / 100 % | 1 画素 | 0.001 % | 0.023 % / 0.18 % |
| ref_far_02 | 100 % / 100 % | 1 画素 | 0.001 % | 0.028 % / 0.045 % |
| ref_cliff_01 | 100 % / 100 % | 1 画素 | 0.022 % | 0.040 % / 0.071 % |
| ref_cliff_02 | 100 % / 100 % | 1 画素 | 0.028 % | 0.026 % / 0.19 % |
| ref_ground_01 | 100 % / 100 % | 1 画素 | 0.000 % | 0.011 % / 0.016 % |
| ref_ground_02 | 100 % / 100 % | 1 画素 | 0.013 % | 0.012 % / 0.044 % |

稜線は、列ごとのいちばん上の表面の画素から相手の空との境の画素までの距離で測る。列ごとの行の差で測ると、崖の側面のように
稜線がほぼ縦に走る所で横に 1 画素ずれるだけで行が十数〜27 画素変わり、ref_cliff_01・02 の 1〜1.7 % の列がその形だった
（両者の深度はその画素で 0.1 % 以内）。Blender の深度を 3 画素下へずらすと、稜線の 2 画素以内は 0.37〜0.70 に落ちて不合格になる。
