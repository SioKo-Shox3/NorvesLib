# 荒野の RTGI が小さな遮蔽を落としていないかの診断

ユーザーの所見（2026-10-22、確認 2）「遮蔽による影の落ち方の質が低い」（溝の奥・岩の下・株の根元が暗くならない）について、
RTGI が働く荒野で遮蔽を決める経路（RTGI の空の分と太陽の影。材質の AO は直接光にも拡散にも掛からない。`WastelandMaterialAO.md`）が
0.1〜2 m の遮蔽を落としていないかを経路ごとに調べた記録。判定日: 2026-10-23。ブランチ `feature/scene-launcher`。
タスク SCN-WLR-OCCLUSION-DIAG。値は変えていない。

## 結論（原因の順位）

| 順位 | 原因 | 証拠 | 影響 |
|---|---|---|---|
| 1 | 崖錐の岩の 99.97 % と株が、RTGI の光線から見えない | 崖錐の散布は影を落とす岩 96 個と影を落とさない岩 274965 個（`WASTELAND_TALUS casters=96 fragments_placed=274965`）。影を落とさない物の instance の印は `RayTracingInstanceMaskNonShadowCaster` で、RTGI の光線は `RayTracingInstanceMaskShadowCaster` だけを調べる（`DiffuseIndirect.comp` の `TraceDiffuseRay`、`Common/RayTracingInstanceMask.glsl`）。株は `FoliagePass` と仮想影の地図だけが描き、RT のシーンに入らない。contact_near の診断の画で、岩塊の根元の地面の空の分は生・時間の蓄積の後・空間のフィルタの後のどれでも周りの地面と同じ明るさ | 岩の下・岩の間・株の根元の拡散の空の光が遮られない。岩の側面が暗いのは岩の光線が地形に当たるためで、地面の側は暗くならない |
| 2 | 近い当たりの読み飛ばしが 0.1〜2 m の遮りを消す | contact_near の 0〜20 m の地面・岩の画素の 21.1 % が 0.1〜2 m の高さの自分の面への当たりを読み飛ばし、そのうち 94 %（画素の 19.8 %）がその後に空へ抜けた。ref_cliff_01 では崖の縦の溝（ひだ）に 0.1〜2 m の読み飛ばしが帯で並び、遮られた所（生の近傍の平均 < 0.6）の画素の 11.3 % が 0.1〜2 m を読み飛ばした（うち 6.1 % が空へ抜けた） | 地形の区画（光線の形のずれが正の instance）の上で、溝の奥や斜面の段の 0.1〜2 m の遮りが、粗い光線の形の自分への当たりとして空へ抜ける。外へ出る当たりの上限は近くで 0.5 m × 4 = 2 m |
| 3 | 時間の蓄積・空間のフィルタ | 遮られた所の空の分: 生の近傍の平均 0.473 → 時間の蓄積の後 0.464 → 空間のフィルタの後 0.464（contact_near）、0.514 → 0.506 → 0.506（ref_cliff_01）。時間の蓄積の後を同じ箱で均すと 0.473・0.515 で生と同じ | 遮蔽を均して消してはいない（差は −0.009 と −0.0002 以内）。原因ではない |
| 4 | 光線の始点のずらし | 始点は法線の向きへ `RAY_ORIGIN_OFFSET` 0.002 m、光線の最小の距離 `RTGI_RAY_MINIMUM_DISTANCE` 0.001 m | 0.1 m より 2 桁小さく、0.1〜2 m の遮蔽は落とさない。原因ではない |

距離に比例する読み飛ばしの上限（`5e-4 × 距離`）が 0.5 m を越えるのは 1 km より遠い画素だけで、contact_near では 0 %、ref_cliff_01 では
2.9 %（崖の上の遠い部分）。0.1〜2 m の読み飛ばしの大半は近くの上限（0.5 m）とその 4 倍（外へ出る当たり）の側で起きている。

## RT のシーン（TLAS）

| 視点 | RT の instance | 範囲の外で除いた候補 | 候補 | RTGI の光線のうち当たった割合 |
|---|---|---|---|---|
| contact_near | 4019（すべて MegaGeometry） | 271243 | 275262 | 19.0 % |
| ref_cliff_01 | 182 | 275080 | 275262 | 21.2 % |

contact_near で TLAS に入る 4019 個の多くは近くの崖錐の岩だが、影を落とさない岩は印が `NonShadowCaster` なので RTGI の光線には当たらない
（全部の印を調べるのはパストレーサーだけ）。スキャンの崖の塊は影を落とす MegaGeometry なので RTGI に当たる。RT に見せる形は影の段
（既定は LOD0）で、ラスタとの差は instance の `RayProxyErrorMeters`（段の誤差 × 拡大率）として読み飛ばしの判定に使われる。
株（低木・草）は RT のシーンに無く、ラスタの形との差を論じる前に形そのものが光線に無い。

## 診断の出力（開発ビルドだけ）

撮影の `--capture-aov` を付けた Game は、描画の初期化の前に環境変数 `NORVES_RTGI_OCCLUSION_DIAG=1` を置き（`GameApplicationHandler.cpp`）、
LightingPass が RTGI の定数の `environmentParameters.z` を 1 にする。既定（撮影の AOV なし・Release）は 0 で、出力と履歴の a は 1 のまま。
照明は RTGI の rgb だけを読むので、診断の有無で画は変わらない。

| 段 | どこ | 値 |
|---|---|---|
| 生 | `DiffuseIndirect.comp` の `ShadeDiffuseIndirect` | 1 本目の光線が空へ抜けたら 1（コサイン標本なので画素の平均が拡散の空の見える割合）。読み飛ばしの分類は、1 次面の光線が読み飛ばした当たりの 1 次面からの高さの最大で 0 = 無し・1 = 0.1 m 未満・2 = 0.1〜2 m・3 = 2 m 以上 |
| 時間の蓄積の後 | 同じ（履歴の a） | 放射輝度と同じ重みで履歴と混ぜた空の分。出力の a = 2 × 符号 + 空の分（符号 = 生 + 2 × 分類。半精度で空の分の誤差は ±0.004 以内） |
| 空間のフィルタの後 | `CrossBilateralDenoise.comp` | 符号を除いた空の分を放射輝度と同じ重みで均した値を出力の a へ |
| 撮影 | `gbuffer_aov.frag`・`GBufferAovPass`・`FrameCaptureHdrWriter.cpp` | AOV の照明の g・b を `<視点>.aov.rtgi_temporal_code.pfm`・`<視点>.aov.rtgi_filtered_sky.pfm` に書く（RTGI が無い構成は -1） |

測るのは `python Scripts/Terrain/ReferenceCheck.py --occlusion-diag --capture <撮影先> --verify`（本体は `OcclusionDiag.py`）。
生の値は 1 画 1 本の 0/1 なので 17 × 17 画素の近傍で平均して比べる。視点ごとの画 `<撮影先>/occlusion_diag/<視点>.png` は
左上 = 生の近傍の平均、右上 = 時間の蓄積の後、左下 = 空間のフィルタの後、右下 = 読み飛ばしの場所（黄 = 0.1 m 未満、赤 = 0.1〜2 m、
紫 = 2 m 以上。明るい色は読み飛ばした後に空へ抜けた画素）。

## 撮影

視点は ref_cliff_01（崖の溝）と contact_near（新しく足した。`SceneCatalog.h`。ref_cliff_01 と同じ向きで、Battleship Rock の西の崖錐の裾の
岩塊の群れを 10〜20 m で見る。画に入る株は小さな草が 2〜3 株だけ）。照明は共通の照明の空だけ（`common-light-case=sky`）で、雲・礫・建物・
空気遠近・霧を隠し、岩と株は残した。AA は FXAA。

`--wasteland-finest-lod` を付けた撮影は、2 視点とも SCENE_READY の前に終了コード −1073740791（0xC0000409）で落ちる（直前のログは
`sw_raster_list mb=256.00`）。この項目の変更の前からある落ち方（NEXT_FINDINGS の SCN-WL-LOD-DETAIL の記録）と同じで、上の数は
`--wasteland-finest-lod` を外した撮影（`scene-capture/SCN-WLR-OCCLUSION-DIAG`。撮り直しても数は小数 4 桁まで同じ）で測った。最も細かい段の地形では
光線の形のずれと読み飛ばしの割合が変わりうるので、落ち方を直した後に同じ組で測り直す。

診断の出力が既定で無効なことは、ランチャーの 4 シーン（rendering3dtest・mega-bananas・sandbox・night-village）の回帰の撮影で
確かめた（検証レイヤーのエラー 0、PSNR 55.0〜55.8 dB・平均の差 −0.02 以内。差は比べた撮影の後に入った表示の Blit の ±0.5 段の雑音の分）。

## 比べの書き出しの契約（SCN-WLR-OCCLUSION-EXPORT）

小さな遮蔽をエンジンと Cycles で比べるとき、両方が同じ形・同じ配置・同じ材質・同じカメラと照明を描いていることを、
画素ごとの同じ面の照合で確かめてから値を比べる。判定日: 2026-10-23。

| 項目 | エンジン | Cycles（`Scripts/Terrain/BlenderOcclusionReference.py`） |
|---|---|---|
| 起動引数・入力 | 開発ビルドの `--wasteland-common-light`・`common-light-case=sky か sun`・`--wasteland-gray-material`・`--wasteland-finest-lod`・`--wasteland-aa=fxaa`、`CaptureScene.ps1 -Hdr -Aov` | 地形は `--wasteland-export-terrain` の区画の LOD 0 の PLY（`--mesh`）、小物は撮影のフォルダの `occlusion_props.*`、カメラは `<視点>.aov.json` |
| 材質 | 地形と小物（崖錐の岩塊・スキャンの崖の塊・配置表の小物）の BaseColor を `gray_material` のアルベド、粗さを同じ値、金属度 0、テクスチャ・法線マップ・崖の色味なし。BaseColor は GBuffer と RTGI・DDGI の当たりの反射率の両方に使われる | 全体に `gray_material` の Principled BSDF（IOR 1.5 で F0 0.04）。法線は頂点の補間した法線 |
| 小物の形 | `Game/GameModes/Wasteland/WastelandPropExport.cpp` が、作った MegaMesh の LOD 0 をメッシュのローカルの座標の PLY（`occlusion_props.<名前>.ply`、地形の書き出しと同じ並び）と一覧 `occlusion_props.manifest.json` に書く | 形ごとにメッシュを 1 つ作り、配置ごとに共有して置く |
| 配置 | 置いた小物の行を `occlusion_props.placements.csv`（形・位置・四元数 (x, y, z, w)・拡大率・影を落とすか）に書く。ワールド = 位置 + (四元数の共役の回転)(拡大率 ⊙ ローカル) | 同じ式の行列に座標の変換（BlenderReference.py の `engine_to_blender_matrix4`）を掛ける |
| 崖錐の岩の範囲 | 灰色の材質の撮影だけ、視点から水平 300 m 以内の岩だけを置く（`kGrayTalusRadius`）。全域の岩（約 27 万個）を最も細かい段で描くと MegaGeometry の BVH のたどりの列（1 << 22）が溢れ、地形がまるごと抜ける（contact_near で bvh_overflow 約 1530 万、`scene-capture/SCN-WLR-OCCLUSION-EXPORT/probe-nogray`）。300 m に限ると溢れ 0 | 書き出しの全行をそのまま置く（同じ岩だけ） |
| 照合の画 | AOV の深度・法線（頂点の補間した陰影の法線）・アルベド・粗さ | `<視点>.aux.depth.pfm`（深度）・`aux.normal.pfm`（陰影の法線。Normal のパス）・`aux.geometric_normal.pfm`（幾何の法線。三角形の面の向き。Geometry の True Normal をシェーダの AOV `GeometricNormal` で出す）・`aux.object.pfm`（Object Index。地形 0・崖錐の岩 1・崖の面の塊 2・そのほか 3・株の枝 4・株の葉 5・空 −1） |

照合（`ReferenceCheck.py --occlusion [--trial] --engine-root <置き場> --reference <参照の置き場> --verify`、本体は `OcclusionCompare.py`）:
組は参照の置き場の下の `inputs.json`（`cycles_occlusion`）を持つフォルダで、エンジンの置き場の同じ名前のフォルダと組む。
入力は、小物の形と配置のハッシュ（今のエンジンの撮影のファイルと）、台本・借りた BlenderReference.py・共通の照明・地形のメッシュ（`check_blender_inputs`）、
起動引数と照明の組、カメラを照らす。同じ面は、どちらかが面に当たった画素から、両方の深度の空との境・深度の跳びの 2 画素（`GrayRegions.edge_mask`）を
除いた画素を分母に、`PlantRegions.same_surface`（深度の差 1 % 以内）かつ陰影の法線の角の差 10° 以内の割合で、90 % 以上を合格とする。
法線の許容差は陰影の法線どうしで比べる（エンジンの AOV の normal も頂点の補間で、幾何の法線を持たない）。幾何の法線は、Cycles の深度から戻した位置の
上下・左右の隣との差の外積（深度の面の向き）と比べ、輪郭から離れた画素のうち 2° 以内で合う割合が 0.5 以上で、陰影の法線の同じ割合より高いことを確かめる
（補間の法線を幾何の法線として出していないことの確かめ。崖錐の岩は三角形が画素より小さく、隣の画素が別の三角形に乗るので割合が低い）。
この割合は、上下左右の隣の幾何の法線が中央と 0.5° 以内でそろう画素（5 画素が 1 つの平面に乗る。1 万画素以上）で判定する
（SCN-WLR-OCCLUSION-SETS で変えた。遠い ref_cliff_01 では三角形が画素ほどの大きさで、全画素では幾何 55.9 %・陰影 58.8 % と見分けられない）。
下の数のうち 68.5 % などは、全画素で数えた変える前の値（`occlusion_check.json` の `all_pixels` に今も書く）。
エンジンの AOV のアルベドと粗さが面のどの画素でも `gray_material` と 1.5/255 以内であることも確かめる。

岩塊の組（rocks-sky。contact_near、空だけ、雲・草と低木・礫・建物・空気遠近・霧を隠す。`scene-capture/SCN-WLR-OCCLUSION-EXPORT/rocks-sky` と
`cycles/rocks-sky`、Cycles は試しの 64 サンプル）:

- 置いた小物 14931 個（崖錐の岩塊 boulder_01 9476・板 rock_09 5452、影を落とす岩 7、スキャンの崖の塊 3）。Cycles は地形の区画 156（1.23 億三角形）と
  同じ小物を読み込みに 56 秒、照合の画に 12 秒、本描画に 7 秒（RTX 4080 の OptiX）
- 同じ面の画素 99.98 %（分母 1891498 画素、輪郭 89844 画素を除く）。地形 100 %、崖錐の岩 99.93 %。エンジンだけ・Cycles だけが面に当たった画素は 0。
  スキャンの崖の塊はこの視点に映らない
- エンジンの AOV のアルベドの差は最大 0.0020（8 bit の丸め）、粗さの差 0
- 幾何の法線が深度の面の向きと 2° 以内で合う画素 68.5 %（地形 87.9 %・崖錐の岩 23.0 %、1886550 画素）、陰影の法線では 38.9 %（地形 48.9 %・崖錐の岩
  15.3 %）。幾何の法線と陰影の法線の角の差は中央値 2.4°・99 % 点 29.6°（画素 (550, 450) の岩の面で 18.5°）
- 並べた画（`cycles/rocks-sky/occlusion_preview/contact_near.png`）で岩の形と位置が重なる。Cycles では岩の根元と岩の間の地面が空の遮蔽で暗く
  なるが、エンジンではほとんど暗くならない（上の順位 1。影を落とさない岩は RTGI の光線に見えない）。値の比べ方は SCN-WLR-OCCLUSION-METRIC で決める

## 比べの組（SCN-WLR-OCCLUSION-SETS）

比べの全組を同じ Game.exe で撮る台本は `Scripts/Terrain/OcclusionSets.py`。後の直しの項目もこれで撮り直す。判定日: 2026-10-23。

```
python Scripts/Terrain/OcclusionSets.py --capture --out <置き場> [--build] [--sets ...]   # エンジン（<置き場>/<組>）
python Scripts/Terrain/OcclusionSets.py --cycles --trial --out <置き場> [--sets ...]     # Cycles（<置き場>/cycles/<組>）
python Scripts/Terrain/ReferenceCheck.py --occlusion --trial --engine-root <置き場> --reference <置き場>/cycles --verify
```

組は 1 組 1 視点（Game は起動ごとに視点を中心に崖錐の岩を選んで `occlusion_props.*` を撮影のフォルダへ書くので、2 視点を同じフォルダで
撮ると後の視点の配置で前の視点の配置が消える）。照明は共通の照明の空だけ（`-sky`）と太陽だけ（`-sun`）。どれも `wasteland-common-light`・
`wasteland-gray-material`・`wasteland-finest-lod`・`wasteland-aa=fxaa` で、礫は隠す。

| 組 | 種類 | 視点 | 隠すもの | 置かれる物 |
|---|---|---|---|---|
| terrain-sky・terrain-sun | 地形だけ（DEM の溝） | ref_cliff_01 | all | 地形 |
| rocks-sky・rocks-sun | 岩塊と地形 | contact_near | 雲・草と低木・礫・建物・空気遠近・霧 | 崖錐の岩 14928・スキャンの崖の塊 3 |
| rocks-cliff-sky・rocks-cliff-sun | 岩塊と地形 | ref_cliff_01 | 同上 | スキャンの崖の塊 3 だけ（崖錐の岩は視点から 300 m の外） |
| plant-sky・plant-sun | 低木の根元 | plant_near | all-but-plant | 地形と SCN-WLR-PLANT-REF の株 1 株（株は模型の色と葉の透過のまま。灰色にしない） |

Cycles（`BlenderOcclusionReference.py --kind terrain|rocks|plant`）は、低木の組で株を `BlenderReference.py` の `--plant` と同じく撮影のログの
`WASTELAND_PLANT_PLACEMENT reference=1` の行と `reference_plant.ply` から置き、物体の識別を株の枝 4・葉 5 にする。照合は組ごとに、
種類の物体（地形だけ = 地形、岩塊 = 地形と崖錐の岩かスキャンの崖の塊、低木 = 地形と株）が映ること、低木の組の株の PLY が撮影のものと合うこと、
全組のエンジンの撮影の Game.exe と実装のハッシュが揃うこと、`occlusion_sets.json` の全組に参照があることを確かめる。低木の組の灰色の材質の確かめは、
同じ面の画素のうち Cycles の物体が灰色のものだけで行う。株は枝と葉の間で深度が跳び、同じ面の分母（輪郭を除く）に株の画素は 38 しか残らないので、
輪郭を含めた Cycles の株の画素でエンジンの深度が 1 % 以内で合う割合（90 % 以上）と、Cycles の地形の画素でエンジンだけが手前に面を見る画素
（株の画素の 0.1 倍以下）で株の形を照らす。

結果（`scene-capture/SCN-WLR-OCCLUSION-SETS`、Game.exe f6869834、Cycles は試しの 64 サンプル。空だけと太陽だけで照合の値は同じ）:

| 組 | 同じ面の画素 | 分母 | 物体ごと | 幾何の法線（平面の画素） | 陰影の法線 |
|---|---|---|---|---|---|
| terrain | 99.97 % | 788311 | 地形 99.97 % | 99.99 %（34303 画素） | 48.4 % |
| rocks | 99.98 % | 1891498 | 地形 100 %・崖錐の岩 99.93 % | 89.5 %（1185174 画素） | 47.2 % |
| rocks-cliff | 99.96 % | 788312 | 地形 99.97 %・スキャンの崖の塊 90.3 %（523 画素） | 99.99 %（34303 画素） | 48.4 % |
| plant | 100 % | 1968627 | 地形 100 % | 92.3 %（1856638 画素） | 87.5 % |

- 低木の組の株の形: Cycles の株の画素 40529 のうち 93.6 % でエンジンの深度が合い（隣の 1 画素まで許すと 98.8 %）、エンジンだけが手前に面を見る
  画素は 1804（株の画素の 0.045 倍）
- エンジンの AOV のアルベドの差は全組で最大 0.0020、粗さの差 0
- 時間: エンジンの撮影は 1 組 1.5〜2.2 分（8 組で 15 分）。Cycles の試しは 1 組 50〜78 秒（読み込み 34〜56 秒、8 組で 9 分）
- 並べた画（`cycles/<組>/occlusion_preview/<視点>.png`）を開いた。terrain-sun は崖の柱の影と溝の陰りが同じ所に落ちる。rocks-sun は岩の形と位置が重なり、
  Cycles では大きな岩の左に地面へ落ちる影がはっきり出るが、エンジンでは岩の根元の影が小さく薄い（影を落とす岩は 7 個だけ。上の順位 1）。
  plant-sun はエンジンの株の影が株の下に濃い塊で落ち、Cycles では枝の影が細い線で疎らに落ちる（NEXT_FINDINGS の SCN-WL-PLANT-SHADOW の記録と同じ）
- rocks-cliff の組は崖錐の岩が無く、スキャンの崖の塊も画の 0.07 % なので、地形だけの組とほとんど同じ画になる。ref_cliff_01 で崖錐の岩を比べるには、
  灰色の材質の撮影の岩の範囲（`kGrayTalusRadius`、視点から 300 m）か BVH のたどりの列の容量を変える必要がある（どちらもこの項目の範囲の外）
