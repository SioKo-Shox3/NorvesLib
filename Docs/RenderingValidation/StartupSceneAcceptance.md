# 起動画面の受入れ判定

判定日: 2026-10-02。起動画面（Rendering3DTest）の描画改善（TASKS.md の `SS-` の項目。ブランチ `feature/startup-scene-rendering`）を、変更前（`163ffe5`）と並べた撮影と、1280×720 の GPU のフレーム時間で受入れる。撮影の画像と数値は `.harness/runs/` に置き、リポジトリには含めない。

## 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| Release の構成のビルド | `cmake --build build --config Release --target Game -- /m:1` | EXIT_CODE=0 |
| 朝・昼・夕 × 既定・近接・低角度の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT -SunElevations 10,45,3` | 9枚、result=pass（シェーダーのコンパイル失敗なし、終了コード0） |
| 夜の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-night -Night` | 3枚、result=pass |
| SS-LOOK-BALANCE の数値の範囲 | `check_look_balance.py`（下記） | 全項目 PASS（下の表） |
| GPU のフレーム時間（1280×720） | `Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -GpuTimingFrames 600`（昼夕朝・夜） | 未完。RelWithDebInfo の2回の計測では全12視点で95パーセンタイルが16.6 ms以下（最大 15.01 ms）だが、1フレームの値は既定の夜で540中6フレーム（最大18.16 ms）、既定の朝で1フレーム（16.87 ms）が予算を超える。Release の構成での計測・加速構造の更新を含む区間・パスごとの内訳は未取得（下記） |

撮影はすべて TAA・RTGI 有効（`indirect_lighting=rtgi`）の起動画面の既定のまま、Debug の構成の Game で撮った。GPU は NVIDIA GeForce RTX 4080。

## 変更前と変更後の比較

変更前は `163ffe5` に撮影の経路だけを足した `73fdb6a`（SS-CAPTURE。描画の変更を含まない）の撮影 `.harness/runs/startup-capture/SS-CAPTURE/` を使う。変更前には太陽の向きを指定する手段が無く、既定の1つの時刻だけを持つ。

視点ごとの比較画像（左上から 変更前・朝10°・昼45°・夕3°・夜）:

- 既定: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-default.png`
- 近接: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-near.png`
- 低角度: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-low.png`

個々の画像は `SS-ACCEPT/<視点>-sun<高度>.png`・`SS-ACCEPT-night/<視点>-night.png`（数値は各ディレクトリの `metrics.json`）。

### 同じカメラの条件での比較

既定視点は、変更前と変更後で起動時のカメラが違う（SpringArm の yaw・pitch・腕の長さが 変更前 `0,30,5`、変更後 `0,20,10`。回転の中心はどちらも原点）。上の `compare-default.png` は各版の既定のカメラのままなので構図が違う。同じカメラの条件で比べるため、変更後を変更前の既定のカメラ（`-DefaultCamera 0,30,5`）で撮り直して並べた。

- 既定（同じカメラ `0,30,5`）: `.harness/runs/startup-capture/SS-ACCEPT-compare/compare-default-samecam.png`（左上から 変更前・朝10°・昼45°・夕3°・夜）
- 撮影: `SS-ACCEPT-oldcam/default-sun<高度>.png`・`SS-ACCEPT-oldcam-night/default-night.png`（`verify-SS-ACCEPT-20.txt`・`-21.txt`。Game.log に `Rendering3DTest startup camera yaw=0 pitch=30 arm=5`）

画面の平均輝度は 朝112.5・昼118.0・夕82.6・夜71.7（変更前 143.7）。光源の球・岩・大きな球が画面のほぼ同じ位置に来る。大きな球の中心の高さは変更後に 0.5 → 0 へ下げたため、球は少し低く写る（シーンの変更で、カメラの違いではない）。

近接・低角度は、変更前と変更後の撮影スクリプトが同じカメラ（近接 `0,5,2.5`、低角度 `20,-8,6`）を渡しているので、`compare-near.png`・`compare-low.png` はそのまま同じカメラの条件の比較になる。

見た目の主な違い:

- 変更前は静的HDR（昼の芝生と木の写真）を背景と環境光にし、市松模様の平面の上に球と岩を浮かせた構図だった。変更後は物理空と空の太陽による屋外で、広い石畳の地面・材質見本の球の列・Cottage を置き、太陽の高度で朝・昼・夕を、`--night` で点光源だけの夜を撮れる。
- 変更後は方向光の影（カメラ距離で分けた CSM とコンタクトシャドウ）、点光源のキューブシャドウ、GTAO、RTGI による地面の照り返し、TAA、段階的なブルーム、自動露出、起動画面用のトーンマップとグレーディングが掛かる。
- 変更前の画面の平均輝度は 143.7〜182.4/255 と明るく、低角度では地面と空が白っぽく写っていた。変更後は 84.9〜120.4/255（夜 58.3〜74.9/255）に収まる。

| 視点 | 変更前 | 朝10° | 昼45° | 夕3° | 夜 |
|---|---|---|---|---|---|
| 既定 | 143.7 | 113.1 | 119.2 | 84.9 | 58.3 |
| 近接 | 167.4 | 114.1 | 117.5 | 87.4 | 58.4 |
| 低角度 | 182.4 | 115.8 | 120.4 | 92.9 | 74.9 |

（画面全体の平均輝度、0〜255。`metrics.json` の `mean_luminance`）

## SS-LOOK-BALANCE の数値の範囲

SS-LOOK-BALANCE の完了条件の判定（`.harness/runs/20261002-185756/check_look_balance.py`）を受入れの撮影に掛けた。出力は `.harness/runs/20261002-185756/verify-SS-ACCEPT-14.txt`。

| 条件 | 範囲 | 測定値 | 判定 |
|---|---|---|---|
| (1) 画面の平均（朝） | 95〜135 | 113.1・114.1・115.8 | PASS |
| (1) 画面の平均（昼） | 105〜140 | 119.2・117.5・120.4 | PASS |
| (1) 画面の平均（夕） | 75〜120 | 84.9・87.4・92.9 | PASS |
| (1) 画面の平均（夜） | 25〜80 | 58.3・58.4・74.9 | PASS |
| (2) 白飛び画素率（全12枚） | 1%未満 | すべて0 | PASS |
| (2) 黒つぶれ画素率（夜以外の9枚） | 2%未満 | すべて0 | PASS |
| (3) 昼の影の中の比（表示のリニア輝度） | 15〜40% | 既定 20.91%・近接 17.61% | PASS |
| (4) 昼の画面上端（y<60）の B − R | 40以上 | 既定 42.3・近接 65.2・低角度 66.0 | PASS |
| (5) 夕の日向の地面の平均色 | R > G > B | 既定 (96.8, 69.8, 62.1)・近接 (106.5, 66.7, 38.2)・低角度 (85.3, 62.3, 56.2) | PASS |
| (6) 夜の画面上端の輝度 | 40未満 | 3.0・4.7・12.1 | PASS |
| (6) 夜の光だまりと周り | 周りの3倍超 | 光だまり 0.7195、左奥 0.0032・左下 0.0230 | PASS |
| (6) 夜の球・岩の影 | 照らされた隣の半分未満 | 球 0.0040 / 0.0377、岩 0.0872 / 0.8038 | PASS |

視点の順は既定・近接・低角度。白飛び（R・G・B がすべて255）は見た目の LUT が白を符号化値0.99へ下げるため常に0になる。参考に R・G・B がすべて250以上の画素の割合も求め、全12枚で0だった。

## GPU のフレーム時間

### 測り方

- 構成: RelWithDebInfo（`/O2 /Ob1`。Release は `/O2 /Ob2` で、違いは CPU 側のインライン展開の度合いと、統計 `NORVES_ENABLE_STATS` が有効なこと）。Release の構成は統計を無効にしてビルドするため GPU のタイムスタンプを取れない（`Library/Core/CMakeLists.txt` の `NORVES_ENABLE_STATS` は Debug・RelWithDebInfo だけで1）。Release のビルドが通ることは別に確かめた。
- 値: `--trace-file` のトレースの `GPUFrameMs`（RenderingCoordinator がコマンドの記録の最初から最後までを `FrameGPU` のタイムスタンプで囲んだ区間。RenderGraph の全パスと表示への書き出しを含む）。描画したフレームの行（`RenderFrameMs > 0`）だけを使う。
- 区間に含まれないもの: レイトレの加速構造の構築・更新（`RayTracingSceneSubsystem::BuildAccelerationStructures` は `FrameGPU` の前に記録する）。BLAS はメッシュごとに使い回し、静止したシーンでは毎フレーム TLAS の更新（refit）だけを記録する。
- 窓: アセットの読み込みが落ち着いてから600描画フレーム走らせ、撮影の直前2フレームを除いた最後の540フレーム。判定は95パーセンタイルを予算16.6 msと比べる。
- 画面: 1280×720（`metrics.json` の `width`・`height`）。

### 測定値

計測1: `.harness/runs/startup-capture/SS-ACCEPT-gpu/`・`SS-ACCEPT-gpu-night/`（`verify-SS-ACCEPT-12.txt`・`-13.txt`）。計測2: `SS-ACCEPT-gpu-run2/`・`SS-ACCEPT-gpu-run2-night/`（`verify-SS-ACCEPT-16.txt`・`-17.txt`）。単位は ms。

| 視点 | 計測1 中央値 | 計測1 p95 | 計測1 最大 | 計測2 中央値 | 計測2 p95 | 計測2 最大 |
|---|---|---|---|---|---|---|
| 既定 朝10° | 5.54 | 6.74 | 8.50 | 9.04 | 12.49 | 16.87 |
| 既定 昼45° | 5.77 | 7.17 | 7.79 | 6.66 | 11.08 | 13.42 |
| 既定 夕3° | 5.68 | 6.59 | 8.46 | 5.43 | 8.61 | 12.62 |
| 近接 朝10° | 4.36 | 5.45 | 6.43 | 3.83 | 7.45 | 10.75 |
| 近接 昼45° | 6.10 | 10.95 | 13.58 | 3.24 | 5.28 | 7.41 |
| 近接 夕3° | 5.57 | 9.26 | 11.51 | 3.23 | 5.02 | 7.32 |
| 低角度 朝10° | 5.13 | 9.31 | 11.76 | 3.42 | 7.20 | 13.06 |
| 低角度 昼45° | 5.67 | 10.06 | 15.03 | 2.96 | 5.59 | 8.03 |
| 低角度 夕3° | 4.07 | 5.43 | 6.91 | 2.97 | 6.13 | 9.12 |
| 既定 夜 | 9.48 | 15.01 | 18.16 | 4.09 | 7.44 | 9.38 |
| 近接 夜 | 4.21 | 6.50 | 8.03 | 3.23 | 4.71 | 6.07 |
| 低角度 夜 | 4.05 | 5.59 | 7.24 | 3.14 | 6.51 | 9.36 |

2回の計測とも、全12視点で95パーセンタイルは16.6 ms以下だが、1フレームの値が予算を超えるフレームがある（撮影スクリプトと同じ窓で数えた。`.harness/runs/20261002-185756/count_over_budget.py`、出力 `verify-SS-ACCEPT-23.txt`）。

| 計測 | 視点 | 予算を超えたフレーム | 値（ms） |
|---|---|---|---|
| 計測1 | 既定 夜 | 540中6 | 17.60・18.16・16.75・17.37・17.18・17.01 |
| 計測2 | 既定 朝10° | 540中1 | 16.87 |

ほかの22の計測（視点 × 条件 × 計測）は予算を超えたフレームが0。完了条件の「16.6 ms以下」を1フレームごとの値で読むと、既定の夜と既定の朝は予算を超える。その場合に要るパスごとの内訳は、今のトレースに GPU のパスごとの時間が出ない（RenderGraph のパスごとのタイムスタンプは取っているが、`--trace-file` へ書かない）ため取れていない。

### 未取得の項目と理由

次の3つは、測る仕組みがエンジン側（`Library/Core`）に要り、このタスクの変更の範囲（`Docs/RenderingValidation`・`Scripts/CaptureStartupScene.ps1`）の外になる。

1. Release の構成での GPU 時間。Release は `NORVES_ENABLE_STATS=0`（`Library/Core/CMakeLists.txt`）で、GPU のタイムスタンプとトレースが無効。
2. 加速構造の構築・更新を含む区間。`RenderingCoordinator.cpp` は `RayTracingSceneSubsystem::BuildAccelerationStructures` を記録した後に `FrameGPU` のタイムスタンプを始める。
3. 予算を超えたフレームのパスごとの内訳。パスごとの GPU 時間をトレースへ書く経路が無い。

外部のプロファイラも試したが使えなかった。Nsight Systems 2021.1.3（`nsys profile -t vulkan --vulkan-gpu-workload=true`）は Release の Game を起動できたが、記録できたイベントは5件で Vulkan の GPU の作業は取れず、Game は終了時にアクセス違反（`-1073741819`）で落ちた（`.harness/runs/startup-capture/SS-ACCEPT-nsys-try/`）。FrameView SDK 同梱の PresentMon 1.8（フレームごとの GPU の作業時間を出せる）と WPR の GPU の記録は ETW のカーネルの記録に管理者の権限が要り、この環境では起動できなかった。

### 既知の限界

- 計測ごとの揺れが大きい。同じ視点の中央値が計測ごとに2倍近く変わる（既定の夜 9.48 → 4.09 ms）。GPU のクロックの状態や、同じ GPU で動くほかの画面の描画の影響と見られる。95パーセンタイルの最大は既定の夜の15.01 msで、予算までの余裕は1.6 ms。
- 1フレームだけの最大値は予算を超えることがある（計測1の既定の夜 18.16 ms、計測2の既定の朝 16.87 ms）。
- 加速構造の更新は区間に含まない（上記）。
- Release の構成そのものの GPU 時間は測れていない（統計が無効。上記）。

## 撮影の注意

撮影中にゲームのウィンドウがマウスのクリックを受けると、`PickingController` が物体を選んで水色の AABB の線を画面に描く（同時に Game.log へ `Debug line vertex capacity exceeded` の警告が出る）。この反復の最初の撮影では近接・夕と低角度・昼の2枚に写ったため、同じコマンドで撮り直した。上の比較・数値は撮り直した撮影（`verify-SS-ACCEPT-11.txt`）のもので、全12枚で水色の線の画素が無いことを確かめた（夜の低角度の Game.log にも同じ警告が1件あるが、画面に線は写っていない）。
