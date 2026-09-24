# R7コア受入れ判定

判定日: 2026-09-23。R7コアの受入れは保留する。R7-P3の光輸送とR7-P4の収束・公開Cornell参照比較が未完であり、R4 DDGIとR6 RTGIを同一条件の自前PTで再照合できない。R7屋外拡張も未完のため、`RenderingRoadmap: R7 complete` trailerは付けない。

## コア完了条件

| 条件 | 現在の証拠と判定 |
|---|---|
| 材質評価の共有 | R7-P1の共通shaderとR1数値検証は成立。Outdoor goldenはR2 CSM後の出力で再承認（`c1474fc`）し、Indoor/Outdoor goldenは通過。R7-P3Bで材質texture・UV・法線マップ・instance色をPTへ接続し、余接フレームの退化判定をラスタと共通の尺度不変な関数にした（`058aab9`・`20b1764`・`8c80695`）。 |
| 独立PTと累積 | R7-P2の16試料で変化量が`0.0130208`から`0.00131565`へ低下し、履歴resetと明示選択を確認。 |
| NEE/MIS・解析照明 | R7-P3C（`f605567`・`9128e3e`・`16f4532`）。PTのBSDFはラスタIBL端点と同じDFG LUTの多重散乱補償と(1-Ed)拡散で、点・spot・方向光はLightingPassと同じ光源表のNEE、発光三角形と太陽円盤はpower heuristicのMIS。`PathTracingLightingVulkanTest`（`.harness/runs/20260924-r7-p3c/ctest-label-r2.txt`）: R1と同じ15行と粗さ0の2行の白炉は平均相対誤差の最大0.626%（roughness 128/255・metallic 1）、固定9点の最大1.50%、符号付き平均は全行で0.05%以内。純Lambertの点光源・方向光と本番BSDFの点光源は画素平均との相対誤差0.244%以内。spotは円錐外3316画素が厳密に0で、縁の画素は値が小さく相対誤差が最大40.8%になるが、画素内の値の広がりから見積もった統計許容差の内側。本番BSDFの解析比較は点光源だけで、spot・方向光・面光源は純Lambertで確かめた。面光源は光源標本のみ・BSDF標本のみ・MISと多角形光源の解析照度が0.623%以内、光沢金属・急な法線・太陽円盤の3戦略の一致は0.52%以内。光源の直前の遮蔽板の背後は0。同じ行のラスタ/PT比較は次の行。 |
| 起動時PT選択・ラスタ/PT比較 | R7-P3D（`39a674d`・`c520c23`・`b70b412`・`c3448c8`・`8d6f7c0`、評価対応`a1dc7e6`・`9125b04`・`1f79a37`・`19227a9`・`97890d2`）。RenderingCoordinatorの初期化設定（Gameと描画検証の`--renderer=path-tracing`）でmain SceneViewをPTにでき、既定はラスタ。PTはLightingPassと同じ環境マップの読み込みと輝度換算を使い、検証表示252（一様100の環境）・253（純Lambert）・254（本番BSDF）に従う。取得要求に最小試料数を持たせ、取得結果に実累積試料数を記録する（RGBA32Fで読み戻す）。レイトレーシングシーンは影を落とさない不透明物体も含め、影・DDGI・RTGIの光線はinstance maskで影を落とす物体だけを調べ、影を落とす物体がなければ従来どおりfallbackする（R5動的影・DDGI放射輝度・RTGIのテストに非casterの段階を追加し、maskを全bitにした負の対照で3件とも失敗）。PTのシェーディング法線はラスタの法線行列と同じ退化規則（3x3の行列式がFLT_EPSILON未満なら単位行列）に従う。カメラ標本（レンズ・シャッター）はdispatchごとの連続した添字で引き、1frameに1・2・4・5試料を束ねても像の二次モーメントから測った分布が期待値と3%以内（実測はレンズ0.21%・シャッター1.28%以内）。`PathTracingRasterParityVulkanTest`（`.harness/runs/20260924-r7-p3d/parity-pass.log`）: R1の白炉15行（32768試料）と既知光度の4段階（1024試料）をPTで実行して同じ評価関数に通し（白炉のマスク平均誤差の最大0.552%、マスク内の最大2.17%、既知光度の解析値との差は純Lambert 0.001%・本番BSDF 0.07%）、同じ行のラスタSceneColorとの差は白炉のマスク平均0.405%・8x8区画0.577%、既知光度のROI平均0.401%（純Lambert）・0.648%（本番BSDF）。閾値は実行前に固定したマスク平均1%・区画2%、ROI平均1%・画素3%。 |
| SPP収束・公開Cornell | R7-P4（`2365a2c`・`944452e`、判定範囲の見直し後）。`PathTracingConvergenceVulkanTest`（`.harness/runs/20260924-r7-p4/ctest-2.txt`）。**収束**: 本番設定（本番BSDF・MIS・画素内一様標本、環境光なし）のCornell boxで、同じ乱数列の入れ子の接頭列の256 spp画像に対するMSEは16 spp `0.00783138`→64 spp `0.00159723`→256 spp `0`と単調に減り、64x64の64区画すべてで単調、区画ごとのMSE(16)/MSE(64)の中央値`4.374`（独立な試料の期待値5、事前固定の範囲4〜6.25）。履歴を捨てて引き直した16 sppはbyte一致。**公開Cornell**: 純Lambert・4096 spp。影床ROIの輝度の相対誤差`0.0025`（上限0.10）、赤・緑ROIの優勢色度の差`0.0044`・`0.0217`（上限0.05）、赤・緑の壁（CPUの光線交差で40131・39875画素）で優勢な成分が一致、発光面の範囲のずれ1画素（上限2）、赤・緑の壁を含まない580区画の輝度の相対誤差は中央値`0.0101`・90%点`0.0340`（上限0.10・0.20）。数値の上限は比較の前に固定した値で、判定範囲だけを下の理由で見直した。 |
| 薄レンズ・シャッター | R7-P5の解析CoC `49.7312 px`に対する1024試料の測定CoCは`49.7069 px`。静止・移動32試料の固定GPU基準と決定論性を確認。 |
| 決定論EXR | R7-P6の同一seed・32 SPP・frameの2出力はbyte一致し、SHA-256は`9333237a36a6a8ee732fe55557a430706c5a831b71e7d0f8bef3edb80291d768`。独立読取でRGB float32/ZIPとwindowを検査し、3枚の全2,099画素が有限。 |

## 公開Cornell参照とR1数値契約

[Cornell Bowersの公開データ](https://bowers.cornell.edu/computer-graphics/data)はdiffuse Cornell boxの形状・反射率と合成RGBEを公開する。R4で固定した`Test/Core/Rendering/Baselines/RenderingValidation/R4CornellReference.rgbe`は512×512、SHA-256は`D94EF786F6BBC70AF81CE700765A598F8AFF73157234B2F50EE5A42C3F9C9F22`。R4の`R4CornellAcceptance.tsv`は白壁ROIで露出scaleを1回決め、影床・赤/緑反射の平均輝度相対誤差を各25%以内、色比差を各0.10以内とする。現行R4単独captureは影床`0.147881`、赤`0.152345`、緑`0.106696`、色比差`0.0194377`/`0.0229986`で合格した。この数値はPTとR4を比較した結果ではない。

R7-P4で公開RGBEとPTの面ごとの色を比べた（`.harness/runs/20260924-r7-p4/cornell-color-diagnosis.txt`）。露出をdirect white ROIで合わせると、白い面（奥の壁・天井・床）の緑成分は1〜3%で一致するが、公開RGBEは発光面の色比がR/G `1.448`・B/G `0.336`で、fixtureの光源色（R/G `1.471`・B/G `0.514`）より青が少ない。赤の壁のG/Rは公開RGBEが`0.0125`、PTが`0.061`で、公開RGBEの色の壁は公開スペクトルをsRGB・Adobe RGB・Rec.2020・CIE RGB・帯域平均のどれへ換算した値（赤の壁のG/Rは`0.046`〜`0.17`）よりも彩度が高い。発光面と白い面の色比はRec.2020または帯域平均（R=600〜700 nm、G=500〜600 nm、B=400〜500 nm）の換算と合うが、色の壁は合わない。公開ページは合成RGBEのRGBへの換算方法を記載していない。したがって公開RGBEの色の壁の輝度は、RGBの入力からは再現する根拠がない。R4 fixtureの白の反射率`(0.712, 0.744, 0.765)`は公開スペクトル（青の帯域で低い）と逆に青が最も高く、光源色の青も帯域平均より高い。

初回の比較（`.harness/runs/20260924-r7-p4/ctest-1.txt`）では、事前固定の判定のうち赤・緑ROIの輝度の相対誤差（`0.1047`・`0.1190`、上限0.10）と16x16区画の90%点（`1.551`、上限0.20）が超過し、超過した区画は赤・緑の壁に限られた。上の色の比較から、公開RGBEの色の壁とその反射光の輝度はRGBの入力から再現する根拠がないため、ユーザーの判断により、輝度の判定（数値は変えない）を白い面・影・発光面の位置に限り、赤・緑の壁とR4の赤・緑ROIは優勢色度で判定する範囲へ見直した。壁そのものの色度は公開RGBE `0.984`（赤）・`0.862`（緑）に対しPT `0.915`・`0.646`で、公開RGBEの方が彩度が高い。この差は判定せず記録する。

R1の`RenderingHdrSceneCaptureTest --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical`は40静的行、68数値行、120 captureで成功した。PTモードでは白炉（`--r1-scenario=white-furnace`）と既知光度（`--r1-scenario=known-cd-lambert`）を同じ評価関数で実行する。GBufferの検証表示を使う行（深度・法線・材質の副段階、DFG表、粗さ掃引など）はPTに対応する出力がないため、PTモードでは受け付けない。

## R4/R6暫定参照の再照合

| 対象 | 現行の単独結果 | 自前PTとの再照合 |
|---|---|---|
| R4 DDGI | Cornell HDR ROIと動的収束は成功。8実frame時点の赤/緑進捗は`0.971325`/`0.820146`。 | 未実施。Cornellの同一camera・geometry・material・light・露出でPT画像を生成できない。 |
| R6 RTGI | 8 rendered-frame warmup後の`mean_y=0.258768`、`center_y=0.0305305`、中心RGB=`0.0408391,0.0290016,0.0153209`。暫定goldenとの差は`1.07262e-07`で、専用許容値は`0.001`。 | 未実施。同一解像度・camera・geometry・material・light・HDR環境・exposure/pre-exposure・warmup条件のPT静止画像がない。 |

再照合では各対象の入力とlinear scene-colorの比較領域を同じ設定に固定し、seed/SPPも記録する。R4は既存Cornell ROIの輝度・色比を測り、R6は静止ROIを測る。R6の`0.001`は現在の暫定golden再現性の許容値であり、PTとの差分判定へ理由なく流用しない。PTとの比較指標と閾値を事前に固定し、差分が出た場合は輸送方式の差と実装不具合を分けて記録する。承認済みgoldenは上書きしない。Roadmapの再オープン条件は、有効な同一条件比較で確定済み閾値を超えたときに適用する。現時点では比較値がないためR4を再オープンしたと判定しない。R6の既存全体gate保留は維持する。

## 読戻した検証ログと既知の制限

| ログ | 結果 |
|---|---|
| `.harness/runs/20260923-095848/verify-R7-P7-1.txt` | Game、PT/カメラ/EXR、R4、R6の関連Debug buildは`EXIT_CODE=0`。MSBuildの既存third-party PDB警告は残る。 |
| `.harness/runs/20260923-095848/verify-R7-P7-2.txt` | 関連CTest 7/7 passed、`EXIT_CODE=0`。R4/R6の単独閾値、CoC、EXR byte一致と有限性を出力で確認。 |
| `.harness/runs/20260923-095848/verify-R7-P7-3.txt`、`verify-R7-P7-4.txt` | R1 capture testのDebug buildとall-numerical実行はともに`EXIT_CODE=0`。 |
| `.harness/runs/20260923-095848/verify-R7-P7-5.txt` | 32×32の2枚と3×17の1枚を独立読取し、全2,099画素の有限性と`EXR_FINITE_SCAN=PASS`を確認。 |
| `.harness/runs/20260923-095848/verify-R7-P7-6.txt` | P3の光輸送とP4の収束テスト名はCTest登録0件。 |
| `.harness/runs/20260923-095848/verify-R7-P7-7.txt` | `git diff --check`は`EXIT_CODE=0`。 |
| `.harness/runs/20260923-095848/verify-R7-P7-8.txt` | Cornell RGBEのSHA-256はR4で固定した値と一致。 |

現行のPT GPUテストは32×32の発光三角形と16/32試料を扱い、公開CornellやR4/R6と同一シーンを描画しない。R7-P3/P4を完了し、P1のOutdoor基準不一致を解決してから、同一条件のR4/R6比較を各1回実行する。
