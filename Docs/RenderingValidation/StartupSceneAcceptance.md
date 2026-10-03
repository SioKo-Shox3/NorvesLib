# 起動画面の受入れ判定

判定日: 2026-10-03（初回 2026-10-02）。起動画面（Rendering3DTest）の描画改善（TASKS.md の `SS-` の項目。ブランチ `feature/startup-scene-rendering`）を、変更前（`163ffe5`）と並べた撮影と、1280×720 の GPU のフレーム時間で受入れる。撮影の画像と数値は `.harness/runs/` に置き、リポジトリには含めない。検証の出力は `.harness/runs/20261003-041958/verify-SS-ACCEPT-<n>.txt`。

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
