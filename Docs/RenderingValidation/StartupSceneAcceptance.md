# 起動画面の受入れ判定

判定日: 2026-10-03（初回 2026-10-02）。起動画面（Rendering3DTest）の描画改善（TASKS.md の `SS-` の項目。ブランチ `feature/startup-scene-rendering`）を、変更前（`163ffe5`）と並べた撮影と、1280×720 の GPU のフレーム時間で受入れる。撮影の画像と数値は `.harness/runs/` に置き、リポジトリには含めない。検証の出力は `.harness/runs/20261003-041958/verify-SS-ACCEPT-<n>.txt`。

その後のテクスチャのミップ・MegaGeometry の太陽の影・高ポリの球（ブランチ `feature/startup-scene-detail`）の受入れは、末尾の「[テクスチャのミップ・MegaGeometry の影・高ポリの球](#テクスチャのミップmegageometry-の影高ポリの球)」に記録する。それより前の節の数値と画像は `b347eb7` の時点のもの。

## 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| Release の構成のビルド | `cmake --build build --config Release --target Game -- /m:1` | EXIT_CODE=0（`verify-SS-ACCEPT-1.txt`） |
| RelWithDebInfo の構成のビルド | `cmake --build build --config RelWithDebInfo --target Game -- /m:1` | EXIT_CODE=0（`-2`） |
| 朝・昼・夕 × 既定・近接・低角度の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT -SunElevations 10,45,3` | 9枚、result=pass（シェーダーのコンパイル失敗なし、終了コード0。`-3`） |
| 夜の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-night -Night` | 3枚、result=pass（`-4`） |
| Release の構成での撮影 | 上の2つに `-Configuration Release`（出力先 `SS-ACCEPT-release`・`SS-ACCEPT-release-night`） | 12枚、result=pass（`-12`・`-13`）。画面の平均と Debug の撮影の差は2.0以内（TAA・RTGI の時間方向の揺れの範囲） |
| SS-LOOK-BALANCE の数値の範囲 | `check_look_balance.py`（下記） | 全項目 PASS（`-16`。下の表） |
| GPU のフレーム時間（1280×720） | `Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -GpuTimingFrames 600`（昼夕朝・夜、2回） | 全12視点 × 2回の計 12960 フレームで、加速構造の更新を含む1フレームの GPU の時間がすべて16.6 ms以下（最大 4.905 ms。`-5`・`-6`・`-8`・`-9`） |

撮影は TAA・RTGI 有効（`indirect_lighting=rtgi`）の起動画面の既定のまま撮った。verify の撮影は Debug の構成の Game、GPU の時間は RelWithDebInfo、Release は撮影できることだけを確かめた。GPU は NVIDIA GeForce RTX 4080。

## 変更前と変更後の比較

変更前は `163ffe5` に撮影の経路だけを足した `73fdb6a`（SS-CAPTURE。描画の変更を含まない）の撮影 `.harness/runs/startup-capture/SS-CAPTURE/` を使う。変更前には太陽の向きを指定する手段が無く、既定の1つの時刻だけを持つ。

視点ごとの比較画像（左上から 変更前・朝10°・昼45°・夕3°・夜。`-17`）:

- 既定: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-default.png`
- 近接: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-near.png`
- 低角度: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-low.png`

個々の画像は `SS-ACCEPT/<視点>-sun<高度>.png`・`SS-ACCEPT-night/<視点>-night.png`（数値は各ディレクトリの `metrics.json`）。

### 同じカメラの条件での比較

既定視点は、変更前と変更後で起動時のカメラが違う（SpringArm の yaw・pitch・腕の長さが 変更前 `0,30,5`、変更後 `0,20,10`。回転の中心はどちらも原点）。上の `compare-default.png` は各版の既定のカメラのままなので構図が違う。同じカメラの条件で比べるため、変更後を変更前の既定のカメラ（`-DefaultCamera 0,30,5`）で撮って並べた。

- 既定（同じカメラ `0,30,5`）: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-default-samecam.png`（左上から 変更前・朝10°・昼45°・夕3°・夜。`-18`）
- 撮影: `SS-ACCEPT-oldcam/default-sun<高度>.png`・`SS-ACCEPT-oldcam-night/default-night.png`（`-14`・`-15`。Game.log に `Rendering3DTest startup camera yaw=0 pitch=30 arm=5`）

画面の平均輝度は 朝111.4・昼117.7・夕80.7・夜62.8（変更前 143.7）。光源の球・岩・大きな球が画面のほぼ同じ位置に来る。大きな球の中心の高さは変更後に 0.5 → 0 へ下げたため、球は少し低く写る（シーンの変更で、カメラの違いではない）。

近接・低角度は、変更前と変更後の撮影スクリプトが同じカメラ（近接 `0,5,2.5`、低角度 `20,-8,6`）を渡しているので、`compare-near.png`・`compare-low.png` はそのまま同じカメラの条件の比較になる。

見た目の主な違い:

- 変更前は静的HDR（昼の芝生と木の写真）を背景と環境光にし、市松模様の平面の上に球と岩を浮かせた構図だった。変更後は物理空と空の太陽による屋外で、広い石畳の地面・材質見本の球の列・Cottage を置き、太陽の高度で朝・昼・夕を、`--night` で白熱電球（黒体 2850 K）の点光源だけの夜を撮れる。
- 変更後は方向光の影（カメラ距離で分けた CSM とコンタクトシャドウ）、点光源のキューブシャドウ、GTAO、RTGI による地面の照り返し、TAA、段階的なブルーム、自動露出、起動画面用のトーンマップとグレーディングが掛かる。
- 変更前の画面の平均輝度は 143.7〜182.4/255 と明るく、低角度では地面と空が白っぽく写っていた。変更後は 84.1〜121.1/255（夜 51.6〜63.7/255）に収まる。

| 視点 | 変更前 | 朝10° | 昼45° | 夕3° | 夜 |
|---|---|---|---|---|---|
| 既定 | 143.7 | 112.8 | 118.3 | 84.1 | 52.9 |
| 近接 | 167.4 | 115.5 | 118.8 | 87.8 | 51.6 |
| 低角度 | 182.4 | 116.5 | 121.1 | 92.8 | 63.7 |

（画面全体の平均輝度、0〜255。`metrics.json` の `mean_luminance`）

## SS-LOOK-BALANCE の数値の範囲

SS-LOOK-BALANCE の完了条件の判定（`.harness/runs/20261002-185756/check_look_balance.py`）を受入れの撮影（`SS-ACCEPT`・`SS-ACCEPT-night`）に掛けた。出力は `verify-SS-ACCEPT-16.txt`。

| 条件 | 範囲 | 測定値 | 判定 |
|---|---|---|---|
| (1) 画面の平均（朝） | 95〜135 | 112.8・115.5・116.5 | PASS |
| (1) 画面の平均（昼） | 105〜140 | 118.3・118.8・121.1 | PASS |
| (1) 画面の平均（夕） | 75〜120 | 84.1・87.8・92.8 | PASS |
| (1) 画面の平均（夜） | 25〜80 | 52.9・51.6・63.7 | PASS |
| (2) 白飛び画素率（全12枚） | 1%未満 | すべて0 | PASS |
| (2) 黒つぶれ画素率（夜以外の9枚） | 2%未満 | すべて0 | PASS |
| (3) 昼の影の中の比（表示のリニア輝度） | 15〜40% | 既定 20.94%・近接 17.59% | PASS |
| (4) 昼の画面上端（y<60）の B − R | 40以上 | 既定 42.0・近接 65.8・低角度 66.4 | PASS |
| (5) 夕の日向の地面の平均色 | R > G > B | 既定 (98.5, 68.6, 61.4)・近接 (112.5, 64.9, 36.6)・低角度 (88.1, 61.5, 55.9) | PASS |
| (6) 夜の画面上端の輝度 | 40未満 | 3.0・4.7・11.0 | PASS |
| (6) 夜の光だまりと周り | 周りの3倍超 | 光だまり 0.5138、左奥 0.0033・左下 0.0237 | PASS |
| (6) 夜の球・岩の影 | 照らされた隣の半分未満 | 球 0.0041 / 0.0390、岩 0.0899 / 0.6249 | PASS |

視点の順は既定・近接・低角度。白飛び（R・G・B がすべて255）は見た目の LUT が白を符号化値0.99へ下げるため常に0になる。参考に R・G・B がすべて250以上の画素の割合も求め、全12枚で0だった。

## GPU のフレーム時間

### 測り方

- 構成: RelWithDebInfo（`/O2 /Ob1`。Release は `/O2 /Ob2`）。最適化が有効で、統計 `NORVES_ENABLE_STATS` が残る構成で測る。Release は GPU の計測やログのデバッグの機能を入れない構成（`NORVES_ENABLE_STATS=0`・`NORVES_ENABLE_LOGGING=0`）なので測らず、ビルドが通り起動画面を撮影できることだけを確かめた（上の一覧）。
- 値: `--trace-file` のトレースの `Type=GPU` の行の `FrameGPU`。RenderingCoordinator がコマンドの記録の最初（レイトレの加速構造の構築・更新の前）から最後までをタイムスタンプで囲んだ区間で、加速構造の更新・RenderGraph の全パス・表示への書き出しを含む。加速構造の更新は `AccelerationStructureBuild`、RenderGraph のパスはパス名の行として同じフレームの番号で出る。
- 窓: アセットの読み込みが落ち着いてから600描画フレーム走らせ、`FrameGPU` を持つフレームのうち撮影の直前2フレームを除いた最後の540フレーム。
- 判定: 窓の1フレームごとの値を予算16.6 msと比べ、1フレームでも超えたら予算超えとする（`metrics.json` の `within_budget`。95パーセンタイルの判定は `p95_within_budget`）。超えたフレームは、パスごとの時間と窓の中央値からの増分を `over_budget_frames` に書く。
- 画面: 1280×720（`metrics.json` の `width`・`height`）。

### 測定値

計測3: `.harness/runs/startup-capture/SS-ACCEPT-gpu3/`・`SS-ACCEPT-gpu3-night/`（`verify-SS-ACCEPT-6.txt`・`-5.txt`）。計測4: `SS-ACCEPT-gpu4/`・`SS-ACCEPT-gpu4-night/`（`-8`・`-9`）。単位は ms。各計測540フレーム。

| 視点 | 計測3 中央値 | 計測3 p95 | 計測3 最大 | 計測4 中央値 | 計測4 p95 | 計測4 最大 | 予算超え（3 / 4） |
|---|---|---|---|---|---|---|---|
| 既定 朝10° | 3.810 | 4.203 | 4.457 | 3.763 | 4.142 | 4.416 | 0 / 0 |
| 既定 昼45° | 3.810 | 4.197 | 4.851 | 3.767 | 4.178 | 4.558 | 0 / 0 |
| 既定 夕3° | 3.795 | 4.188 | 4.401 | 3.805 | 4.194 | 4.404 | 0 / 0 |
| 近接 朝10° | 3.133 | 4.029 | 4.767 | 3.135 | 4.058 | 4.568 | 0 / 0 |
| 近接 昼45° | 3.071 | 3.996 | 4.565 | 3.131 | 4.027 | 4.594 | 0 / 0 |
| 近接 夕3° | 3.128 | 4.075 | 4.905 | 3.167 | 3.985 | 4.706 | 0 / 0 |
| 低角度 朝10° | 3.279 | 3.804 | 4.009 | 3.259 | 3.876 | 4.462 | 0 / 0 |
| 低角度 昼45° | 3.220 | 3.854 | 4.058 | 3.256 | 3.918 | 4.805 | 0 / 0 |
| 低角度 夕3° | 3.228 | 3.790 | 4.011 | 3.200 | 3.752 | 4.312 | 0 / 0 |
| 既定 夜 | 3.566 | 4.013 | 4.199 | 3.633 | 4.071 | 4.409 | 0 / 0 |
| 近接 夜 | 3.611 | 4.146 | 4.478 | 3.642 | 4.176 | 4.334 | 0 / 0 |
| 低角度 夜 | 3.090 | 3.717 | 4.243 | 2.994 | 3.616 | 3.996 | 0 / 0 |

24の計測（視点 × 条件 × 計測）のすべてで、窓の全フレームが予算16.6 ms以下だった。最大は計測3の近接・夕の4.905 msで、予算までの余裕は11.7 ms。

### パスごとの内訳

窓の中央値（計測3。単位 ms）。加速構造の更新は静止したシーンで毎フレーム TLAS の更新（refit）だけを記録し、0.02 ms前後。RTGI（`DiffuseIndirect`）とデノイズは LightingPass の中で記録する。

| 視点 | GBufferPass | MegaGeometryPass | ShadowMapPass | LightingPass | SSAOPass | SSRPass | AccelerationStructureBuild |
|---|---|---|---|---|---|---|---|
| 既定 昼45° | 1.663 | 0.549 | 0.478 | 0.453 | — | — | 0.017 |
| 近接 昼45° | 0.881 | 0.343 | 0.451 | 0.569 | — | — | 0.019 |
| 低角度 昼45° | 0.759 | 0.557 | 0.481 | 0.539 | — | — | 0.024 |
| 既定 夜 | 1.665 | 0.530 | 0.450 | 0.438 | 0.112 | 0.099 | 0.018 |

（昼の SSAO・SSR は `metrics.json` の `pass_median_ms` にある。計測3・4の全視点で SSAOPass 0.10〜0.16・SSRPass 0.09〜0.15・VolumetricsPass 0.01〜0.32・BloomPass 0.07〜0.12・TemporalAAPass 0.05〜0.09、ほかのパスは0.04 ms以下）

予算を超えたフレームは無かったため、超えたフレームの内訳は無い。内訳の出力の経路は、予算を仮に4.0 msにした計測（既定の夜、`SS-ACCEPT-gpu3-budget4-night/`、`-7`）で確かめた。540中44フレームが4.0 msを超え、最大の4.343 msのフレームの内訳は GBufferPass 1.876（中央値から +0.210）・ShadowMapPass 0.681（+0.210）・MegaGeometryPass 0.677（+0.141）・LightingPass 0.645（+0.215）、どの区間にも入らない残り 0.028 ms。44フレームの増分の平均は GBufferPass +0.226・LightingPass +0.207 で、ほかは +0.03 以下だった。

軽くする先の目安: 既定視点では GBufferPass が FrameGPU の約44%（1.66 / 3.8 ms）を占め、揺れの増分も GBufferPass と LightingPass に出る。予算を超えるフレームが出た場合は、まず GBufferPass（POM を含む G バッファの書き込み。地面と Cottage が画面の多くを覆う既定視点で、近接・低角度の約2倍）、次に LightingPass（RTGI のレイとデノイズを含む）を見る。

### 以前の計測との違い

2026-10-02 の計測1・2（`SS-ACCEPT-gpu*`・`SS-ACCEPT-gpu-run2*`、トレースの Frame 行の `GPUFrameMs`）では、同じ視点の中央値が 4.09〜9.48 ms と揺れ、既定の夜で540中6フレーム（最大 18.16 ms）、既定の朝で1フレーム（16.87 ms）が予算を超えた。当時のトレースには GPU のパスごとの行が無く、これらのフレームの内訳は取れない。

今回のトレースでは Frame 行の `GPUFrameMs`（既定の夜の中央値 3.631・最大 4.409）と `FrameGPU` の行（3.633・4.409）が一致し、同じ指標で計測1の既定の夜は中央値 9.48 ms だった（約2.6倍）。その間の描画の変更は電球の色・ブルームのシェーダーのレンズダートの数行・タイムスタンプの追加だけで、パスの負荷を2倍以上にするものは無い。計測1の値は全体が一様に遅い形で、計測の時点の GPU のクロックの状態や同じ GPU で動くほかの描画の影響と見られる（推定。当時のパスごとの値が無いため確かめられない）。

### 既知の限界

- 計測の時点の GPU の状態で値が揺れる（上記）。今回の2回の計測は中央値の差が視点ごとに0.1 ms以内だった。
- Release の構成そのものの GPU 時間は測っていない（計測の仕組みを入れない構成のため。RelWithDebInfo との違いは CPU 側のインライン展開の度合いで、GPU のコマンドは同じ）。

## 撮影の注意

撮影中にゲームのウィンドウがマウスのクリックを受けると、`PickingController` が物体を選んで水色の AABB の線を画面に描く（同時に Game.log へ `Debug line vertex capacity exceeded` の警告が出る）。今回の受入れの撮影（`SS-ACCEPT*` の Debug・Release・同じカメラの撮影）では、水色（R<80・G>180・B>180）の画素が20を超える画像は無かった。

Release の構成はログを書かない（`NORVES_ENABLE_LOGGING=0`）ため、Release の撮影では Game.log によるシェーダーのコンパイル失敗と間接光の出どころの検査を飛ばす（`metrics.json` の `game_log`・`indirect_lighting` が空）。シェーダーは Debug の撮影の Game.log で失敗が無いことを確かめた。

## テクスチャのミップ・MegaGeometry の影・高ポリの球

判定日: 2026-10-03。FIX-ASYNC-TEXTURE-MIPS（`451be2a`）・SS-CSM-MEGA-CASTERS（`695e9d2`・`8123494`）・SS-MEGA-SPHERE（`d173855`）・SS-MEGA-SPHERE-DISPLACE（`b632987`）の後の起動画面を、変更前（`b347eb7`）と並べて受入れる。撮影・SS-LOOK-BALANCE の判定・GPU のフレーム時間は、その後の FIX-MEGA-CLUSTER-ADJACENCY（`13ba56a`・`cba0ca6`。クラスタ化の隣接と法線コーンのカリング）と SS-MEGA-LOD-PERF（`cb84375`・`da1ee8f`。大きな球と影の MegaGeometry の LOD を画面上・影の地図のテクセル上の誤差で選ぶ）の後（`dd3e4e5`）で撮り直した。撮り直しの検証の出力は `.harness/runs/20261003-181031/verify-SS-ACCEPT-PERF-<n>.txt`、最初の受入れ（`b632987`）の出力は `.harness/runs/20261003-114333/verify-SS-ACCEPT-DETAIL-<n>.txt`。撮影は `.harness/runs/startup-capture/` の下に置く。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| RelWithDebInfo の構成のビルド | `cmake --build build --config RelWithDebInfo --target Game -- /m:1` | EXIT_CODE=0（`-PERF-1`。Debug は `-PERF-0-smoke`） |
| 朝・昼・夕 × 既定・近接・低角度の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-PERF -SunElevations 10,45,3` | 9枚、result=pass（`-PERF-2`） |
| 夜の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-PERF-night -Night` | 3枚、result=pass（`-PERF-3`） |
| SS-LOOK-BALANCE の数値の範囲 | `check_look_balance.py`（`.harness/runs/20261002-185756/`） | 全項目 PASS（`-PERF-8`） |
| GPU のフレーム時間（1280×720） | `-Configuration RelWithDebInfo -GpuTimingFrames 600`（昼夕朝・夜、2回） | 12視点 × 2回の計12960フレームのうち468フレームが16.6 msを超えた（最大 37.261 ms）。計測の間、別のアプリが GPU を約4割使っていた（`-PERF-12`）。同じコードを競合なしで測った昼の3視点は予算超え0（下） |
| Release の構成のビルドと撮影 | `-Configuration Release`（最初の受入れの回） | EXIT_CODE=0、12枚 result=pass（`-DETAIL-1`・`-9`・`-10`） |
| 起動から撮影まで | Game.log の最初の行から `capture_png saved` まで | 最初の受入れの回で変更前との差は −1.1〜+1.5 s。撮り直しの回は機械全体が遅かった（下） |

撮影は TAA・RTGI 有効（`indirect_lighting=rtgi`）の既定のまま、verify の撮影は Debug、GPU の時間は RelWithDebInfo で撮った。GPU は NVIDIA GeForce RTX 4080。

### 変更前と変更後の比較

変更前は `SS-ACCEPT/`・`SS-ACCEPT-night/`（上の節の受入れの撮影。撮影したコード `229556d` は `b347eb7` と同じツリー）。撮影スクリプトの視点は変更前後で同じ（既定は起動時の既定のカメラ `0,20,10`、近接 `0,5,2.5`、低角度 `20,-8,6`）。

視点ごとの比較画像（上の段が変更前、下の段が変更後 `dd3e4e5`。左から 朝10°・昼45°・夕3°・夜）:

- 既定: `.harness/runs/startup-capture/SS-ACCEPT-PERF-compare/compare-default.png`
- 近接: `.harness/runs/startup-capture/SS-ACCEPT-PERF-compare/compare-near.png`
- 低角度: `.harness/runs/startup-capture/SS-ACCEPT-PERF-compare/compare-low.png`
- 大きな球の拡大（等倍の切り出しを2倍。左から 変更前・最適化の前 `SS-ACCEPT-DETAIL`・最適化の後 `SS-ACCEPT-PERF`）: `zoom-<既定|近接|低角度>-<sun45|sun3|night>.png`（例 `zoom-near-sun45.png`・`zoom-default-sun45.png`）
- 最適化の前後の差（画素値の差を8倍）: `diff8x-detail-vs-perf-<視点>-<条件>.png`

作り方は `.harness/runs/20261003-181031/make_compare.py`（出力は `-PERF-10`）。

| 視点 | 朝10° 前→後 | 昼45° 前→後 | 夕3° 前→後 | 夜 前→後 |
|---|---|---|---|---|
| 既定 | 114.3 → 112.7 | 117.6 → 118.9 | 83.6 → 83.9 | 52.9 → 53.3 |
| 近接 | 115.4 → 116.7 | 118.0 → 121.1 | 87.8 → 88.7 | 51.5 → 53.6 |
| 低角度 | 116.5 → 117.8 | 120.8 → 121.8 | 93.0 → 94.1 | 63.6 → 64.1 |

（画面全体の平均輝度、0〜255。`metrics.json` の `mean_luminance`。変更前の値は `SS-ACCEPT/` の現在の撮影のもので、上の節の表は同じ版の別の回の撮影）

見た目の違い（撮影を開いて確かめた）:

- 太陽の影: 変更前は小屋と岩に太陽の影が無く、接地部のコンタクトシャドウだけだった。変更後は朝・昼・夕の既定視点で小屋の影が地面へ落ち（朝・昼は小屋の左、夕は長く伸びる）、岩の影も地面に落ちる。CSM の各カスケードへ描く MegaGeometry は岩（LOD0、66,122 三角形）・小屋（LOD0、4,281 三角形）・大きな球（c0 は LOD3 の 16,128、c1〜c3 は LOD4 の 3,968 三角形）で、カスケードごとに 86,531・74,371・74,371・74,371 三角形（Debug の Game.log の `csm_mega_levels`・`csm_mega_triangles`）。最初の受入れの回（各カスケード 135,427 三角形、球は LOD2）より少ないが、昼の影の中の比は 既定 20.56 → 20.54%・近接 17.19 → 17.20% と変わらず（下の (3)）、既定視点の最適化の前との画素の差の平均は 0.36〜1.21。
- 大きな球: 変更前は UV 1周に4Kを1回貼った石が横に伸びた模様で、輪郭は滑らか。変更後は石の大きさが地面の石畳とほぼ同じになり、近接・低角度で輪郭が石の凹凸で波打ち、目地の窪みに陰が入る。画面に描く段は Game.log の `mega_lod_select` で 近接 LOD0（1,046,528 三角形。LOD1 の誤差 1.862 画素が閾値1画素を超えるため）・低角度 LOD1（261,120 三角形、誤差 0.579 画素）・既定 LOD4（3,968 三角形、誤差 0.866 画素）。近接は最適化の前と同じ LOD0 で、拡大画像の差は球の自転の位相のずれ（模様が横へずれる）だけで目地の細部は同じ。既定の LOD4 は輪郭に角が見えず、球の陰影は最適化の前（LOD0〜2）よりわずかに暗く柔らかい（backlog の FIX-MEGA-LOD-SHADING の差）。
- 石畳のミップ: Game.log で石畳の5枚（基本色・法線・ラフネス・AO・高さ）がすべて `mip_levels=13`。地面の遠景のざらつきの減少は FIX-ASYNC-TEXTURE-MIPS の記録のとおりで、画面の平均の変更前後の差は全視点で −1.6〜+3.1。
- 最適化の前（`SS-ACCEPT-DETAIL`）との画素の差の平均（0〜255）は 既定 0.36〜1.21・低角度 0.75〜1.62・近接 2.85〜5.40（16を超える画素 10〜13%）。近接の差は自転の位相のずれによるもので、同じコードの2回の撮影でも平均3.2の差が出ていた（FIX-MEGA-CLUSTER-ADJACENCY の記録）。

### SS-LOOK-BALANCE の数値の範囲

`check_look_balance.py` を `SS-ACCEPT-PERF`・`SS-ACCEPT-PERF-night` に掛けた（`verify-SS-ACCEPT-PERF-8.txt`、RESULT pass）。

| 条件 | 範囲 | 測定値 | 判定 |
|---|---|---|---|
| (1) 画面の平均（朝） | 95〜135 | 112.7・116.7・117.8 | PASS |
| (1) 画面の平均（昼） | 105〜140 | 118.9・121.1・121.8 | PASS |
| (1) 画面の平均（夕） | 75〜120 | 83.9・88.7・94.1 | PASS |
| (1) 画面の平均（夜） | 25〜80 | 53.3・53.6・64.1 | PASS |
| (2) 白飛び画素率（全12枚） | 1%未満 | すべて0 | PASS |
| (2) 黒つぶれ画素率（夜以外の9枚） | 2%未満 | すべて0 | PASS |
| (3) 昼の影の中の比（表示のリニア輝度） | 15〜40% | 既定 20.54%・近接 17.20% | PASS |
| (4) 昼の画面上端（y<60）の B − R | 40以上 | 既定 42.5・近接 67.4・低角度 66.9 | PASS |
| (5) 夕の日向の地面の平均色 | R > G > B | 既定 (96.2, 68.6, 63.3)・近接 (112.5, 65.2, 37.9)・低角度 (89.0, 62.3, 57.0) | PASS |
| (6) 夜の画面上端の輝度 | 40未満 | 3.1・4.3・10.7 | PASS |
| (6) 夜の光だまりと周り | 周りの3倍超 | 光だまり 0.5275、左奥 0.0025・左下 0.0227 | PASS |
| (6) 夜の球・岩の影 | 照らされた隣の半分未満 | 球 0.0042 / 0.0380、岩 0.0948 / 0.6386 | PASS |

判定スクリプトの影・光だまりの領域は画素の座標で固定している。大きな球を変えた後も領域が影と日向に当たっていることは、最初の受入れの回に領域を描いた画像で確かめた（`SS-ACCEPT-DETAIL-compare/regions-default-sun45.png`・`regions-near-sun45.png`・`regions-default-night.png`）。撮り直しで球・小屋・岩の位置と影の形は変わっていない（比較画像）。参考の R・G・B がすべて250以上の画素の割合は全12枚で0。

### GPU のフレーム時間

測り方は上の節の「測り方」と同じ（RelWithDebInfo、`FrameGPU` は加速構造の更新を含む区間、600描画フレームのうち最後の540フレーム、1フレームでも16.6 msを超えたら予算超え）。計測1: `SS-ACCEPT-PERF-gpu/`・`SS-ACCEPT-PERF-gpu-night/`（`-PERF-4`・`-5`）。計測2: `SS-ACCEPT-PERF-gpu2/`・`SS-ACCEPT-PERF-gpu2-night/`（`-PERF-6`・`-7`）。表は `verify-SS-ACCEPT-PERF-9.txt`（`gpu_table.py`）。単位は ms。

**計測の条件:** この2回の計測の間、別のアプリ（`javaw`）が開いていて、Game が動いていない時点でも GPU の使用率が 36〜40%（グラフィックスのクロック 1380〜1680 MHz、約52 W）あった（`verify-SS-ACCEPT-PERF-12.txt`）。GPU を時分割で共有した分だけ各パスの時間が伸び、同じ視点・同じコードの2回で中央値が2倍違う（近接 夕3° 16.144 / 7.922 ms）。最適化そのものの効果は、同じコード（`da1ee8f` のビルド）を約20分前に競合なしで測った SS-MEGA-LOD-PERF の計測（`SS-MEGA-LOD-PERF-gpu/`、`.harness/runs/20261003-181031/verify-SS-MEGA-LOD-PERF-5.txt`）で読む。

| 視点（昼45°、競合なし） | 中央値 | p95 | 最大 | 予算超え | MegaGeometryPass | ShadowMapPass | LightingPass |
|---|---|---|---|---|---|---|---|
| 既定 | 3.655 | 5.593 | 7.584 | 0 / 540 | 0.606 | 0.743 | 0.899 |
| 近接 | 8.423 | 10.683 | 14.412 | 0 / 540 | 4.616 | 0.802 | 1.335 |
| 低角度 | 4.467 | 6.858 | 7.771 | 0 / 540 | 1.586 | 0.785 | 0.562 |

12視点 × 2回（競合あり）:

| 視点 | 計測1 中央値 | 計測1 p95 | 計測1 最大 | 計測2 中央値 | 計測2 p95 | 計測2 最大 | 予算超え（1 / 2） | MegaGeometryPass 中央値（1 / 2） | ShadowMapPass 中央値（1 / 2） |
|---|---|---|---|---|---|---|---|---|---|
| 既定 朝10° | 5.786 | 10.036 | 18.250 | 4.075 | 7.841 | 9.672 | 1 / 0 | 0.578 / 0.147 | 2.292 / 1.213 |
| 既定 昼45° | 5.130 | 9.976 | 18.506 | 4.007 | 7.448 | 10.715 | 2 / 0 | 0.717 / 0.147 | 1.533 / 1.186 |
| 既定 夕3° | 4.806 | 8.479 | 11.889 | 6.060 | 13.622 | 21.109 | 0 / 6 | 0.684 / 0.450 | 1.441 / 2.052 |
| 近接 朝10° | 9.671 | 17.161 | 31.871 | 12.004 | 21.950 | 27.810 | 35 / 99 | 4.807 / 6.117 | 1.578 / 2.162 |
| 近接 昼45° | 11.516 | 21.204 | 37.261 | 9.068 | 13.461 | 18.957 | 83 / 2 | 5.500 / 4.246 | 1.902 / 1.520 |
| 近接 夕3° | 16.144 | 24.915 | 28.334 | 7.922 | 11.109 | 16.134 | 239 / 0 | 6.969 / 3.746 | 4.252 / 1.206 |
| 低角度 朝10° | 7.159 | 12.866 | 16.147 | 4.610 | 8.005 | 12.087 | 0 / 0 | 1.829 / 1.040 | 2.270 / 1.513 |
| 低角度 昼45° | 5.949 | 11.198 | 16.662 | 4.961 | 9.069 | 15.402 | 1 / 0 | 1.702 / 0.945 | 1.494 / 1.219 |
| 低角度 夕3° | 4.522 | 8.468 | 11.541 | 4.749 | 8.072 | 12.248 | 0 / 0 | 0.942 / 1.139 | 1.352 / 1.470 |
| 既定 夜 | 3.452 | 6.977 | 8.627 | 3.814 | 7.143 | 8.578 | 0 / 0 | 0.155 / 0.169 | 1.291 / 1.408 |
| 近接 夜 | 7.762 | 10.532 | 14.367 | 8.125 | 11.006 | 15.887 | 0 / 0 | 4.030 / 4.087 | 1.134 / 1.408 |
| 低角度 夜 | 4.346 | 7.819 | 9.673 | 4.230 | 7.525 | 9.984 | 0 / 0 | 1.213 / 1.158 | 1.354 / 1.371 |

ほかのパスの中央値は全視点・2回で LightingPass 0.53〜2.04・GBufferPass 0.15〜0.19・VolumetricsPass 0.01〜0.46・SSAOPass 0.09〜0.11・SSRPass 0.03〜0.08・BloomPass 0.07・TemporalAAPass 0.05・AccelerationStructureBuild 0.015〜0.017。

SS-ACCEPT-DETAIL（最適化の前、同じ測り方の2回）からの変化:

| 視点 | 中央値 前 → 後 | MegaGeometryPass 前 → 後 | ShadowMapPass 前 → 後 | 最大 前 → 後 |
|---|---|---|---|---|
| 既定 朝・昼・夕 | 3.68〜5.22 → 4.01〜6.06 | 0.55〜1.10 → 0.15〜0.72 | 0.80〜1.50 → 1.19〜2.29 | 8.28〜16.18 → 9.67〜21.11 |
| 近接 朝・昼・夕 | 8.49〜9.28 → 7.92〜16.14 | 5.44〜5.97 → 3.75〜6.97 | 0.78〜0.86 → 1.21〜4.25 | 14.58〜17.80 → 16.13〜37.26 |
| 低角度 朝・昼・夕 | 4.19〜4.68 → 4.52〜7.16 | 1.27〜1.58 → 0.94〜1.83 | 0.79〜0.87 → 1.22〜2.27 | 9.94〜13.23 → 11.54〜16.66 |
| 夜 3視点 | 3.48〜9.03 → 3.45〜8.13 | 0.59〜6.16 → 0.15〜4.09 | 0.75〜1.08 → 1.13〜1.41 | 8.94〜15.51 → 8.58〜15.89 |

- 競合なしの昼の計測（SS-MEGA-LOD-PERF）では、最適化の前（SS-ACCEPT-DETAIL の昼）より 既定 3.88〜5.22 → 3.66・近接 9.04〜9.28 → 8.42 ms と下がり、ShadowMapPass は 0.80〜1.50 → 0.74〜0.80 ms、3視点1620フレームの予算超えは 2 → 0 になった。近接の MegaGeometryPass は 5.69〜5.97 → 4.62 ms（近接は最適化の前と同じ LOD0 で、減ったのは FIX-MEGA-CLUSTER-ADJACENCY のクラスタの大きさと法線コーンのカリングによると見る。推定）。
- 競合ありの今回の計測でも、変わらない側の傾向は読める。既定の MegaGeometryPass は計測2で 0.147 ms（LOD4。最適化の前 0.55〜0.62）、夜の近接の MegaGeometryPass は 5.57〜6.16 → 4.03〜4.09 ms で、夜（太陽の CSM が無い）の3視点は2回とも予算超え0、中央値は最適化の前の2回の範囲の中か、それより下だった。
- 昼・夕・朝の ShadowMapPass は2回の間で 1.19〜4.25 ms と揺れ、競合なしの 0.74〜0.80 ms の1.5〜5倍だった。描く三角形数は最適化の前より少ない（上の「太陽の影」）ため、増えたのは GPU の共有による（推定。パスの中の内訳は取っていない）。夜の ShadowMapPass（点光源のキューブだけ）も2回とも 0.75〜1.08 → 1.13〜1.41 ms と高いが、夜は競合なしの計測が無く、共有の影響と切り分けられていない。

予算を超えたフレームの内訳（`metrics.json` の `over_budget_frames` を `over_budget_summary.py` でまとめた。`verify-SS-ACCEPT-PERF-13.txt`。括弧は窓の中央値からの増分の平均）:

| 視点（計測） | 超えたフレーム | 増分の大きいパス | 最も遅いフレーム |
|---|---|---|---|
| 既定 朝10°（1） | 1 | ShadowMapPass +11.14・MegaGeometryPass +1.60 | 18.250（ShadowMapPass 13.432） |
| 既定 昼45°（1） | 2 | ShadowMapPass +10.46・LightingPass +1.57・MegaGeometryPass +1.01 | 18.506（ShadowMapPass 13.711、GBufferPass 1.589 は中央値の8.4倍） |
| 既定 夕3°（2） | 6 | ShadowMapPass +8.03・MegaGeometryPass +2.14・LightingPass +1.99 | 21.109（ShadowMapPass 15.248） |
| 近接 朝10°（1 / 2） | 35 / 99 | ShadowMapPass +4.65 / +3.75・MegaGeometryPass +3.67 / +3.16・LightingPass +1.73 / +1.70 | 31.871（MegaGeometryPass 13.609・ShadowMapPass 8.824・LightingPass 6.860） |
| 近接 昼45°（1 / 2） | 83 / 2 | ShadowMapPass +5.06 / +6.59・MegaGeometryPass +2.82 / +2.52・LightingPass +1.62 / +1.10 | 37.261（ShadowMapPass 14.726・MegaGeometryPass 12.553・LightingPass 6.005・VignettePass 3.247） |
| 近接 夕3°（1） | 239 | ShadowMapPass +4.42・MegaGeometryPass +0.88・LightingPass +0.75 | 28.334（MegaGeometryPass 12.929・ShadowMapPass 8.901） |
| 低角度 昼45°（1） | 1 | ShadowMapPass +7.96・MegaGeometryPass +2.57 | 16.662（ShadowMapPass 9.454） |

超えたフレームでは ShadowMapPass・MegaGeometryPass・LightingPass が同じフレームで数倍になり、変更していない軽いパス（VignettePass 0.012 → 3.247 ms、GBufferPass 0.19 → 1.59 ms、VolumetricsPass）まで跳ねるフレームがある。区間に入らない残りは 0.02〜0.28 ms。同じ視点の別の計測では超えない・数フレームだけ（近接 夕3° は 239 / 0、近接 昼45° は 83 / 2）で、競合なしの昼の3視点は0だった。

軽くする案（効く見込みの大きい順）:

1. 近接の大きな球の LOD0（1,046,528 三角形、MegaGeometryPass 4.6 ms）を軽くする。LOD0 と LOD1 の間の段を足しても誤差は約1.2画素の見込みで閾値1画素を超える（SS-MEGA-LOD-PERF の記録）ため、クラスタごとに段を選ぶ（DAG の切り口）か、近接で変位の細部を法線マップへ移して粗い段を使う。
2. ShadowMapPass の跳ねには、岩・小屋の影だけの粗い段（backlog の FIX-MEGA-SHADOW-LOD-LOADED。今は LOD0 を4カスケードすべてへ描く）と、遠いカスケードの更新を数フレームおきにする。
3. 計測を GPU を他のアプリと共有しない状態で回す（予算の判定の前提）。

### 起動から撮影まで

Game.log の最初の行から `capture_png saved` の行まで（秒、Debug）。変更に関係しない区間（起動から環境の IBL の作成 `derived IBL resources created` まで）も並べる。出力は `verify-SS-ACCEPT-DETAIL-12.txt`・`-15.txt`・`verify-SS-ACCEPT-PERF-11.txt`（求め方は `.harness/runs/20261003-114333/startup_phase.py`）。

| 撮影 | コード | 起動から撮影まで（昼夕朝 / 夜） | 関係しない区間（起動から IBL まで） |
|---|---|---|---|
| 変更前 `SS-ACCEPT` | `b347eb7` と同じ | 36.38〜37.13 / 34.00〜36.23 | 18.28〜18.94 |
| 変更後 `SS-MEGA-SPHERE-DISPLACE`（昼・夕と夜） | `b632987` | 36.46〜37.37 / 35.15〜35.79 | 18.58〜19.71 |
| 変更後 `SS-ACCEPT-DETAIL` | `b632987` | 44.25〜55.38 / 40.54〜40.90 | 21.93〜25.52 |
| 変更後 `SS-ACCEPT-DETAIL-run2` | `b632987` | 43.02〜46.85 / 40.80〜43.59 | 21.75〜23.14 |
| 変更後 `SS-ACCEPT-PERF` | `dd3e4e5` | 48.05〜60.20 / 54.18〜62.90 | 24.07〜35.15 |

同じコード（`b632987` のコミットの後、14:41）の `SS-MEGA-SPHERE-DISPLACE` の撮影では、視点ごとの変更前との差は −1.08〜+1.53 s（昼/夕で 既定 +0.67/+0.53、近接 −0.34/+0.07、低角度 +0.71/+0.40、夜は 既定 −1.08・近接 +1.27・低角度 +1.53）。受入れの2回と撮り直しの回では起動から撮影までが 40〜63 s に伸びたが、変更と関係しない「起動から IBL まで」も同じ割合（約1.2〜1.9倍）で遅い。どの区間も一様に遅い形で、撮影した時点の機械の負荷によると見る（撮り直しの回は上の GPU の競合と同じ時間帯）。大きな球の高さマップの読み込みとクラスタ・LOD の構築は別のスレッドの仕事で 2.47 s（Debug、`stage=big_sphere_cluster_lod_build`）、テクスチャがそろった時点で終わっていて待ちは無い（`big_sphere_build_wait wait_ms=0.0`）。

### 既知の限界

- GPU のフレーム時間の12視点 × 2回は、別のアプリが GPU を共有した状態の値で、12960フレーム中468フレームが16.6 msを超えた（内訳は上）。競合なしで測った昼の3視点（1620フレーム）は予算超え0・最大 14.41 ms。朝・夕・夜を含む12視点を競合なしで2回測り直すまで、全視点の予算の判定は保留する（再計測待ち）。
- 近接視点の中央値は競合なしでも 8.42 ms で、予算の半分を超える（大きな球の LOD0 の MegaGeometryPass 4.6 ms が主）。軽くする案は上の1・2。
- 夜の近接（低角度も目で見て同じ）で、大きな球の光源と反対側の明るさが撮るフレームで大きく違う。60フレーム目で撮る Debug・Release の撮影では反対側が暗い（近接で反対側 / 光源側の8bit輝度 0.02〜0.03）が、600フレーム目で撮る RelWithDebInfo の GPU 計測の撮影では反対側が明るい（0.80〜0.84）。変更前（`b347eb7`）の GPU 計測の撮影（`SS-ACCEPT-gpu-night`）でも 0.48〜0.61 で、この節の変更より前からある（`verify-SS-ACCEPT-PERF-14.txt`）。履歴が長く積もる RTGI の間接光と見られるが、原因は確かめていない（TASKS.md の FIX-NIGHT-SPHERE-LONG-RUN）。SS-LOOK-BALANCE の判定は60フレーム目の撮影で行っている。
- 既定視点の大きな球（LOD4）は LOD0 より陰影がわずかに暗く柔らかい（backlog の FIX-MEGA-LOD-SHADING）。
- Release の撮影の画面の平均が Debug より最大8.1低い（最初の受入れの回。昼の既定 Release 111.5・Debug 119.6）。画像の差は画面全体で一様（`SS-ACCEPT-DETAIL-compare/debug-vs-release-default-sun45.png`）で、物の欠けではなく露出の差。Release の既定・昼を撮るフレームを変えると 60フレーム目 112.3・240フレーム目 122.8・600フレーム目 124.2（`SS-ACCEPT-DETAIL-release-settle/`、`-DETAIL-17`）と上がる。撮影は落ち着いてから60描画フレーム目で、Release はフレームが速いぶん自動露出が収束しきる前に撮る。12枚とも SS-LOOK-BALANCE の (1) の範囲の中（朝・昼 107.0〜118.0、夕 80.2〜91.3、夜 51.1〜63.0）。撮り直しの回では Release を撮っていない。
- 起動から撮影までの時間は機械の負荷で揺れる（上記）。
