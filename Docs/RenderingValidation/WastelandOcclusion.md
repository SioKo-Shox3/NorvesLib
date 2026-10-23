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

1 組の撮影は約 2 分（全 8 組で約 17 分）なので、verify は 1 行 10 分の上限に収まるよう `--sets <組>` で 1 組ずつ撮る（最初の行だけ
`--build`）。組を絞った撮影は撮った組だけを確かめ、全組のハッシュの揃いと撮っていない組は記録だけにする。揃いの合否は最後の
`ReferenceCheck.py --occlusion` が全組の撮影で見る。

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

## 測り方（SCN-WLR-OCCLUSION-METRIC）

小さな遮蔽と太陽の影を、Cycles の本描画の前に何でどう測るかを決めた記録。定義は `Scripts/Terrain/ExpectSpecs/occlusion-regions.json`、
本体は `Scripts/Terrain/OcclusionMetric.py`（`ReferenceCheck.py --occlusion --regions <定義>` から `OcclusionCompare.py` が視点ごとに呼ぶ）。
判定日: 2026-10-23。値は変えていない。下の数はすべて**試し**（SCN-WLR-OCCLUSION-SETS の撮影、Cycles は 64 サンプル・種 2 の試しの描画）。

```
python Scripts/Terrain/OcclusionSets.py --cycles --trial --seeds 2 --out <置き場>
python Scripts/Terrain/ReferenceCheck.py --occlusion --trial --engine-root <置き場> --reference <置き場>/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --verify
```

### 空だけの組

| 項目 | 決めたこと |
|---|---|
| 使う画素 | 同じ面の画素（書き出しの契約の `same_surface_masks`。輪郭を除く）だけ |
| 層 | 傾斜（陰影の法線と上の角）0〜30°・30〜60°・60° 以上 × カメラの前方向の距離 0〜10・10〜50・50〜500・500 m 以上。共通の照明の空は一様な放射輝度なので、法線の方位では分けない |
| 崖の柱の間の溝 `groove` | 地形の画素で、傾斜 30° 以上かつ画面の上の幾何のくぼみ（cavity）が 0.3 以上、地形でない物から 3 m 以上。cavity は 8 向き × 世界の半径 0.25〜8 m の標本の点の接平面からの仰角の sin の最大を向きで平均した値（0 = 開けた平面） |
| 岩塊と斜面の接する帯 `contact` | 地形の画素で、岩塊・崖の塊の面から 3 次元で 0.5 m 以内（12 向き × 半径 0.05〜3 m の画面の上の標本で見込む） |
| 株の根元 `plant_root` | 地形の画素で、株の置き場所から水平に 0.6 m（株の冠の半径）以内 |
| 対照 `control` | 同じ層の地形の画素で、cavity 0.05 以下・地形でない物から 3 m 以上・くぼみの領域の外 |
| 量 | 層ごとの（くぼみの Y の平均 ÷ 対照の Y の平均）のエンジン ÷ Cycles。1 より大きいとエンジンのくぼみが明るすぎる（遮蔽が足りない） |
| 下限 | くぼみ 2000 画素・対照 5000 画素。分母は対照の Cycles の Y が視点の面の 99 % 点の 0.05 倍以上、Cycles の比が 0.05 以上 |
| 収束 | 本描画は種を 2 つ描き、必須の層ごとの Cycles の比の種の間の差（最大 − 最小）÷ 平均が許容差 0.15 の 1/3（0.05）以下。種が 1 つ・差が超える層は不合格。試しの描画は差を測って記録するだけ。種は参照の記録（`inputs.json` の視点の `seed_outputs`）のハッシュが合う今回の描画の出力だけを読み（Blender の台本は描く前に前の描画の種の出力を消す）、2 つに届かなければ試しでも不合格 |
| 必須の層 | 下の表の 4 層（試しで画素数の下限を満たした層のうち本描画の組 terrain・rocks・plant の全部）。欠ける・下限に届かない・未収束・組か視点が測られていないものは不合格（難しい層を外して通せない）。必須でない層も測って記録する |

cavity と物体からの距離は Cycles の深度から戻した位置で求めるので、エンジンと Cycles で同じ画素を使う。画面の上の標本なので、画面の外や手前の物に隠れた
遮蔽物は数えない（領域を選ぶためだけの量で、遮蔽の正解ではない）。

試しの値（`occlusion_check.json` の `metric`）:

| 組 / 視点 | 領域 | 層 | 画素（くぼみ / 対照） | エンジンの比 | Cycles の比 | エンジン ÷ Cycles | 種の間の差 | 必須 |
|---|---|---|---|---|---|---|---|---|
| terrain-sky / ref_cliff_01 | groove | 60° 以上・500 m 以上 | 22364 / 31603 | 0.844 | 0.772 | 1.093 | 0.0004 | 必須 |
| terrain-sky / ref_cliff_01 | groove | 30〜60°・500 m 以上 | 1327 / 85003 | 0.784 | 0.724 | 1.083 | 0.0011 | |
| rocks-sky / contact_near | contact | 0〜30°・0〜10 m | 79697 / 382548 | 1.013 | 0.771 | 1.313 | 0.0001 | 必須 |
| rocks-sky / contact_near | contact | 30〜60°・10〜50 m | 15025 / 39170 | 0.982 | 0.772 | 1.272 | 0.0004 | 必須 |
| rocks-sky / contact_near | contact | 0〜30°・10〜50 m | 17409 / 1962 | 0.991 | 0.791 | 1.253 | 0.0006 | |
| rocks-sky / contact_near | groove | 60° 以上・10〜50 m | 2852 / 4437 | 0.858 | 0.828 | 1.037 | 0.0008 | |
| plant-sky / plant_near | plant_root | 0〜30°・0〜10 m | 37537 / 1293107 | 0.979 | 0.783 | 1.251 | 0.0001 | 必須 |

- 岩塊の接する帯と株の根元は、Cycles では対照の 0.77〜0.78 倍に暗くなるが、エンジンではほぼ暗くならない（0.98〜1.01）。崖錐の岩の大半と株が RTGI の光線に
  見えない（上の結論の順位 1）ことと合う。崖の溝は Cycles 0.77・エンジン 0.84 で、差は 9 %
- 64 サンプルの試しでも必須の層の種の間の差は 0.0001〜0.0004（条件 0.05 の 1/100 未満）。区域の平均を比べるので、本描画のサンプル数は条件で決まるより
  地形の溝の細部の雑音で決める（BlenderOcclusionReference.py の既定 1024 のままでよい見込み）
- rocks-cliff-sky は地形だけの組と同じ値（溝 1.094）で、崖の塊の接する帯は 57 画素しかない

### 太陽だけの組

Cycles の太陽の可視率は `BlenderOcclusionReference.py` が太陽だけの組で書く `<視点>.cycles.sun_visibility.pfm`（拡散の直接光のパスの輝度 ÷ 太陽の影を切った
同じ描画の拡散の直接光のパスの輝度。材質と N·L が消え、太陽の円盤の半影を含む 0〜1。太陽へ向かない面は −1）。エンジンは `sun_visibility` の AOV。
同じ面の灰色の物体の画素で、両方とも太陽へ向く画素を 3 × 3 画素で均して比べる。

| 量 | 定義 |
|---|---|
| 影の位置の一致 | 影 = 可視率 0.5 未満の IoU |
| 漏れ | Cycles で可視率 0.1 未満（本影）なのに、エンジンで 0.5 以上の画素 ÷ Cycles の本影の画素 |
| 影の縁の幅 | 0.8 × 半影（可視率 0.1〜0.9）の世界の面積 ÷ 半影の上の可視率の世界の勾配の大きさの面積分（m。余面積の式で、幅 w で直線に変わる帯なら境の向きによらず w。画素の世界の歩みは Cycles の深度と幾何の法線の接平面から求め、視線と面の cos は 0.1 まで。勾配は面の切れ目を越えない隣の画素の差分） |
| VSM の記録 | 撮影のログの `VSM_CLIPMAP`（段 12・段 0 の幅 32 m・バイアス −0.5）、最後の `VSM_PAGES`、受け手の texel（段の選び方の式で影の境の画素の中央値）、影の LOD（`MEGA_LOD_ERROR_PX`・megamesh_cluster_stats の shadow_lod と影の段の三角形の割合） |
| 下限 | 太陽へ向く同じ面の画素 5 万・Cycles の影の画素 2000。値の合否は SCN-WL-SUN-SHADOW-FIX が決める |

試しの値:

| 組 / 視点 | IoU | 漏れ | 縁の幅 エンジン / Cycles | 受け手の texel | VSM のページ（段） |
|---|---|---|---|---|---|
| terrain-sun / ref_cliff_01 | 0.931 | 0.9 % | 1.68 / 1.52 m | 0.125 m（画素 0.18 m） | 367（段 4〜7） |
| rocks-sun / contact_near | 0.497 | 41.9 % | 0.105 / 0.115 m | 3.9 mm（画素 8.2 mm） | 1500（段 0〜5） |
| plant-sun / plant_near | 0.767 | 0 % | 0.026 / 0.027 m | 1.95 mm（画素 2.9 mm） | 1402（段 0〜6） |

- rocks-sun の漏れ 41.9 % は、Cycles では岩の落とす影の中なのにエンジンで日なたの画素（重ね画像の赤）。影を落とす岩が 7 個だけ（`casters=7/96`）なのと合う
- plant-sun は漏れ 0 % だが、エンジンだけが影の画素が影の和の 21.8 %（株の下の濃い塊。Cycles の枝の影は細い線）
- 影の LOD: どの組も `MEGA_LOD_ERROR_PX threshold=0.000`（最も細かい段）。megamesh_cluster_stats の影の段は地形の区画で LOD 0 の三角形の 3.3 %、
  崖錐の岩塊（boulder）で 49 %。最も細かい段の撮影でこの段が VSM に使われるかはこの記録からは分からない

重ね画像は `cycles/<組>/occlusion_regions/<視点>.png`。空: 左上 Cycles の HDR に領域の色（赤 溝・橙 接する帯・紫 根元・青 対照）、右上 層、左下 エンジンの HDR、
右下 cavity。太陽: 左上 Cycles の可視率、右上 エンジンの可視率、左下 一致（灰 両方の影・赤 Cycles だけの影〔明るい赤は漏れ〕・青 エンジンだけの影）、
右下 半影（橙 Cycles・水色 エンジン・黄 両方）と Cycles の影の境（白）。開いて、溝が崖の柱の間、接する帯が岩の根元、根元が株の下の円に乗ること、
rocks-sun の漏れが岩の地面への影に出ることを確かめた。

## Cycles の本描画（SCN-WLR-OCCLUSION-CYCLES）

上の測り方の収束の条件まで、全 8 組の Cycles の正解を本描画で描いた記録。判定日: 2026-10-23。エンジンの撮影は SCN-WLR-OCCLUSION-SETS のもの
（Game.exe f6869834）で撮り直していない。

```
python Scripts/Terrain/OcclusionSets.py --cycles --seeds 2 --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS
python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --verify
```

必須の組は 6 組（terrain・rocks・plant）だが、`ReferenceCheck.py --occlusion` は参照の置き場の全組を照らし、試しの描画が残る組は `--trial` なしで
不合格になるので、rocks-cliff の 2 組も本描画にした。判定は `--trial` なしで `mode=occlusion sets=8 result=pass`。

### 参照の記録

各組の `cycles/<組>/inputs.json` に書かれた値。全組に共通: 1920 × 1080、1024 サンプル・種 2（0 と 1）・デノイズなし、`trial: false`、
git a7a44425、入力のハッシュ `BlenderOcclusionReference.py` c4db1e43…・`BlenderReference.py` af4cfd5f…・共通の照明 4f18ff20…・
`OcclusionSets.py` c90dc54e…・`occlusion-regions.json` 3ee7526e…、地形の書き出し `SCN-WLR-BLENDER-TERRAIN-mesh`（manifest d9fd5483…）。
照明は空だけの組が一様な空の放射輝度 RGB (4000, 5000, 6500)・太陽 0、太陽だけの組が空 0・太陽の法線照度 100000 lx・円盤の角 0.536°。

| 組 | 視点 | カメラの位置（縦の画角） | 太陽の高度 / 方位 | 三角形 | 置いた物 | エンジンの深度 |
|---|---|---|---|---|---|---|
| terrain-sky・terrain-sun | ref_cliff_01 | (−2595, 44.37, −1550)（18.3°） | 10° / 172° | 1.21 億 | 地形だけ | 25a22c0f… |
| rocks-sky・rocks-sun | contact_near | (−2178, 59.91, −1307.5)（60°） | 58.5° / 154° | 1.23 億 | 崖錐の岩とスキャンの崖の塊 14931（配置 73e089d6…） | e77a7383… |
| rocks-cliff-sky・rocks-cliff-sun | ref_cliff_01 | (−2595, 44.37, −1550)（18.3°） | 10° / 172° | 1.21 億 | スキャンの崖の塊 3（配置 2b3cbc15…） | 0e0a142e… |
| plant-sky・plant-sun | plant_near | (362.1, −40.01, 1966.4)（60°） | 58.5° / 154° | 0.82 億 | 株 1（PLY b1fb5c34…） | e2d4787d… |

種の出力は `<視点>.cycles.hdr.pfm`（種 0）と `<視点>.cycles.seed1.hdr.pfm`（種 1）で、ハッシュは `seed_outputs` にある。
時間は 8 組で 12 分（1 組 66〜120 秒。2026-10-23 08:01〜08:13、ログ `aux-SCN-WLR-OCCLUSION-CYCLES-render.txt` の `END ... EXIT=0`）。

### 今のエンジンの値

空だけの組（エンジン ÷ Cycles は記録だけ。合否は直しの項目の SCN-WL-OCCLUSION-FIX が決める）:

| 組 / 視点 | 領域 | 層 | 画素（くぼみ / 対照） | エンジンの比 | Cycles の比 | エンジン ÷ Cycles | 種の間の差 | 必須 |
|---|---|---|---|---|---|---|---|---|
| terrain-sky / ref_cliff_01 | groove | 60° 以上・500 m 以上 | 22364 / 31603 | 0.844 | 0.772 | 1.093 | 0.0000 | 必須 |
| terrain-sky / ref_cliff_01 | groove | 30〜60°・500 m 以上 | 1327 / 85003 | 0.784 | 0.724 | 1.082 | 0.0007 | |
| rocks-sky / contact_near | contact | 0〜30°・0〜10 m | 79697 / 382548 | 1.013 | 0.771 | 1.313 | 0.0000 | 必須 |
| rocks-sky / contact_near | contact | 30〜60°・10〜50 m | 15025 / 39170 | 0.982 | 0.772 | 1.273 | 0.0000 | 必須 |
| rocks-sky / contact_near | contact | 0〜30°・10〜50 m | 17409 / 1962 | 0.991 | 0.791 | 1.253 | 0.0001 | |
| rocks-sky / contact_near | groove | 60° 以上・10〜50 m | 2852 / 4437 | 0.858 | 0.827 | 1.037 | 0.0001 | |
| rocks-sky / contact_near | groove | 30〜60°・10〜50 m | 42 / 39170 | 0.773 | 0.745 | 1.037 | 0.0013 | |
| rocks-cliff-sky / ref_cliff_01 | groove | 60° 以上・500 m 以上 | 22351 / 30835 | 0.844 | 0.772 | 1.093 | 0.0000 | |
| rocks-cliff-sky / ref_cliff_01 | groove | 30〜60°・500 m 以上 | 1327 / 85000 | 0.784 | 0.724 | 1.082 | 0.0007 | |
| rocks-cliff-sky / ref_cliff_01 | contact | 60° 以上・500 m 以上 | 57 / 30835 | 0.988 | 0.798 | 1.238 | 0.0018 | |
| plant-sky / plant_near | plant_root | 0〜30°・0〜10 m | 37537 / 1293107 | 0.979 | 0.783 | 1.250 | 0.0001 | 必須 |

- 必須の 4 層は画素数の下限を満たし、種の間の差は最大 0.0001（条件 0.05）で収束した
- 本描画の Cycles の比は試し（64 サンプル）と小数 3 桁で同じで、エンジン ÷ Cycles の差は最大 0.001。区域の平均なので試しのサンプル数でも足りていた
- 岩塊の接する帯と株の根元は Cycles で対照の 0.77〜0.78 倍に暗くなるが、エンジンは 0.98〜1.01 でほぼ暗くならず、エンジン ÷ Cycles は 1.25〜1.31。
  崖の溝は 1.09

太陽だけの組（値の合否は SCN-WL-SUN-SHADOW-FIX が決める。VSM の記録はエンジンの撮影が同じなので上の試しの表のまま）:

| 組 / 視点 | IoU | 漏れ | 縁の幅 エンジン / Cycles | 受け手の texel |
|---|---|---|---|---|
| terrain-sun / ref_cliff_01 | 0.930 | 0.9 % | 1.68 / 1.52 m | 0.125 m |
| rocks-cliff-sun / ref_cliff_01 | 0.930 | 0.9 % | 1.68 / 1.53 m | 0.125 m |
| rocks-sun / contact_near | 0.497 | 41.9 % | 0.105 / 0.115 m | 3.9 mm |
| plant-sun / plant_near | 0.768 | 0 % | 0.026 / 0.027 m | 1.95 mm |

重ね画像（`cycles/rocks-sky/occlusion_regions/contact_near.png`・`cycles/rocks-sun/occlusion_regions/contact_near.png`）を開いた。本描画の Cycles の HDR に
目立つ雑音は無く、接する帯が岩の根元、漏れ（明るい赤）が大きな岩の地面への影に出る。verify の出力は `.harness/runs/20261022-173753/verify-SCN-WLR-OCCLUSION-CYCLES-1.txt`。

## 直す原因（SCN-WL-OCCLUSION-FIX）

最初の編集の前に、上の診断と本描画の値から直す原因を 1 つに決めた記録。判定日: 2026-10-23。

| 候補 | 値 | 判断 |
|---|---|---|
| RT 用の形の差（崖錐の岩が RTGI の光線の形に無い） | 岩塊の接する帯のエンジン ÷ Cycles が 1.313（0〜30°・0〜10 m）・1.273（30〜60°・10〜50 m）。エンジンの比 0.98〜1.01 で接する帯がほとんど暗くならない。影を落とさない崖錐の岩（274965 個のうち 96 個以外）は TLAS に入っているが印が `NonShadowCaster` で、RTGI の光線（`ShadowCaster` だけを調べる）に当たらない（診断の順位 1） | **これを直す** |
| 近い当たりの読み飛ばしの範囲 | 崖の溝 1.093（60° 以上・500 m 以上）・1.082（30〜60°）。合否の幅 0.85〜1.15 に既に入る。接する帯の画素の読み飛ばしは岩が光線に無いので測れない | 今は直さない（形の差を直した後に残る差で見直す） |
| ノイズ取りが小さな遮蔽を消す | 遮られた所の空の分は生 → 時間の蓄積 → 空間のフィルタで −0.009 以内（診断の順位 3） | 原因ではない |

- 効く必須の層は rocks-sky / contact_near の接する帯 `contact` の 0〜30°・0〜10 m と 30〜60°・10〜50 m の 2 層。株の根元（1.250）は株が RT のシーンに
  そもそも無い別の原因で、この項目では直さない。崖の溝の層は前より悪くならないことだけを見る
- 直し方: RTGI の拡散の光線（`DiffuseIndirect.comp` の `TraceDiffuseRay`）が、影を落とさない不透明物にも当たるようにする。影を落とすかは
  直接光の影（VSM・RT の影・RTGI と DDGI の光源への影の光線）の印のまま変えない。影を落とさない物は荒野の崖錐の岩のほかにランチャーの
  各シーン（mega-bananas のバナナ・rendering3dtest の電球と負荷の板・sandbox）にもあり、そちらの見た目を変えないよう、描画の設定
  `RenderingCoordinator::SetRTGINonShadowCasterOcclusion` で荒野だけが入れる（既定は切）
- 遮蔽済みの RTGI 全体に GTAO を掛ける形は二重の遮蔽になるので採らない。材質の AO は直接光にも拡散にも掛からないまま（`WastelandMaterialAO.md`）で、
  光線が岩に当たって暗くなる分と二重には数えない
- 合否（`Scripts/Terrain/ExpectSpecs/occlusion-fix.json`、`ReferenceCheck.py --occlusion --expect`）: 決めた 2 層のエンジン ÷ Cycles が 0.85〜1.15、
  ほかの全層のエンジン ÷ Cycles の 1 からの離れが前（SCN-WLR-OCCLUSION-SETS の撮影）の値の 3 % を越えて増えない、対照の層のエンジンの Y の平均が
  同じ画素で前の ±3 %

### 直した結果

全 8 組を直した Game.exe で撮り直し（`.harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX`）、直しの前の撮影（SCN-WLR-OCCLUSION-SETS）と同じ参照・同じ領域の画素で比べた
（`ReferenceCheck.py --occlusion ... --expect Scripts/Terrain/ExpectSpecs/occlusion-fix.json --verify` が `result=pass`。前後の表は撮影の置き場の `occlusion_expect.json`）。

| 組 / 視点 | 領域 | 層 | 画素（くぼみ / 対照） | エンジン ÷ Cycles 前 → 後 | 判定 |
|---|---|---|---|---|---|
| rocks-sky / contact_near | contact | 0〜30°・0〜10 m | 79697 / 382548 | 1.313 → **1.049** | 決めた層（0.85〜1.15） |
| rocks-sky / contact_near | contact | 30〜60°・10〜50 m | 15025 / 39170 | 1.273 → **1.036** | 決めた層 |
| rocks-sky / contact_near | contact | 0〜30°・10〜50 m | 17409 / 1962 | 1.253 → 1.048 | 対照が下限未満（記録だけ） |
| rocks-sky / contact_near | groove | 60° 以上・10〜50 m | 2852 / 4437 | 1.037 → 1.037 | 対照が下限未満（記録だけ） |
| terrain-sky / ref_cliff_01 | groove | 60° 以上・500 m 以上 | 22363 / 31602 | 1.093 → 1.093 | 悪くならない |
| rocks-cliff-sky / ref_cliff_01 | groove | 60° 以上・500 m 以上 | 22351 / 30835 | 1.093 → 1.093 | 悪くならない |
| plant-sky / plant_near | plant_root | 0〜30°・0〜10 m | 37537 / 1293107 | 1.250 → 1.250 | 悪くならない（株は RT のシーンに無い） |

- 対照のエンジンの Y（同じ画素）: contact_near の 0〜30°・0〜10 m が前の 0.985 倍、30〜60°・10〜50 m が 0.993 倍（周りの岩が空を少し遮る分）。ほかの視点は 1.000 倍
- 太陽だけの組は変わらない（IoU 0.930 / 0.497 / 0.768・漏れ 0.9 % / 41.9 % / 0 %）。直したのは拡散の光線だけで、影の光線と VSM は変えていない
- 並べた画 `cycles/rocks-sky/occlusion_preview/contact_near.png` を開いた: 岩の根元と岩の間の地面が暗くなり、Cycles の画と同じ向きに寄った
- 表の値は下の「自分への当たりの読み飛ばし」を入れた最終の版の撮影（`--expect` は前後で Cycles と同じ面の画素の積だけを測る。前後の画素の差は
  terrain-sky 10 画素・rocks-cliff-sky 11 画素で、共通の画素だけで測った）
- ref_cliff_01 の既定の荒野の GPU 時間（中央値、300 フレーム、同じ RelWithDebInfo のビルドを単独で前 → 後の順に測った）: 直す前
  （`--wasteland-rtgi-non-caster-occlusion=off`、`SCN-WL-OCCLUSION-FIX/timing-before`）14.733 ms（p95 15.800 ms）、直した後（`timing`）15.106 ms
  （p95 16.514 ms）。LightingPass（RTGI を含む）は 1.531 → 1.533 ms で、差 +0.37 ms のほとんどは直しと関係の無い FoliagePass（1.915 → 2.211 ms）の揺れ
- ランチャーの 4 シーンの回帰（`.harness/runs/regression/SCN-WL-OCCLUSION-FIX`）: 検証レイヤーのエラー 0、rendering3dtest・mega-bananas・sandbox は PSNR 62.6 dB 以上で
  平均の差 0（設定が切のまま）。night-village は荒野の夜なので設定が入り、PSNR 63.9 dB・平均の差 +0.004（差の画 `after/night-village-diff.png` は灯りの周りの
  まばらな点だけ）。material-gallery（設定は切）は 10-22 の撮影と最大 1 段の差（PSNR 54.3〜54.7 dB）
- 基準画像の検査（Indoor・Outdoor）は落ちるが、この項目の変更を HEAD の版へ戻しても同じ値（Indoor 8929 画素・mean_flip 0.0053、Outdoor 20356 画素・
  mean_flip 0.0089）で落ちるので、この直しによる差ではない（検証アプリは RTGI を切り、設定の既定も切）。再承認はしていない

### 岩の面の黒い斑と自分への当たりの読み飛ばし（2026-10-23）

上の結果の撮影には、影を落とさない崖錐の岩の面に黒い斑が出ていた（`contact_near` のエンジンの HDR で、直す前の 0.3 倍未満に暗くなった画素が
約 9 万。判定は地形の画素だけを測るので通っていた）。岩の光線の形は焼き込んだ階層の最も粗い段（常駐のフォールバック）でラスタとのずれが大きく、
岩の 1 次面から出た拡散の光線がすぐ近くで自分の光線の形に当たって空を遮っていた。

- 直し方: RTGI の instance の表の `rayProxy.z` に影を落とさない物体の印（1/0）を足し、`TraceDiffuseRay` で印のある物体への当たりのうち、道のりが
  `max(rayProxy.x, proxyHeightLimit)`（光線の形のずれと 1 次面の読み飛ばしの高さの上限。1 次面から 0.5 m 以上）より近いものを読み飛ばす
- 試し（`.harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX-try/rocks-sky`、rocks-sky だけ）: 黒い斑の画素は約 9 万 → 7823（岩の輪郭のずれの分）。
  並べた画で岩の面が Cycles と同じ明るさに戻った。決めた 2 層のエンジン ÷ Cycles は 1.049・1.036（範囲の内）
- 道のりを `rayProxy.x` だけにした試しと、閉じた形の裏への当たり（三角形の巻きの向き）で読み飛ばす試しでは斑が残った
- 起動引数 `--wasteland-rtgi-non-caster-occlusion=off`（開発ビルドだけ）で直す前の印に戻せる。GPU 時間の前後はこれで同じビルドを測る
- 前後の比べ（`--expect`）は、視点ごとに前後どちらの撮影でも Cycles と同じ面の画素の積だけで前後の両方を測り直す（前の版は前後を別の画素で
  測っていた。差は terrain-sky 10 画素・rocks-cliff-sky 13 画素）。測り直しは並べた画・重ね画像を書かない
- この直しで全 8 組を撮り直した（上の表）。rocks-sky の contact_near で直す前の 0.3 倍未満に暗くなった画素は 7823（試しと同じ。岩の輪郭のずれと
  岩の根元の地面の遮り）で、直す前・後・暗くなった画素を並べた画で岩の面は直す前と同じ明るさだった
- 回帰の 4 シーンはこの版でも検証レイヤーのエラー 0、rendering3dtest 102.9 dB・mega-bananas 65.4 dB・sandbox は差 0（設定が切）、night-village 64.1 dB・
  平均の差 +0.004（差の画は灯りの周りのまばらな点だけ）。設定が切のシーンでは影を落とさない物が拡散の光線の印に入らないので、読み飛ばしも効かない
