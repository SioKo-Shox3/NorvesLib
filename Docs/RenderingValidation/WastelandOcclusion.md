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
