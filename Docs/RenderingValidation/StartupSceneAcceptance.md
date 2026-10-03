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

判定日: 2026-10-03。FIX-ASYNC-TEXTURE-MIPS（`451be2a`）・SS-CSM-MEGA-CASTERS（`695e9d2`・`8123494`）・SS-MEGA-SPHERE（`d173855`）・SS-MEGA-SPHERE-DISPLACE（`b632987`）の後の起動画面を、変更前（`b347eb7`）と並べて受入れる。検証の出力は `.harness/runs/20261003-114333/verify-SS-ACCEPT-DETAIL-<n>.txt`。撮影は `.harness/runs/startup-capture/` の下に置く。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| Release の構成のビルド | `cmake --build build --config Release --target Game -- /m:1` | EXIT_CODE=0（`-1`） |
| RelWithDebInfo の構成のビルド | `cmake --build build --config RelWithDebInfo --target Game -- /m:1` | EXIT_CODE=0（`-2`） |
| 朝・昼・夕 × 既定・近接・低角度の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-DETAIL -SunElevations 10,45,3` | 9枚、result=pass（`-3`） |
| 夜の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-DETAIL-night -Night` | 3枚、result=pass（`-4`） |
| 2回目の撮影（起動時間の確認） | 上の2つを出力先 `SS-ACCEPT-DETAIL-run2`・`SS-ACCEPT-DETAIL-run2-night` で | 12枚、result=pass（`-13`・`-14`）。画面の平均の1回目との差は1.0以内 |
| Release の構成での撮影 | 上の2つに `-Configuration Release`（出力先 `SS-ACCEPT-DETAIL-release`・`-release-night`） | 12枚、result=pass（`-9`・`-10`）。平均の差は下の「既知の限界」 |
| SS-LOOK-BALANCE の数値の範囲 | `check_look_balance.py`（`.harness/runs/20261002-185756/`） | 全項目 PASS（`-11`） |
| GPU のフレーム時間（1280×720） | `-Configuration RelWithDebInfo -GpuTimingFrames 600`（昼夕朝・夜、2回） | 12視点 × 2回の計12960フレームのうち2フレームが16.6 msを超えた（最大 17.799 ms）。内訳と軽くする案は下 |
| 起動から撮影まで | Game.log の最初の行から `capture_png saved` まで | 同じコードの撮影で変更前との差は −1.1〜+1.5 s。今回の受入れの回は機械全体が約2割遅かった（下） |

撮影は TAA・RTGI 有効（`indirect_lighting=rtgi`）の既定のまま、verify の撮影は Debug、GPU の時間は RelWithDebInfo で撮った。GPU は NVIDIA GeForce RTX 4080。

### 変更前と変更後の比較

変更前は `SS-ACCEPT/`・`SS-ACCEPT-night/`（上の節の受入れの撮影。撮影したコード `229556d` は `b347eb7` と同じツリー）。撮影スクリプトの視点は変更前後で同じ（既定は起動時の既定のカメラ `0,20,10`、近接 `0,5,2.5`、低角度 `20,-8,6`）。

視点ごとの比較画像（上の段が変更前、下の段が変更後。左から 朝10°・昼45°・夕3°・夜）:

- 既定: `.harness/runs/startup-capture/SS-ACCEPT-DETAIL-compare/compare-default.png`
- 近接: `.harness/runs/startup-capture/SS-ACCEPT-DETAIL-compare/compare-near.png`
- 低角度: `.harness/runs/startup-capture/SS-ACCEPT-DETAIL-compare/compare-low.png`
- 大きな球の拡大（等倍の切り出しを2倍。左が変更前）: `zoom-near-sun45.png`・`zoom-near-sun3.png`・`zoom-low-sun45.png`・`zoom-low-sun3.png`、変更後の球の正面の中央の4倍: `zoom-near-sun45-center-x4.png`

作り方は `.harness/runs/20261003-114333/make_compare.py`。

| 視点 | 朝10° 前→後 | 昼45° 前→後 | 夕3° 前→後 | 夜 前→後 |
|---|---|---|---|---|
| 既定 | 114.3 → 113.2 | 117.6 → 119.6 | 83.6 → 83.7 | 52.9 → 52.8 |
| 近接 | 115.4 → 117.6 | 118.0 → 120.8 | 87.8 → 88.6 | 51.5 → 52.8 |
| 低角度 | 116.5 → 117.1 | 120.8 → 122.7 | 93.0 → 93.2 | 63.6 → 63.9 |

（画面全体の平均輝度、0〜255。`metrics.json` の `mean_luminance`。変更前の値は `SS-ACCEPT/` の現在の撮影のもので、上の節の表は同じ版の別の回の撮影）

見た目の違い（撮影を開いて確かめた）:

- 太陽の影: 変更前は小屋と岩に太陽の影が無く、接地部のコンタクトシャドウだけだった。変更後は朝・昼・夕の既定視点で小屋の影が地面へ落ち（朝・昼は小屋の左、夕は長く伸びる）、岩の影も地面に落ちる。CSM の各カスケードへ描く MegaGeometry は3つで、計 135,427 三角形（RelWithDebInfo の Game.log の `csm_mega_triangles`。岩 66,122・小屋 4,281 と、大きな球の LOD2 の三角形数 65,024 の和に一致する）。
- 大きな球: 変更前は UV 1周に4Kを1回貼った石が横に伸びた模様で、輪郭は滑らか。変更後は石の大きさが地面の石畳とほぼ同じになり、近接・低角度で輪郭が石の凹凸で波打ち、目地の窪みに陰が入る。拡大画像で目地は球の正面の中央でも途切れずにつながり、割れ・穴は見えない（中央に目地が縦にそろう列が1本見えるが、模様の並びで形の段差ではない）。夜の近接でも点光源の側の石の凹凸に陰影が出る。
- 石畳のミップ: Game.log で石畳の5枚（基本色・法線・ラフネス・AO・高さ）がすべて `mip_levels=13`。地面の遠景のざらつきの減少は FIX-ASYNC-TEXTURE-MIPS の記録のとおりで、画面の平均の変更前後の差は全視点で −1.1〜+2.8。

### SS-LOOK-BALANCE の数値の範囲

`check_look_balance.py` を `SS-ACCEPT-DETAIL`・`SS-ACCEPT-DETAIL-night` に掛けた（`verify-SS-ACCEPT-DETAIL-11.txt`、RESULT pass）。

| 条件 | 範囲 | 測定値 | 判定 |
|---|---|---|---|
| (1) 画面の平均（朝） | 95〜135 | 113.2・117.6・117.1 | PASS |
| (1) 画面の平均（昼） | 105〜140 | 119.6・120.8・122.7 | PASS |
| (1) 画面の平均（夕） | 75〜120 | 83.7・88.6・93.2 | PASS |
| (1) 画面の平均（夜） | 25〜80 | 52.8・52.8・63.9 | PASS |
| (2) 白飛び画素率（全12枚） | 1%未満 | すべて0 | PASS |
| (2) 黒つぶれ画素率（夜以外の9枚） | 2%未満 | すべて0 | PASS |
| (3) 昼の影の中の比（表示のリニア輝度） | 15〜40% | 既定 20.56%・近接 17.19% | PASS |
| (4) 昼の画面上端（y<60）の B − R | 40以上 | 既定 42.7・近接 67.2・低角度 67.3 | PASS |
| (5) 夕の日向の地面の平均色 | R > G > B | 既定 (95.8, 68.3, 63.0)・近接 (112.0, 65.1, 38.0)・低角度 (88.1, 61.7, 56.4) | PASS |
| (6) 夜の画面上端の輝度 | 40未満 | 3.0・4.5・10.8 | PASS |
| (6) 夜の光だまりと周り | 周りの3倍超 | 光だまり 0.5251、左奥 0.0025・左下 0.0223 | PASS |
| (6) 夜の球・岩の影 | 照らされた隣の半分未満 | 球 0.0042 / 0.0374、岩 0.0933 / 0.6358 | PASS |

判定スクリプトの影・光だまりの領域は画素の座標で固定している。大きな球を変えた後も領域が影と日向に当たっていることを、領域を描いた画像で確かめた（`SS-ACCEPT-DETAIL-compare/regions-default-sun45.png`・`regions-near-sun45.png`・`regions-default-night.png`。昼の影の領域は大きな球の影の中、日向の領域は影の外の地面、夜の球・岩の影の領域は影の帯の中、光だまりは光源の手前の地面）。参考の R・G・B がすべて250以上の画素の割合は全12枚で0。

### GPU のフレーム時間

測り方は上の節の「測り方」と同じ（RelWithDebInfo、`FrameGPU` は加速構造の更新を含む区間、600描画フレームのうち最後の540フレーム、1フレームでも16.6 msを超えたら予算超え）。計測1: `SS-ACCEPT-DETAIL-gpu/`・`SS-ACCEPT-DETAIL-gpu-night/`（この回の標準出力は保存に失敗したため、各ディレクトリの `metrics.json` とトレースが根拠）。計測2: `SS-ACCEPT-DETAIL-gpu2/`・`SS-ACCEPT-DETAIL-gpu2-night/`（`-7`・`-8`）。表は `verify-SS-ACCEPT-DETAIL-16.txt`。単位は ms。

| 視点 | 計測1 中央値 | 計測1 p95 | 計測1 最大 | 計測2 中央値 | 計測2 p95 | 計測2 最大 | 予算超え（1 / 2） | MegaGeometryPass 中央値（1 / 2） |
|---|---|---|---|---|---|---|---|---|
| 既定 朝10° | 3.756 | 6.820 | 11.552 | 3.679 | 6.418 | 8.284 | 0 / 0 | 0.623 / 0.578 |
| 既定 昼45° | 5.222 | 9.604 | 16.183 | 3.884 | 7.531 | 9.801 | 0 / 0 | 1.105 / 0.547 |
| 既定 夕3° | 4.133 | 6.959 | 10.486 | 3.890 | 6.797 | 9.809 | 0 / 0 | 0.621 / 0.572 |
| 近接 朝10° | 8.896 | 12.117 | 16.214 | 8.500 | 11.636 | 16.713 | 0 / 1 | 5.674 / 5.513 |
| 近接 昼45° | 9.280 | 12.855 | 15.170 | 9.035 | 12.294 | 17.799 | 0 / 1 | 5.967 / 5.686 |
| 近接 夕3° | 8.490 | 11.981 | 14.584 | 9.156 | 12.305 | 15.342 | 0 / 0 | 5.442 / 5.767 |
| 低角度 朝10° | 4.380 | 7.365 | 13.234 | 4.192 | 8.432 | 11.560 | 0 / 0 | 1.584 / 1.270 |
| 低角度 昼45° | 4.291 | 6.949 | 9.939 | 4.683 | 7.803 | 11.107 | 0 / 0 | 1.466 / 1.441 |
| 低角度 夕3° | 4.657 | 8.048 | 11.827 | 4.198 | 8.080 | 12.258 | 0 / 0 | 1.346 / 1.307 |
| 既定 夜 | 4.184 | 7.737 | 10.146 | 3.480 | 7.087 | 9.606 | 0 / 0 | 0.947 / 0.586 |
| 近接 夜 | 9.033 | 12.970 | 15.512 | 8.195 | 11.171 | 13.640 | 0 / 0 | 6.163 / 5.566 |
| 低角度 夜 | 4.609 | 8.369 | 13.866 | 3.989 | 6.919 | 8.936 | 0 / 0 | 1.409 / 1.533 |

ほかのパスの中央値は全視点・2回で ShadowMapPass 0.75〜1.50・LightingPass 0.45〜0.93・GBufferPass 0.15〜0.19・VolumetricsPass 0.01〜0.24・SSAOPass 0.08〜0.11・SSRPass 0.03〜0.08・BloomPass 0.07・TemporalAAPass 0.05・AccelerationStructureBuild 0.015〜0.017。

変更前（上の節の計測3・4）との違い:

- 近接視点の中央値が 約3.1 → 8.2〜9.3 ms に増えた。増分のほぼすべてが MegaGeometryPass（0.34 → 5.4〜6.2 ms）で、画面の大半を覆う大きな球を LOD0（1,046,528 三角形。全5段の合計は 1,392,768）で描くため。
- 既定・低角度の中央値は 3.2〜3.8 → 3.5〜5.2 ms。ShadowMapPass が 約0.47 → 0.75〜1.5 ms（CSM へ MegaGeometry を描く分）、MegaGeometryPass が低角度で 約0.56 → 1.3〜1.6 ms に増えた。
- GBufferPass は既定視点で 1.66 → 0.19 ms に減った。SS-CSM-MEGA-CASTERS の計測（`SS-CSM-MEGA-CASTERS-gpu/`）で既に 0.186 ms で、その前の描画の変更はミップだけなので、ミップの無い4Kの石畳の読み出しが重かったと見る（推定。パスの中の内訳は取っていない）。
- p95 と最大は変更前（p95 3.8〜4.2・最大 4.0〜4.9 ms）より全体に大きく、中央値から離れた跳ねが増えた。
- SS-MEGA-SPHERE-DISPLACE のコミットの直前に同じ LOD0（1024×512）で測った `SS-MEGA-SPHERE-DISPLACE-gpu/` では、近接の中央値が 6.10/6.04 ms（昼/夕）・MegaGeometryPass 3.96/3.91 ms・最大 12.364 msで、今回の2回の近接（中央値 8.5〜9.3 ms・MegaGeometryPass 5.4〜5.8 ms）はその約1.4倍だった。起動時間と同じく今回の受入れの時点の機械の状態による揺れと見られる（推定）が、予算の判定は遅い側の今回の値で行った。

予算を超えた2フレームの内訳（`metrics.json` の `over_budget_frames`。括弧は窓の中央値からの増分）:

| フレーム | FrameGPU | MegaGeometryPass | ShadowMapPass | LightingPass | ほか |
|---|---|---|---|---|---|
| 近接 朝10°（計測2） | 16.713 | 10.824（+5.310） | 4.231（+3.450） | 0.721（+0.003） | 区間に入らない残り 0.246 |
| 近接 昼45°（計測2） | 17.799 | 7.470（+1.784） | 6.503（+5.689） | 3.109（+2.384） | 区間に入らない残り 0.027 |

どちらも540フレーム中の1フレームだけで、ほかのパスは中央値のまま、MegaGeometryPass・ShadowMapPass（・LightingPass）が同じフレームで一度に数倍になる跳ねだった。同じ視点の別の計測では超えていない（計測1の最大は 16.214・15.170 ms）。

軽くする案（効く見込みの大きい順）:

1. 大きな球の LOD の選び方を画面上の誤差（三角形が1画素より小さくなる段は使わない）で決める。近接視点の球の三角形の多くは1画素未満で、LOD1（261,120 三角形）でも見た目の差は小さいと見込む。MegaGeometryPass の中央値 5.5 ms の大半を占める。
2. クラスタの裏向きの判定（法線の円錐による裏面のクラスタの除外）を足す。球の奥の半分のクラスタを描かずに済む。
3. ShadowMapPass の跳ねには、遠いカスケードへ描く MegaGeometry をより粗い LOD にする（今は岩・小屋が LOD0 のまま4カスケードすべてへ描く。`csm_mega_lod=0`）。

### 起動から撮影まで

Game.log の最初の行から `capture_png saved` の行まで（秒、Debug）。変更に関係しない区間（起動から環境の IBL の作成 `derived IBL resources created` まで）も並べる。出力は `verify-SS-ACCEPT-DETAIL-12.txt`・`-15.txt`（求め方は `startup_phase.py`）。

| 撮影 | コード | 起動から撮影まで（昼夕朝 / 夜） | 関係しない区間（起動から IBL まで） |
|---|---|---|---|
| 変更前 `SS-ACCEPT` | `b347eb7` と同じ | 36.38〜37.13 / 34.00〜36.23 | 18.28〜18.94 |
| 変更後 `SS-MEGA-SPHERE-DISPLACE`（昼・夕と夜） | `b632987` | 36.46〜37.37 / 35.15〜35.79 | 18.58〜19.71 |
| 変更後 `SS-ACCEPT-DETAIL` | `b632987` | 44.25〜55.38 / 40.54〜40.90 | 21.93〜25.52 |
| 変更後 `SS-ACCEPT-DETAIL-run2` | `b632987` | 43.02〜46.85 / 40.80〜43.59 | 21.75〜23.14 |

同じコード（`b632987` のコミットの後、14:41）の `SS-MEGA-SPHERE-DISPLACE` の撮影では、視点ごとの変更前との差は −1.08〜+1.53 s（昼/夕で 既定 +0.67/+0.53、近接 −0.34/+0.07、低角度 +0.71/+0.40、夜は 既定 −1.08・近接 +1.27・低角度 +1.53）。今回の受入れの2回では起動から撮影までが 40〜55 s に伸びたが、変更と関係しない「起動から IBL まで」も同じ割合（約1.2倍）で遅く、IBL の後から撮影までの区間も変更前の約1.2倍（昼夕朝 18.0〜18.8 → 21.3〜24.1 s）だった。どの区間も一様に遅い形で、撮影した時点の機械の負荷によると見る（推定。CPU の負荷は記録していない）。大きな球の高さマップの読み込みとクラスタ・LOD の構築は別のスレッドの仕事で 2.47 s（Debug、`stage=big_sphere_cluster_lod_build`）、テクスチャがそろった時点で終わっていて待ちは無い（`big_sphere_build_wait wait_ms=0.0`）。

### 既知の限界

- 近接視点で540フレーム中1フレームが16.6 msを超えることがある（上記）。中央値は 8.2〜9.3 ms で、予算の半分を超える。軽くする案は上の3つ。
- Release の撮影の画面の平均が Debug より最大8.1低い（昼の既定 Release 111.5・Debug 119.6）。画像の差は画面全体で一様（`SS-ACCEPT-DETAIL-compare/debug-vs-release-default-sun45.png`）で、物の欠けではなく露出の差。Release の既定・昼を撮るフレームを変えると 60フレーム目 112.3・240フレーム目 122.8・600フレーム目 124.2（`SS-ACCEPT-DETAIL-release-settle/`、`-17`）と上がり、RelWithDebInfo の長い計測の後の撮影（124.25）にそろう。撮影は落ち着いてから60描画フレーム目で、Release はフレームが速いぶん自動露出が収束しきる前に撮る（変更前の受入れでは差が2.0以内だった）。12枚とも SS-LOOK-BALANCE の (1) の範囲の中（朝・昼 107.0〜118.0、夕 80.2〜91.3、夜 51.1〜63.0）。
- 起動から撮影までの時間は機械の負荷で揺れる（上記）。
