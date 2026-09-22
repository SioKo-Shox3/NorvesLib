# R6 RTGI・テンポラルデノイザ受入れ記録

策定日: 2026-09-23

## 判定

R6-P1〜P4の機能実装と、R6-P5専用の動的GPU受入れは証拠を持つ。しかし、P5の指定全体`RenderingValidation`が現HEADでも失敗したため、R6-P6は保留とする。`TASKS.md`のR6-P5/R6-P6は`blocked`であり、現時点では`RenderingRoadmap: R6 complete` trailerを付けない。

P5専用テストは固定8 rendered-frame warmup、静止golden、RT無効/R4 fallback、カメラ・物体移動、履歴棄却、移動後停止、ライト移動を検証している。これはP5の個別機能証拠として保持するが、全体gateの失敗を合格へ読み替えない。

## 実装コミット

帳簿だけの保存コミットを除き、R6の機能範囲へ入るコードコミットを次に整理する。

| 工程 | コミット | 内容 |
|---|---|---|
| R6-M1 | `e2ed66c` | 1 bounce diffuse、ray query、テンポラル蓄積、3x3 cross-bilateral、8 rendered-frame warmup、性能gate保留を選定 |
| R6-P1 | `db612c0`, `190e812`, `0d40fc0` | RTGI結果形式、fallback、履歴revision、順序・カリング非依存のSceneRevisionを接続 |
| R6-P2 | `cc3dab4`, `a684739`, `326abc6`, `2be8e7b`, `81b5707` | ray-query compute、LightingPass、RTGI GPU陽性経路、hit/missとfallbackのreadbackを接続 |
| R6-P3 | `09fdbb0` | velocity再投影、current/history ping-pong、rendered FrameNumber連続性を接続 |
| R6-P4 | `2808077`, `db337ae` | temporal出力へ3x3 cross-bilateral denoiseを接続し、pre-exposure契約を維持 |
| R6-P5 | `b032e0d` | 動的GI、静止golden、移動、履歴棄却、fallbackの専用GPU受入れfixtureを追加 |

## 方式と公開契約

- 既定GIは1 bounce diffuseのcompute ray queryとし、R5のTLAS/BLAS snapshotを共有する。R6用のRT pipeline/SBTは追加しない。
- 履歴はvelocity再投影、最大8 rendered frames、confidence、depth/normal/material/revision棄却、動的ライトrevisionの2-frame weight制限を使う。
- デノイザは1回の3x3 cross-bilateral filterとし、外部NRD、複数段SVGF、SSR/TAA置換、specular GI、透過、ReSTIRはR6の対象外とする。
- 公開優先順位は、完全なR6 RTGI、R6無効または失敗時のR4 DDGI、DDGIも無効・非対応・不完全な場合の既存IBL/直接照明・rasterである。不完全なRTGI履歴や出力は公開しない。
- 既定の`Rendering3DTest`起動経路、球、地面、ライト球、方向ライト、boulder、HDR環境は変更していない。

## golden・threshold

R6専用の静止参照は`Docs/RenderingValidation/R6RTGIStaticGolden.tsv`に固定した。schemaは`NorvesLib.RenderingValidation.R6RTGIStaticGolden.v1`、`mean_y=0.258768`、`center_y=0.0305305`、center RGBは`0.0408391,0.0290016,0.0153209`、許容値は`0.001`である。

P5専用受入れでは、8-frame warmup後に`R6_RTGI_WARMUP=PASS`、`R6_STATIC_GOLDEN`の`static_delta=0`、`golden_delta=1.07262e-07`を確認した。ライト移動は`elapsed=4`で`R6_LIGHT_FOLLOWUP=PASS`、進捗`1.23262`だった。

## 検証ログ

記録先はリポジトリrootからの相対パスである。ログは終了コード、CTest結果、readbackまたはcaptureの判定行を開いて確認した。

| 範囲 | 記録先 | 結果 |
|---|---|---|
| R6-P1 / revision契約 | `.harness/runs/20260922-200402/verify-R6-P1-1.txt`, `verify-R6-P1-2.txt`, `verify-R6-P1-FIX-1.txt`, `verify-R6-P1-FIX-2-1.txt`, `verify-R6-P1-FIX-2-2.txt` | Game buildと指定契約/GPUテストを通過。履歴側revision差は即時fallbackに使わず、構成変更だけを不採用条件にした。 |
| R6-P2 / RTGI陽性経路 | `.harness/runs/20260922-200402/verify-R6-P2-FIX-5.txt`〜`verify-R6-P2-FIX-7.txt` | Game build、指定GPU 3件、Vulkan 1.2 shader compileを通過。finite hit、miss zero、RTGI公開、disabled/incomplete fallbackを確認。 |
| R6-P3 | `.harness/runs/20260923-033012/verify-R6-P3-1.txt`, `verify-R6-P3-2.txt` | Game buildとcamera/object velocity・RenderGraphCompileTestの3/3を通過。 |
| R6-P4 | `.harness/runs/20260923-r6-p4-review2/verify-build.txt`, `verify-ctest.txt`, `verify-ctest-LastTest.txt` | Game/対象テストbuild、指定CTest、shader/denoise接続の読戻しを通過。 |
| R6-P5専用 | `.harness/runs/20260923-050747/verify-R6-P5-1.txt`, `verify-R6-P5-3.txt`, `verify-R6-P5-4.txt` | Game build、受入れfixture build、`R6RTGIAcceptanceVulkanTest` 1/1は通過。 |
| R6-P5全体gate | `.harness/runs/20260923-050747/verify-R6-P5-2.txt` | 34 passed・8 skipped・10 failed、EXIT_CODE=8。現HEADの再実行でも34 passed・8 skipped・2 failedとなり、P5 gateを完了扱いにできない。 |
| R6-P6保留確認 | `.harness/runs/20260923-050747/verify-R6-P6-1.txt`〜`verify-R6-P6-3.txt` | `git diff --check`、直近コミット本文、R6/RTGI/性能/Deferred/fallbackの記録を確認する。完了trailer不在を含む保留証拠。 |

## fallbackと既知の制限

- P5専用fixtureの`R6_FALLBACK_RT_DISABLED=PASS`はIBL、`R6_FALLBACK_R4=PASS`はR4 DDGIを確認した。RT capability不在、RT無効、TLAS不完全、resource/dispatch失敗時も、R6の不完全な結果を公開せず既存間接光へ戻す契約をP2/P2-FIXで固定している。
- P5全体gateの再実行で`DDGIProbeRadianceVulkanTest`はpoint/spot occluded hitのchannel 0が期待`4.92249`、実測`5.13309`となった。`RenderingGoldenOutdoorVulkanTest`は`mean_flip=0.002326954`、`max_flip=0.529430032`、`differing_pixels=459`で失敗した。これらを既知baselineと推測して無視せず、P5の再検証条件として残す。
- P5の全体gateログには、RHI image layout系と既存golden系の失敗、Slang SDK未導入によるneural material decoder無効化warningが含まれる。R6の完了根拠へ混ぜず、再実行時の失敗集合と原因を分離して確認する。
- denoised textureそのものの直接readback、遠景の非線形depth閾値、複数frame slotへ拡張した場合のsame-slot履歴境界は、R6の機能受入れを阻害しない追跡事項として`NEXT_FINDINGS.md`に残す。
- R6は1 bounce diffuseまでであり、2 bounce、specular GI、反射・透過、path tracing、ReSTIR、SSR/TAA、外部NRDは実装していない。

## R7暫定参照の再照合条件

R6の`R6RTGIStaticGolden.tsv`はR7未完了時のR6専用暫定参照であり、R7の実装完了やR8の実装を前提にR6を受入れない。R7コアが完了した時点で、同一解像度、カメラ、geometry、material、light、HDR環境、exposure/pre-exposure、rendered-frame warmup条件を揃え、R7の自前path tracer出力とR6の静止ROI・thresholdを再照合する。差分がR7方式による正当な輸送差ならR7側の別参照へ記録し、R6のgoldenを上書きしない。

R8についてはR6-aのvelocity契約だけを利用し、R8の未実装機能をR6の完了条件へ取り込まない。

## 性能gateと完了条件

GPU性能は`Deferred`とする。ray query、テンポラル蓄積、3x3 filter、fallbackの機能検証は行うが、パス別GPU時間はR6の合否へ混ぜず、将来のCI GPU性能回帰トラックで計測する。

P5全体gateが合格し、保留中の失敗を再検証した後にだけ、完了コードコミットへ`RenderingRoadmap: R6 complete` trailerを付ける。現HEADにはそのtrailerを付けていない。
