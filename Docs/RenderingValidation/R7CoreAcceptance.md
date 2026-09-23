# R7コア受入れ判定

判定日: 2026-09-23。R7コアの受入れは保留する。R7-P3の光輸送とR7-P4の収束・公開Cornell参照比較が未完であり、R4 DDGIとR6 RTGIを同一条件の自前PTで再照合できない。R7屋外拡張も未完のため、`RenderingRoadmap: R7 complete` trailerは付けない。

## コア完了条件

| 条件 | 現在の証拠と判定 |
|---|---|
| 材質評価の共有 | R7-P1の共通shaderとIndoor/R1数値検証は成立。承認済みOutdoor goldenは平均FLIP `0.002326954`、459画素差で未達のためP1は`blocked`。 |
| 独立PTと累積 | R7-P2の16試料で変化量が`0.0130208`から`0.00131565`へ低下し、履歴resetと明示選択を確認。 |
| NEE/MIS・解析照明 | R7-P3は`blocked`。現行のPT材質snapshotはBaseColor/発光のみで、texture、UV、metallic、roughness、DFGと光源PDFの契約がない。PTモードでのR1白炉・解析照明とラスタ/PT一致は未検証。 |
| SPP収束・公開Cornell | R7-P4は`blocked`。16/64/256 sppの接頭列MSEと公開参照比較の専用テストは未登録。現行の16試料GPUスモークはこの条件の代わりにならない。 |
| 薄レンズ・シャッター | R7-P5の解析CoC `49.7312 px`に対する1024試料の測定CoCは`49.7069 px`。静止・移動32試料の固定GPU基準と決定論性を確認。 |
| 決定論EXR | R7-P6の同一seed・32 SPP・frameの2出力はbyte一致し、SHA-256は`9333237a36a6a8ee732fe55557a430706c5a831b71e7d0f8bef3edb80291d768`。独立読取でRGB float32/ZIPとwindowを検査し、3枚の全2,099画素が有限。 |

## 公開Cornell参照とR1数値契約

[Cornell Bowersの公開データ](https://bowers.cornell.edu/computer-graphics/data)はdiffuse Cornell boxの形状・反射率と合成RGBEを公開する。R4で固定した`Test/Core/Rendering/Baselines/RenderingValidation/R4CornellReference.rgbe`は512×512、SHA-256は`D94EF786F6BBC70AF81CE700765A598F8AFF73157234B2F50EE5A42C3F9C9F22`。R4の`R4CornellAcceptance.tsv`は白壁ROIで露出scaleを1回決め、影床・赤/緑反射の平均輝度相対誤差を各25%以内、色比差を各0.10以内とする。現行R4単独captureは影床`0.147881`、赤`0.152345`、緑`0.106696`、色比差`0.0194377`/`0.0229986`で合格した。この数値はPTとR4を比較した結果ではない。

R1の`RenderingHdrSceneCaptureTest --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical`は40静的行、68数値行、120 captureで成功した。これはラスタ側の契約であり、PTモードで同じ白炉・解析照明条件を評価した証拠はない。

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
