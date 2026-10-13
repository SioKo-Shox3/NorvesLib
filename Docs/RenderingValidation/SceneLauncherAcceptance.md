# シーンランチャーの受入れ

ランチャーに載せるシーンの受入れ記録。シーンごとに節を分ける。判定日・ブランチは節ごとに書く。
証拠は `.harness/runs/20261009-233224/verify-<タスク>-*.txt`（コマンドの出力）と `.harness/runs/scene-capture/<タスク>/`（`metrics.json`・PNG・各視点の `*.Game.log`・`*.trace.csv`。git 管理外）。

## バナナ（`mega-bananas`）の性能: 1 万・2 万・3 万本

判定日: 2026-10-10。ブランチ `feature/scene-launcher`。タスク SCN-BANANAS-PERF。

### 測り方

- RelWithDebInfo の Game、1920×1080、視点は `overview`（既定）と `close`。本数は `--banana-count=<n>`。
- GPU 時間は `Scripts/CaptureScene.ps1 -GpuTimingFrames 300`（実時間の計測。トレースの `FrameGPU` の窓 240 フレームの中央値。段5・段6の `VirtualizationAcceptance.md` と同じ測り方）。決定的な撮影ではない。
- 描いたクラスタ数は `MEGA_OCCLUSION` の `pass1 + pass2_drawn` の最後の行、バッファの大きさは `MEGA_CAPACITY`・`VRAM_LEDGER`・`VRAM_POOLS` の行から読む（`mega_stats`）。いずれも開発ビルドのログで、Release には出さない。
- 予算は 16.6 ms。

### 結果（変更後）

`MegaGeometry` は `VisibilityRasterPass` と同じ値（ID のラスタの描画を含む。足さない）。バッファは MegaGeometry の可視ビット・間接描画・描画情報・BVH の待ち行列・ソフトウェアラスタの合計（MiB）。「捨てた」は上限を超えて描かれなかった可視クラスタ（`draw_dropped`。0 が合格）。CPU はフレームの CPU 時間（`CPUFrameMs` の中央値）。

| 本数 | 視点 | フレームの GPU（中央値 / p95） | MegaGeometry の GPU | 描いたクラスタ数 | 捨てた | バッファ（MiB） | CPU フレーム |
|---|---|---|---|---|---|---|---|
| 1 万 | overview | 16.99 / 17.24 ms | 11.33 ms | 697,938 | 0 | 464.3 | 72 ms |
| 1 万 | close | 16.45 / 17.12 ms | 6.41 ms | 340,997 | 0 | 464.3 | 72 ms |
| 2 万 | overview | 29.57 / 54.20 ms | 22.22 ms | 1,197,320 | 0 | 672.6 | 266 ms |
| 2 万 | close | 32.01 / 49.39 ms | 17.83 ms | 740,485 | 0 | 672.6 | 319 ms |
| 3 万 | overview | 41.03 / 76.89 ms | 32.57 ms | 1,633,061 | 0 | 752.9 | 579 ms |
| 3 万 | close | 55.86 / 64.73 ms | 37.78 ms | 1,125,447 | 0 | 752.9 | 581 ms |

（証拠: `verify-SCN-BANANAS-PERF-vm2-<本数>-<視点>.txt`。3 万本の close は同じ条件の再計測が 57.05・56.05 ms（`-rerun1-`・`-rerun2-`）で、MegaGeometry は 38.7・37.9 ms。）

バッファの内訳（MiB）。本数に比例するのは可視ビット（4 B × 全インスタンスのクラスタ数 = 1 本 2,105 クラスタ × 4 B）と BVH の待ち行列の段（1 万本 128、2 万・3 万本 256）。ほかは本数によらない。

| 本数 | 可視ビット | 間接描画 | 描画情報 | BVH の待ち行列 | ソフトウェアラスタ | 合計 |
|---|---|---|---|---|---|---|
| 1 万 | 80.3 | 160 | 64 | 128 | 32 | 464.3 |
| 2 万 | 160.6 | 160 | 64 | 256 | 32 | 672.6 |
| 3 万 | 240.9 | 160 | 64 | 256 | 32 | 752.9 |

ソフトウェアラスタの 32 MB は上の表の `sw_raster_bytes` と `VRAM_LEDGER sw_raster_list` が同じ `m_SwRasterBuffer` を測った値で、合計に含まれる（別の確保ではない）。上のほかに本数によらない固定の確保: `VisibilityRasterPass` の記録バッファ 1024 MB（`VRAM_LEDGER visbuffer_records`）、ID のバッファ 15.8 MB、VSM の物理ページ 320 MB（`vsm_pool_mb`）、影のマップの確保 623 MB（`shadow_map_pool_mb`）。

ジオメトリのプール（`VRAM_LEDGER geometry_pool` の最終行。読み込み完了後の値）。容量は 1 ブロック 256 MiB で、使用量は本数によらず視点ごとに同じ値（バナナのメッシュは 1 つで共有）。

| 本数 | 視点 | 容量（MiB） | 使用量（MiB） | 確保数 |
|---|---|---|---|---|
| 1 万 | overview | 256.0 | 3.9 | 30 |
| 1 万 | close | 256.0 | 5.7 | 46 |
| 2 万 | overview | 256.0 | 3.9 | 30 |
| 2 万 | close | 256.0 | 5.7 | 46 |
| 3 万 | overview | 256.0 | 3.9 | 30 |
| 3 万 | close | 256.0 | 5.7 | 46 |

（証拠: `scene-capture/SCN-BANANAS-PERF/vm2-<本数>-<視点>/<視点>.Game.log` の最後の `VRAM_LEDGER geometry_pool`。）

### 1 万本 overview の内訳（17.0 ms）

予算 16.6 ms を 0.39 ms（2%）超える。パスごとの中央値:

| パス | ms | 備考 |
|---|---|---|
| `VisibilityRasterPass`（= `MegaGeometry`） | 11.33 | 下の 4 つとレコードの合算 |
| └ カリング 1 回目（`MegaGeometryCull1`） | 2.19 | 2,105 万クラスタの BVH の判定 |
| └ ハードウェアラスタ（`MegaGeometryDraw1`） | 3.05 | 比較的大きい三角形 |
| └ ソフトウェアラスタ（`VisRasterSw1`） | 3.48 | 小さい三角形 |
| └ カリング 2 回目（`MegaGeometryCull2`） | 2.03 | 前フレームの HZB との再判定 |
| `LightingPass` | 3.28 | |
| `VirtualShadowMapPass`（`VsmMark` 0.37） | 0.58 | |
| `SSAOPass` | 0.38 | |
| 残り（材質のタイル分類・解決・SSR・Bloom ほか） | 約 1.4 | 各 0.3 以下 |

### この計測で直したもの

前の状態（`base`。変更前の既定）は 1 万本 overview で 31.4 ms、2 万本・3 万本では描画コマンドの上限（1,048,576）を超えて可視クラスタを捨てていた（2 万 overview 148,824・3 万 overview 584,766・3 万 close 77,554。画面の穴になる）。次の 3 点を直した。

| 直したもの | 内容 | 効果 |
|---|---|---|
| 太陽の影を VSM が持つ間は CSM に MegaGeometry を描かない（`ShadowMapPass`・`SceneView`） | MegaGeometry の影の描画は（インスタンス数 × 根の段の三角形数 × カスケード数）で増え、1 万本で CSM だけで 17.7〜18.4 ms（フレームの約 5〜6 割）。CSM を読む物（半透明・ボリュームのフォグ・検証表示・`--shadow-probe`）があるときは従来どおり描く。ログ `CSM_MEGA_SKIPPED` | 1 万 overview 31.4 → 17.3 ms、2 万 62.7 → 30.3 ms、3 万 66.9 → 42.0 ms（`fix1`） |
| `vsm_mark.comp` が立っているビットへ atomicOr しない | 近接の視点は画面のほぼ全画素が同じ数語へ印を付けるので、無条件の atomicOr は同じ語への直列化になる。`coherent` の読みで立っていれば省く（立つビットは同じ） | 1 万 close の `VsmMark` 11.9 → 4.1 ms、フレーム 20.3 → 16.4 ms |
| `MegaGeometryPass::Settings::MaxDrawCount` 1,048,576 → 4,194,304 | 3 万本 overview の可視クラスタ 約 163 万、確保は材質の全クラスタ数との小さい方（上限時で約 235 MB。小さなシーンでは増えない） | 捨てた数が全視点で 0 |

見た目が変わっていないことの確認（CSM を省く前後。`--shadow-probe` は CSM に描き続ける経路）: 起動画面（1280×720、3 視点）の平均輝度は省いた状態と測定の経路で同じ（124.143・123.973・126.023。`eq-startup-on`・`eq-startup-probe`）、バナナ 1 万本は overview 122.798・close 122.494 で同じ（黒つぶれの画素率の差は 1e-6 以下。`eq-bananas-on`・`eq-bananas-probe`）。

### 撮影の確認

1 万・2 万・3 万本の overview と close、3 万本の low（決定的な撮影）を開いた（`scene-capture/SCN-BANANAS-PERF/vm2-<本数>-<視点>/*.png`・`low.png`）。本数が増えても、床の透け・バナナの欠け・影の欠けは無い。overview は本数に応じて升目が奥まで密になり、3 つの山は同じ位置に出る。close は手前の山と 1 本ごとの影が 1 万・2 万・3 万本で同じ。3 万本の low は床すれすれの先まで影が出て、遠景の山もそろっている。

`Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs banana-count=30000`（3 視点・決定的な撮影）は result=pass、約 261 秒（`verify-SCN-BANANAS-PERF-3.txt`。平均輝度 default 122.996・close 122.977・low 108.179、検査のエラー 0）。

### 既知の限界

- **1 万本 overview の GPU 時間が予算を 2% 超える**（16.99 ms。close は 16.45 ms で予算の内、p95 は両方 17 ms 台）。残りの 0.4 ms は MegaGeometry のカリングとラスタの合算（約 11.3 ms）で、これ以上は段の見直しが要る。LOD の誤差の閾値 `NORVES_MEGA_LOD_ERROR_PX` を上げれば届く（1.5 画素で 16.47 ms・描いたクラスタ 605,014、2 画素で 15.95 ms・536,857。`-lod1.5-`・`-lod2-`）が、1 画素は起動画面の撮り比べで決めた値（`MegaGeometryPass.h` の `LODBias`）で、全シーンの LOD の選び方が変わるので既定は変えていない。
- **2 万本・3 万本は予算の 2〜3 倍**（2 万 29.6〜32.0 ms、3 万 41.0〜55.9 ms）。GPU 時間は描いたクラスタ数にほぼ比例して増え、`MegaGeometry` がフレームの約 55〜80% を占める。
- **CPU のフレームが GPU より長く、本数に対して線形より速く伸びる**（1 万 72 ms・2 万 266〜319 ms・3 万 579 ms）。実際のフレーム率はこれで決まる（1 万本で約 14 fps）。トレースの `RenderGraph.Execute` が 1 万 26 ms → 3 万 313 ms、ゲームスレッドは 73 → 579 ms で、`MegaGeometryPass.RecordFrameCommand` は 2.1 → 7.1 ms。原因はトレースのスコープからは絞れておらず、未調査（`TASKS.md` に別件として追記した）。
- **3 万本の GPU 時間は計測のたびに揺れる**: フレームが CPU 律速で GPU が空く時間が長く、同じ視点でも中央値が最大 20% 動く（3 万 close の `fix1` は 45.7 ms、最新は 56〜57 ms で、パスごとに全体が遅い）。p95 は 65〜77 ms、最大は 142 ms。2 万・3 万本の値は目安。
- 他の GPU・ドライバでは測っていない。本数の上限は `--banana-count` で 65,536（可視ビットは 1 クラスタ 1 bit に詰めておらず、約 550 MB まで増える）。
- CSM に MegaGeometry を描かない間は、CSM のテクスチャ（`shadow_map_pool_mb` の分）に MegaGeometry の影は入らない。CSM を読む物（半透明・ボリュームのフォグ・検証表示）が出た時点で描き戻す。

## mega-stress の 2 フレームの重ねの GPU 時間（クロックとの切り分け）

判定日: 2026-10-14。ブランチ `feature/scene-launcher`。タスク SCN-ECS-DIAG-GPU-FIF2。証拠は `.harness/runs/20261014-005646/verify-SCN-ECS-DIAG-GPU-FIF2-*.txt`・`check-SCN-ECS-DIAG-GPU-FIF2-clock.txt` と `.harness/runs/scene-capture/SCN-ECS-DIAG-GPU-FIF2/`。

### 測り方

- 同じ RelWithDebInfo の Game で、mega-stress 5 万個 close・1920×1080・`-GpuTimingFrames 300` を `--frames-in-flight=1`・2・1・2 の順に続けて撮った。
- 各撮影の間 `nvidia-smi`（SM・メモリのクロック、電力、温度、throttle の理由）を 500 ms ごとに記録し、計測の窓（300 フレーム）の時刻の範囲に入った標本だけを集計した。GPU は RTX 4080（ドライバ 610.88、電力の上限 320 W）。
- パスの値は `gpu_pass_ms` の median（ms）。`MegaGeometry` は Cull・Draw などを含む合計。

### 結果

| 撮影（順に撮った） | 重ね | GPU のフレーム median | p95 | RenderThread の 1 フレーム median | `MegaGeometry` | `MegaGeometryCull1` | `MegaGeometryCull2` | `MegaGeometryDraw1` | `VisRasterSw1` | `LightingPass` | 窓の間の SM のクロック median（範囲） | 電力 median | 温度 max | throttle の理由 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `fif1-a` | 1 | 33.580 | 35.045 | 38.687 | 29.584 | 10.577 | 8.896 | 4.255 | 4.085 | 1.965 | 2760 MHz（2760〜2760、24 標本） | 145.9 W | 54 ℃ | 0x0（無し）24/24 |
| `fif2-a` | 2 | 33.328 | 35.262 | 33.288 | 29.797 | 11.249 | 9.418 | 4.291 | 3.872 | 1.683 | 2745 MHz（2745〜2760、20 標本） | 156.2 W | 56 ℃ | 0x0（無し）20/20 |
| `fif1-b` | 1 | 33.620 | 34.909 | 38.624 | 29.742 | 10.613 | 9.539 | 4.385 | 4.160 | 1.886 | 2745 MHz（2745〜2745、23 標本） | 148.1 W | 56 ℃ | 0x0（無し）23/23 |
| `fif2-b` | 2 | 34.395 | 36.404 | 34.362 | 30.577 | 11.009 | 9.883 | 4.500 | 4.192 | 1.719 | 2745 MHz（2745〜2745、22 標本） | 157.4 W | 58 ℃ | 0x0（無し）22/22 |

### 判断

- 2 フレームの重ねによる GPU の費用は、フレームの median で +0.26 ms（+0.8%）で、同じ設定の 2 本の間の揺れ（1.07 ms）より小さい。クロックは 4 本とも同じで、throttle は起きていない。
- SCN-ECS-MEASURE-FIF の撮影で出た 2 フレーム 40.0 ms・1 フレーム 34.5 ms の差は、同じコードで再現しない。全パスが一様に遅い形から、その撮影の時点の GPU の状態によるものと見て、2 フレームの重ねの費用とは扱わない。VRAM は溢れていない。
- RenderThread の 1 フレームは 2 フレームの重ねで 38.6 → 33.3〜34.4 ms に縮む。

### 既知の限界

- 2 フレームでは `MegaGeometryCull1`＋`MegaGeometryCull2` が +0.7〜+1.2 ms（+4〜6%）、`LightingPass` が -0.2〜-0.3 ms。フレームの合計では揺れの中に収まる。
- 40.0 ms の撮影のときのクロックは記録が無い。他の GPU・ドライバでは測っていない。60 fps の判定はこの節では行わない（SCN-MEGA-ACCEPT-1M）。
