# 荒野の崖の面に貼るスキャンの塊

近くの主役の崖（Battleship Rock・Franklin Butte の尖塔の台座）の面を、写真測量のスキャンの岩の塊で作るための経路。
素材の選び方は `WastelandReference.md` の「近くの崖のスキャンの素材」。

## 経路（SCN-WL-CLIFF-SCAN-PLACE、2026-10-22）

1. **素材を焼く**: `Scripts/CalibrateScanRockAlbedo.py` が rock_face_01・02 のアルベドを崖の砂岩の色（線形の輝度 0.17・R ÷ G 2.1・
   G ÷ B 1.5）へ寄せ、粗さを乾いた砂岩の 0.86 へ寄せた画像を書き、`Assets/AssetSets/WastelandModels.json` が メッシュと 3 枚の
   テクスチャを焼く（`Scripts/CookAssets.ps1`）。
2. **Blender で置く**: `Scripts/Terrain/BlenderCliffScan.py --place --mesh <地形の書き出し> --save-blend <.blend>`。
   地形はエンジンが描くメッシュ（`Game --wasteland-export-terrain=<フォルダ>` の LOD 0 の PLY）。台本の `PLAN` の塊ごとに崖の面の
   当たりと向きを求めて置き、配置表 `Scripts/Terrain/CliffScanPlacements.json` を書く。置いた塊と周りの地形は .blend に残るので、
   人が Blender で動かしたら `--export --blend <.blend>` で配置表を書き直す（配置の印は空の物体のカスタムの属性 `cliff_scan_asset`）。
3. **エンジンが読む**: 荒野のシーンは起動のたびに配置表を読み（`Game/GameModes/Wasteland/WastelandCliffScan.h`）、素材ごとに
   MegaMesh を作って、塊ごとに仮想化ジオメトリの Entity（`MegaGeometryComponent`、影を落とす）を置く。配置を変えても地形の
   焼き直しは要らない（配置表と素材のクックだけ）。`--wasteland-hide=rocks`（`all` を含む）では読まない（比較の撮影の地形だけの絵を
   変えない）。配置表が無い・壊れているときは `WASTELAND_CLIFF_SCAN_TABLE_MISSING` を出して塊を置かずに続ける。
   ログ: `WASTELAND_CLIFF_SCAN_TABLE`（読んだ素材と塊の数）、塊ごとの `WASTELAND_CLIFF_SCAN_PLACEMENT`（位置・四元数・拡大率・
   頂点の標本のワールドの範囲・DEM の高さ場より下の標本の割合）、`WASTELAND_CLIFF_SCAN placed=`。

### 配置表の形

`version` 1。`assets` は素材の id とクック済みのメッシュ・テクスチャの論理パス、`placements` は塊ごとの `name`・`asset`・
`position`（エンジンのワールドの m）・`rotation_xyzw`・`scale`（一様）。`rotation_xyzw` は `Entity::SetRotation` へそのまま渡す
四元数で、Entity の行ベクトルのワールド行列はローカルの向きをこの四元数の標準の回転の逆で写す（Y 軸まわりの角 a の四元数は
ローカルの (x, z) を (x cos a − z sin a, z cos a + x sin a) へ写す。建物の配置と同じ）。台本は Blender の空の物体の回転 R_b を
エンジンの座標の R_e = C⁻¹ R_b C（C はエンジン → Blender の軸の入れ替え）へ移し、その四元数の共役を書く。
台本の記録（`world_bounds_min/max`・`blender_check`）はエンジンは読まない。

### 置き方

rock_face_01・02 は、底と背面の開いた「45° に傾いた岩肌の切片」の殻で、面の法線の面積の重みの和が glTF の (0, 0.5, 0.5) 付近を
向く。法線の向きの厚みは ±0.5 m×拡大率と平たく、殻の縁（開いた縁の頂点）も同じ厚みの帯にある。

- 向き: 殻の表の向き（法線の和）を崖の面の法線へ、局所の横（glTF の +X。スキャンの層の筋）を水平に向ける（殻を壁に貼る）。
  崖の面の向きは、狙いの点の周りへ水平に撃った 5 × 5 本の当たりに当てた平面。そこから鉛直の軸まわりと壁に沿う軸まわりに
  ±15° を 5° 刻みで振った組を試す。
- 位置: 塊の範囲の中心を狙いの当たりに置き、面の法線の向きの押し出しを二分で探す。縁の頂点の、地形の面からの符号付きの距離
  （BVH の最近点。面の外が正）の 90 百分位が 0.5 m に収まる最も外への押し出しを採り、組の中で見える面（地形の面の外に出る面）の
  割合が最も大きい組を選ぶ。見える割合が 0.25 に届かなければ失敗にする（埋もれ過ぎ）。
- 縁を全部壁に入れないのは、壁の側にも塊の大きさで数 m の起伏があり、縁の 95 % を壁の面から 0.1 m 以内に入れると見える面が
  1〜9 % しか残らないため（西の壁の中ほどで試した値）。0.5 m の隙間は、最も遠い ref_cliff_01（1.13 km、縦の画角 18.3°、
  1 画素 ≈ 0.36 m）で約 1.5 画素、cliff（520 m、1 画素 ≈ 0.53 m）で約 1 画素で、隙間の奥に見えるのはすぐ後ろの崖の面。

## 試しに置いた塊と照合

置き場は Battleship Rock の南西の角に近い上の壁（x −1615〜−1585・z −865〜−815・高さ 137〜189 m、法線は西南西）。
cliff と ref_cliff_01 の両方から見える崖の面を、両方の撮影の深度（-Aov）に崖の頂点を投影して選んだ。その下の西の壁の中ほど
（高さ 100〜145 m）は ref_cliff_01 からは手前の崖錐の高まりに隠れ、最初にそこへ置いた 3 個は ref_cliff_01 に 1 画素も映らなかった。

| 塊 | 素材・拡大率 | 向きの振り（鉛直・水平の軸） | 縁の距離 中央値／90 百分位／最大 | 見える面 | cliff の IoU・重心のずれ | ref_cliff_01 の IoU・重心のずれ |
|---|---|---|---|---|---|---|
| battleship_west_01 | rock_face_01・3 | −5°・+5° | −0.10／0.51／1.09 m | 0.41 | 0.90・0.7 画素 | 0.75・1.3 画素 |
| battleship_west_02 | rock_face_02・4 | −5°・+5° | −0.06／0.49／0.96 m | 0.54 | 0.83・0.7 画素 | 0.79・2.5 画素 |
| battleship_west_03 | rock_face_01・3 | −5°・+10° | −0.19／0.50／1.51 m | 0.43 | 0.88・0.2 画素 | 0.74・1.9 画素 |

塊は高さ・幅とも約 13〜15 m、壁からの出は 0.5〜3 m。

照合の撮り方（RelWithDebInfo、1920×1080、AA は FXAA に固定して深度にジッタを乗せない）:

1. 塊あり: `Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Aov -Views cliff,ref_cliff_01 -GameArgs wasteland-aa=fxaa -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-PLACE-aov`
2. 塊なし: 配置表を退けて同じ撮影（`-OutDir ...-aov-without`。ログに `WASTELAND_CLIFF_SCAN_TABLE_MISSING`）。撮り終えたら配置表を戻す。
3. `python Scripts/Terrain/BlenderCliffScan.py --render --mesh <地形の書き出し> --capture <1> --out <Blender の書き先>`（Cycles・64 サンプル、
   地形は LOD 0 の視錐台の中で最も遠い塊 ＋ 400 m まで、太陽は視点ごとのエンジンと同じ向き）
4. `python Scripts/Terrain/BlenderCliffScan.py --compare --verify --capture <1> --without <2> --blender-dir <3> --engine <既定の撮影> --out <書き先>`

結果（2026-10-22。`.harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-PLACE-compare/`）:

- 位置・向き・大きさ: 6 組（2 視点 × 3 個）すべてで、エンジンの塊の画素（塊ありと塊なしの深度の差）と Blender の塊の画素が重なり、
  IoU 0.74〜0.90、重心のずれ 0.2〜2.5 画素。両方に塊が映った画素の深度の差の中央値は、塊 0.09〜0.11 m（cliff）・0.58〜0.66 m
  （ref_cliff_01）で、周りの地形の画素の差 0.12〜0.13 m・0.58〜0.73 m と同じ（ref_cliff_01 の 0.6 m は 1.1 km の 0.05 % で、
  画面全体に共通の差）。塊の外で深度が変わった画素は 0。エンジンが置いた塊の頂点の標本のワールドの範囲は、配置表の記録
  （Blender の全頂点の範囲）と 0.05〜0.10 m 以内で揃う。
- ref_cliff_01 の IoU が cliff より低いのは、Blender が地形の LOD 0 を、エンジンが遠くを粗い段で描くため。塊は壁から 0.5 m 前後
  しか出ない縁で、どちらの地形が前に出るかで縁の画素が 1 画素ほど入れ替わり、140〜250 画素の塊では縁の 1 画素の帯が面積の
  3〜4 割になる。照合の合格の線（`--verify`）は IoU 0.7・重心のずれ 3 画素・塊の深度の差が周りの地形の差 ＋ 0.25 m まで。
- 比較図 `compare_cliff.png`・`compare_ref_cliff_01.png`（上が Blender｜エンジン、下が塊の周りの 4 倍の拡大。赤が Blender、
  水色がエンジンの塊の画素の輪郭）を開き、両方の視点で塊が同じ形・同じ位置に映り、縁が壁から浮いて隙間や裏側が見える所が
  無いことを確かめた。
- 見え方の所見: 殻は壁から出る部分だけが斑に見え、まとまった岩の塊には見えない（見える面 0.41〜0.54）。崖の面の覆い方
  （壁の下地をどれだけ下げるか・塊の重ね方・拡大率）は SCN-WL-CLIFF-SCAN-NEAR で決める。

## Battleship Rock の壁を覆う試し（SCN-WL-CLIFF-SCAN-NEAR、2026-10-24、途中）

合否は `Scripts/Terrain/ExpectSpecs/shape-scan-battleship.json`（`WastelandShape.md` の「スキャンの崖の担当の範囲」。置く前の値 棚 0.272・角 0.143・ずらした後の外形 10.8 画素・ずらす前 11.4 画素）。撮影はどれも同じ RelWithDebInfo の Game.exe、灰色の材質の形の撮影（`.harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/shape`）。

| 試し | 置いたもの | 棚の一致 | 角の一致 | 外形（ずらした後／前） | 見え方 |
|---|---|---|---|---|---|
| 殻を棚の線に貼る | rock_face_01・02 を 11 個 | － | － | － | 置けない。9 個は縁を壁から 0.5 m に収める向きが無く、4 個は収めると見える面が 5〜20 %（壁の崖の細部の溝の起伏が殻の厚み ±1.5〜2 m より大きい） |
| 閉じた岩塊を棚の線に半分埋める（中心を線に） | boulder_01・rock_09 を 67 個 | 0.435 | 0.143 | 10.8／11.4 | 塊の上端と下端の折れが線から 20〜25 画素離れる |
| 同じ（上端を線に揃える）＋寝かせた巨岩を尾根に載せる | 67 ＋ 11 個 | 0.541 | 0.143 | 10.0／10.4 | 右の段が丸い巨岩の山になり、写真の外形の右へはみ出す |
| 上端を線に揃える＋立てた柱を尾根に載せる | 68 ＋ 16 個 | 0.492 | 0.143 | 6.7／8.0 | 外形は基準に入るが、柱の頂が尖った単独の針に見える（既定の撮影でも） |
| 同じ（柱を逆さ） | 68 ＋ 16 個 | 0.486 | 0.000 | 6.4／7.6 | 丸い巨岩の山 |
| **配置表に残した形**: 上端を線に揃えた棚の岩塊だけ | 68 個（＋前からの殻 3 個） | **0.559** | 0.143 | 10.8／11.4 | 壁と崖錐に角張った岩の帯。浮いた塊・針は無い |

- 棚の岩塊（`BlenderCliffScan.py` の `SHELF_POINTS`・`solve_embed`）: 写真の注釈の棚 4 本の線を、置く前の撮影（SCN-WL-SHAPE-CAUSE の lod-default、注釈のずらし 0, −7 画素）の深度で世界へ戻した 71 点（壁は X −2150〜−1760・Y 81〜142 m・Z −1336〜−1169、ref_cliff_01 から 540〜900 m）。岩塊の長い水平の軸を壁に沿わせ、体積の標本の 45 % が壁の外に出る押し出しを二分で求め、上端を棚の線の高さに置く。素材・拡大率（boulder_01 7.5〜10.5、rock_09 90〜125）・振り・傾きを 7 通りで巡回する。壁の面が水平に近い 3 点は飛ばす
- 尾根の岩塊（`CAP_POINTS`・`solve_cap`、`CAP_ENABLED = False`）: 外形を写真へ上げる量は撮影の列ごとの外形の差から求めた（右の段は 12〜30 m、左の縁は 9〜32 m 低い）。外形の 2 行は基準に入るが、boulder_01 は頂が尖るか丸く、写真の平らな頂の段と角（角の一致）にならないので既定の景色には置いていない。次は平らな頂の柱状の素材（節理で割れた板の積み重ね）か、尾根の上の地形そのものを段に削る直しが要る
- 棚の残り: 中ほどの棚（写真の y 0.605）は上の壁と崖錐の境の線で、岩塊の上端の折れが線に沿って 10 画素以上続かない所が多い。左の短い棚（y 0.61〜0.64）は撮影では壁が無く、外形を上げないと置けない
- 費用（既定の撮影 `look/`、棚の岩塊だけの配置表）: 岩塊の三角形 合計 306 万（boulder_01 66122 × 40、rock_09 12416 × 28、殻 3 個）。配置表を読み始めてから全部を置くまで 102 秒（地形の読み込みと重なる。起動から SCENE_READY までの中）。VRAM の使用の最大 10035 MB（予算 15280 MB）。MEGA_CAPACITY の draw_dropped・bvh_overflow・visible_list_overflow は 0、GEOMETRY_PAGES の request_overflow 0・failed 0
- 式の焼き込み（`TerrainCliffDetail.cpp`）は下地として残している。殻を貼れない原因は焼き込みの溝の起伏なので、殻を使うなら Battleship Rock の範囲だけ焼き込みを外す（崖の変形なしの区画は `Assets/Models/WastelandTerrainCliffOff/` にある）かを、閉じた岩塊で覆い切った後に撮って決める
