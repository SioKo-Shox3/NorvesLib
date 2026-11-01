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

## 100万本と混在の負荷の受入れ（60 fps の判定）

判定日: 2026-10-14。ブランチ `feature/scene-launcher`。タスク SCN-MEGA-ACCEPT-1M。証拠は `.harness/runs/20261014-122019/verify-SCN-MEGA-ACCEPT-1M-*.txt`・`aux-SCN-MEGA-ACCEPT-1M-capture-*.txt` と `.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/<負荷>/`。

**判定: 不合格。** 4 つの負荷のどれも GPU・ゲームスレッド（gt）・描画スレッド（rt）の median と p95 の 16.6 ms 以下を満たさない。既定の本数（`kDefaultBananaCount` = 1 万）は変えない。直す先は下の「直す先」の 8 項目（`TASKS.md` の `SCN-MEGA-FIX-*`・`SCN-FIX-SKY-BANDING`）。

### 測り方

- RelWithDebInfo の Game、1920×1080、RTX 4080（ドライバ 610.88）。`Scripts/CaptureScene.ps1 -GpuTimingFrames <n>`（実時間の計測。SceneReady の後のウォームアップ 60 フレームを捨てた窓）。各負荷の撮影は 1 回。
- 4 つの負荷の引数は SCN-MEGA-MIXED-LOAD で決めたもの。

| 負荷 | シーン・視点 | `-GameArgs` | 窓のフレーム数 | 窓に入れたもの |
|---|---|---|---|---|
| 静止 | mega-bananas・horizon | `banana-count=1000000` | 300 | — |
| カメラパス | mega-bananas・horizon | `banana-count=1000000,camera-path-play,camera-path-time=0` | 1,300 | パス（周期 20 秒）を約 5.6 周（窓は約 112 秒。`CAMERA_PATH_TIME` が 0〜20 秒を 5 回以上巡る） |
| 1% 移動 | mega-stress・overview | `banana-count=1000000,stress-entities=50000,stress-moving-percent=1` | 300 | — |
| ストリーミング | mega-stress・overview | 1% 移動 ＋ `stress-stream-cell=64,stress-batch-cells` | 600（`-TimingReadyMarginFrames 2000`） | 区画の解放から再公開（1 巡り 8 秒）を数巡（窓は約 29 秒。`MEGA_STRESS_STREAM_CYCLE result=pass`、Entity の数 50,000 が毎巡一致） |

- CPU の内訳は、トレースの区間を窓のフレームごとに足し、フレームについての median・p95 を取った（同じ名前の区間は足す。親と子は別の行で、足し合わせない）。

### 結果

| 負荷 | GPU median / p95 | gt median / p95 | rt median / p95 | 判定 |
|---|---|---|---|---|
| 静止 | 15.11 / 15.63 | 36.85 / 39.96 | 36.91 / 39.96 | GPU は合格、gt・rt は不合格 |
| カメラパス | 85.99 / 116.56 | 85.75 / 118.81 | 85.83 / 118.91 | 不合格 |
| 1% 移動 | 32.61 / 34.43 | 51.01 / 56.83 | 51.03 / 56.94 | 不合格 |
| ストリーミング | 31.08 / 36.15 | 48.84 / 147.89 | 48.87 / 147.93 | 不合格 |

（単位は ms。証拠: `verify-SCN-MEGA-ACCEPT-1M-1.txt`〜`-4.txt` の `CHECK_CAPTURE_METRICS result=fail`。）

どの負荷でも gt は描画スレッドを待つ時間（`GT.WaitRenderThread`）がほぼ全部で、ゲームスレッドの作業（`GameThread.Simulation`・ECS の系）は静止 0.4 ms・1% 移動 1.3 ms（ECS の系の合計 3.5 ms）・ストリーミング 0.7 ms。gt は rt で決まる。

#### 描画スレッドの内訳（median / p95、ms）

| 区間 | 静止 | カメラパス | 1% 移動 | ストリーミング |
|---|---|---|---|---|
| `RT.BuildAccelerationStructures` | 22.05 / 23.14 | 29.86 / 32.30 | 31.58 / 34.23 | 30.11 / 32.51 |
| `MegaGeometryPass.UpdateResidentSceneTables` | 7.11 / 7.72 | 7.12 / 7.93 | 7.63 / 8.49 | 7.60 / 23.84 |
| `MegaGeometryPass.PrepareSceneTables`（上を含む） | 7.15 / 7.80 | 7.16 / 8.04 | 7.79 / 8.73 | 7.67 / 105.63 |
| `RG.ParallelRecord`（記録） | 3.72 / 4.88 | 4.27 / 5.74 | 4.26 / 6.01 | 4.15 / 5.73 |
| `RT.ReleasePacket` | 1.60 / 1.74 | 1.60 / 1.75 | 1.61 / 1.81 | 1.62 / 1.90 |
| `RT.WaitFrameFence`（GPU の待ち） | 0.00 / 0.01 | 38.22 / 69.81 | 0.00 / 0.01 | 0.00 / 0.01 |

- `RT.BuildAccelerationStructures` は、静止で TLAS の更新が 0 回（`RT_SCENE update_skipped=120 updated=0`）なのに 22 ms 掛かる。中身は TLAS の入力の選択で、毎フレーム 100 万席すべての距離を判定し（`candidates=1000002`）、半径 120 m の内の約 44 万から近い 65,536 件を選び直し、パケットへ 65,534 件を足している（`RT_SCENE ... out_of_range=558922 over_limit=375544`）。量に比例する処理を毎フレームやり直している（単価ではなく量）。
- `UpdateResidentSceneTables` は、変わった行が 0（`MEGA_SCENE_UPLOAD rows=0`・`MEGA_TABLES delta_rows=0`）でも 7.1 ms。100 万の描く順の行を毎フレームたどっている。
- ストリーミングの p95 の山は、区画の散布の出し入れ（約 1 秒に 1 回）ごとに `MegaGeometryPass.BuildSceneTables` が約 105 万行を全部作り直すため（`MEGA_TABLES full=6`/120 フレーム、窓の 600 フレームのうち 29 フレームが 20 ms 超え、最大 515 ms）。
- 開発ビルドの表の検算 `MegaGeometryPass.CheckSceneTables` は 120 フレームに 1 回 87〜99 ms 掛かる（median・p95 には入らず、最大の山として出る）。Release には入らない。

#### GPU の内訳（median / p95、ms）

| パス | 静止 | カメラパス | 1% 移動 | ストリーミング |
|---|---|---|---|---|
| `VisibilityRasterPass`（= `MegaGeometry`） | 11.84 / 12.22 | 12.22 / 14.72 | 16.07 / 16.63 | 15.99 / 17.37 |
| └ `VisRasterSw1`（ソフトウェアラスタ） | 5.09 / 6.80 | 5.92 / 7.80 | 9.08 / 9.68 | 8.80 / 10.06 |
| └ `MegaGeometryCull1`（BVH の判定） | 3.04 / 4.73 | 3.66 / 5.39 | 4.89 / 6.57 | 4.91 / 6.99 |
| └ `MegaGeometryDraw1`（ハードウェアラスタ） | 1.35 / 2.53 | 1.43 / 2.65 | 0.68 / 1.29 | 0.57 / 1.06 |
| `LightingPass`（RTGI を含む） | 1.66 / 2.96 | 70.53 / 100.39 | 11.43 / 13.29 | 11.62 / 14.29 |
| `VirtualShadowMapPass` | 0.66 / 1.36 | 1.16 / 2.38 | 2.62 / 4.32 | 2.50 / 4.37 |
| `AccelerationStructureBuild` | 0 | 0.93 / 1.46 | 0.84 / 1.38 | 0.80 / 1.01 |
| 描いたクラスタ（最後の行） | 1,679,084 | 1,663,450 | 2,639,192 | 2,655,129 |

- カメラパスの GPU 86 ms は `LightingPass` 70.5 ms が占める。選ぶ件数が上限 65,536 で一定なので、カメラが動いて顔ぶれが替わっても TLAS は毎フレーム Update（refit）だけで、作り直しは 0 回（`RT_SCENE updated=120 built=0`）。refit は同じ要素の小さな動きを前提とするので、席の中身が別の場所のインスタンスに替わると木の箱が広がり、RTGI のレイの判定が重くなる、と見る。静止では TLAS を読み込みのときに作ったまま使い、1.7 ms。
- 1% 移動の GPU 32.6 ms は、`MegaGeometry` 16.1 ms と `LightingPass` 11.4 ms（ここでも `updated=120 built=0`）。描いたクラスタは 264 万でバナナだけの静止の 1.6 倍、ソフトウェアラスタの単価は 100 万クラスタあたり 3.4 ms（静止 3.0）でほぼ同じなので、ラスタは量が多い側。VSM は動く Entity のページの描き直しで 2.6 ms。
- 静止の GPU は 15.1 / 15.6 ms で予算の内（`MegaGeometry` 11.8 ms が 78%）。

#### 溢れ・メモリ・Entity の数

- 溢れ: 静止・カメラパスはログの全行で `draw_dropped`・`bvh_overflow`・`visible_list_overflow`・影の `overflow`・二段目の `*_overflow` が 0。1% 移動とストリーミングは可視クラスタの一覧（容量 4,194,304）を `visible_list_peak` 4,820,881〜4,821,043 が超え、1% 移動は SceneReady の前（最大 `draw_dropped=330250`）、ストリーミングは窓の中でも `draw_dropped=626568` が出る（**不合格**）。BVH と影の列は 4 負荷とも 0。
- メモリ（GPU Scene まわり）: 見張り前の各構造の最大値の和（`VRAM_LEDGER`・`MEGA_CAPACITY`・`MEGA_SCENE_UPLOAD`）はバナナ 100 万本だけで 2,007 MB（インスタンス・影・席の表 320、BVH の列 145、間接描画 320、描画情報 128、可視の一覧と印 80、ソフトウェアラスタ 32、ID の記録 512、影の BVH の列 240、送りの窓 229）。今回の `mega-bananas` は既定の 1 万本で、見張りの device の山は 1,294.5 MiB（frame 1）。混在（100 万本 + 5 万個、1% 移動・ストリーミング、可視の一覧 6,291,456）の実測は `SCN-MEGA-FIX-DRAWLIST-GROWTH`・`-long`。通常の device の山は 2,662.6 MiB（frame 712）、長い撮影（`exit-after-rendered-frames=1200`）も 2,662.6 MiB（frame 738）で、どちらも山の live は 2,662.6 MiB、回収待ち（pending）は 0.0 MiB（該当バッファなし）。山の内訳では `MegaGeometry_InstanceTable` 432.000 MiB・`MegaGeometry_ShadowInstanceTable` 144.000 MiB・`MegaGeometry_RangeBounds` 1.500 MiB を live に含み、最後の確保はそれぞれ 288・96・1 MiB。落ち着いた後の device の live は両方 2,470.1 MiB（通常の最後 frame 824、長い撮影の最後 frame 1974・SceneReady 後 1,200 フレーム）。4 列の最終サイズは両方とも `MegaGeometry_IndirectDraw` 654.058 MiB、`MegaGeometry_DrawInfo` 261.623 MiB、`VisBuffer_DrawRecords` 384.000 MiB、`MegaGeometry_SwRaster` 48.000 MiB。各列は 2 世代・拡張 1 回で、通常の 29 行・長い撮影の 67 行すべてで `draw_dropped=0`・`visible_list_overflow=0`。山は上限 3,072 MiB 以下、最後の live は直す前の 2,658.4 MiB 以下。表の容量 2,097,152 に対し使う行は約 105 万。
- Entity の数: 散布 1 つ（Object 1 つと軽い行）で本数によらない。ストリーミングの負荷は 1M + 5 万個で `world_objects=828`（5 万個は区画の散布 325 個にまとめる）、巡るたびに `baseline=828` と一致。
- バナナの本数: mega-stress の混在では `MEGA_STRESS_BANANAS count=999999 requested=1000000` で 1 本足りず、1% 移動・ストリーミングの撮影は本数の違いでも fail（Entity の足元を空けるマスクのある敷き詰めで不足を埋めていない）。

### 撮影の確認

4 つの PNG を開いた（`scene-capture/SCN-MEGA-ACCEPT-1M/<負荷>/*.png`）。静止の horizon は地平線までバナナが描かれ、4 つの塚が出て、穴・影の欠けは無い。1% 移動・ストリーミングの overview は家・塔・岩・バナナの区画が奥まで描かれ、近くの床に影が落ち、穴は無い。カメラパスの horizon は穴は無いが、近くのバナナの周りに粒の荒い暗い輪が出る（TLAS の refit で RTGI が乱れていると見て、SCN-MEGA-FIX-TLAS-REBUILD の完了の条件に入れた）。mega-stress の overview とカメラパスの horizon の空に、波打つ横縞の段がはっきり見える（mega-stress の空の列の値は SCN-MEGA-MIXED-LOAD の撮影と同じで前からある。静止の horizon の空は滑らか）。

### 直す先

| 項目 | 対象 | 量か単価か |
|---|---|---|
| `SCN-MEGA-FIX-RT-SELECT` | CPU: `RT.BuildAccelerationStructures` 22〜32 ms（TLAS の入力の選択） | 量（毎フレーム 100 万席を判定） |
| `SCN-MEGA-FIX-TLAS-REBUILD` | GPU: `LightingPass` 70.5 ms（カメラパス）・11.4 ms（混在） | 単価（refit で崩れた木） |
| `SCN-MEGA-FIX-SCENE-TABLES-STEADY` | CPU: `UpdateResidentSceneTables` 7.1 ms（変わらないフレーム） | 量（毎フレーム 100 万行をたどる） |
| `SCN-MEGA-FIX-MIXED-DEFICIT` | 混在のバナナの本数 999,999 | — |
| `SCN-MEGA-FIX-VISIBLE-LIST` | 混在の可視クラスタの一覧の溢れ | — |
| `SCN-MEGA-FIX-STREAM-TABLES-DELTA` | CPU: 区画の出し入れでの表の全部の作り直し（p95 106 ms・最大 515 ms） | 量（出し入れごとに 105 万行） |
| `SCN-MEGA-FIX-STRESS-GPU` | GPU: 1% 移動の `MegaGeometry` 16.1 ms（TLAS の直しの後の残り） | 量（描くクラスタ 264 万） |
| `SCN-FIX-SKY-BANDING` | 見た目: 空の段 | — |

構造・件数・ログで規模の依存を説明できたので、30 万本の比較の負荷は足していない。

### 既知の限界

- 各負荷 1 回の撮影。他の GPU・ドライバでは測っていない。
- 回収待ちの表の大きさはログに無いので、メモリの最大は見積もり。
- `RT.BuildAccelerationStructures` の中は区間に分けていない（選択と TLAS の記録の比は、静止で TLAS の更新が 0 回であることから選択と判断した）。

### 10-14 からの静止の GPU の増え（2026-11-01）

`mega-bananas` の `horizon`、`--banana-count=1000000`、カメラパスなし、1920×1080、RelWithDebInfo の 300 フレームを比較する。10-14 の基点はメインの作業ツリーの reflog で撮影開始時刻 13:08 の直前にあった `49211974`（13:02）。10-14 の記録はメインの `.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/static/`、10-29 は `.harness/runs/scene-capture/SCN-MEGA-FIX-TLAS-REBUILD/static-default-1m/`、11-01 は `.harness/runs/scene-capture/SCN-MEGA-FIX-TLAS-REBUILD-P2/static/` にある。11-01 のビルドと撮影、必要な 3 パスを指定した `CheckCaptureMetrics.py` はともに終了コード 0、`MEGA_BANANAS_PLACED count=1000000` を確認した。この検査の合格は記録と標本の確認であり、60 fps の合格を意味しない。

パスの median / p95（ms。`metrics.json` の `gpu_timing.pass_ms`。GPU フレームは `gpu_frame_ms_*`）:

| パス | 10-14 | 10-29 | 11-01 |
|---|---|---|---|
| `LightingPass` | 1.657 / 2.961 | 2.666 / 2.686 | 2.857 / 2.883 |
| `MegaGeometryCull1` | 3.043 / 4.730 | 5.319 / 6.186 | 5.496 / 16.022 |
| └ `MegaBvhC1Leaf` | 2.560 / 4.248 | 4.829 / 5.695 | 4.995 / 15.482 |
| `VisRasterSw1` | 5.091 / 6.795 | 5.090 / 5.094 | 5.089 / 5.347 |
| `MegaGeometryDraw1` | 1.351 / 2.527 | 1.358 / 1.362 | 1.358 / 1.363 |
| `VirtualShadowMapPass` | 0.658 / 1.362 | 0.659 / 0.669 | 0.677 / 0.687 |
| `VisibilityResolvePass` | 0.148 / 0.165 | 0.219 / 0.221 | 0.214 / 0.217 |
| GPU フレーム | 15.107 / 15.634 | 16.784 / 17.852 | 17.265 / 27.616 |

`MegaGeometryCull1` は `MegaBvhC1Leaf` を含む。中央値の増分は 10-14 → 11-01 でそれぞれ +2.453 ms・+2.435 ms。`MegaBvhC1S0`〜`S3` の中央値は順に 0.105 / 0.081 / 0.054 / 0.052 ms → 0.102 / 0.082 / 0.054 / 0.053 ms で、葉以外の段はほぼ同じ。11-01 の GPU フレームは中央値・p95 とも 16.6 ms を超える。

窓の各 `GPU` 行から求めた分布（ms、各 300 件。百分位は昇順の `floor((n-1)×p)` 番目）:

| パス | 撮影 | p05 | p25 | median | p75 | p95 |
|---|---|---:|---:|---:|---:|---:|
| `LightingPass` | 10-14 | 1.632 | 1.647 | 1.657 | 1.670 | 2.961 |
|  | 10-29 | 2.650 | 2.660 | 2.666 | 2.672 | 2.686 |
|  | 11-01 | 2.841 | 2.850 | 2.857 | 2.865 | 2.883 |
| `MegaGeometryCull1` | 10-14 | 3.018 | 3.040 | 3.043 | 3.425 | 4.730 |
|  | 10-29 | 5.302 | 5.311 | 5.319 | 5.337 | 6.186 |
|  | 11-01 | 5.304 | 5.348 | 5.496 | 6.689 | 16.022 |
| `MegaBvhC1Leaf` | 10-14 | 2.554 | 2.557 | 2.560 | 2.856 | 4.248 |
|  | 10-29 | 4.817 | 4.822 | 4.829 | 4.848 | 5.695 |
|  | 11-01 | 4.820 | 4.859 | 4.995 | 6.176 | 15.482 |

窓は順に 46483〜46782・58574〜58873・57986〜58285。11-01 の葉は 300 フレーム中 33 フレームが 10 ms 以上、最大 35.115 ms で、中央値の増加とは別に大きな山がある。

件数とドライバ（ログの最後の該当行。`RT_SCENE` の 120 フレーム行も併記）:

| 項目 | 10-14 | 10-29 | 11-01 |
|---|---|---|---|
| `GPU_DRIVER` | RTX 4080 / 610.88.0.0 | 同左 | 同左 |
| 配置本数 | 1,000,000 | 1,000,000 | 1,000,000 |
| `MEGA_TWOPASS` 席 / 回した席 / 節 / クラスタ / 2 回目の席の判定 / 通過 | 292,080 / 99,464 / 4,062 / 793 / 99,464 / 12 | 同左 | 同左 |
| `MEGA_OCCLUSION` 1 回目 / 2 回目の判定 / 描画 | 1,679,044 / 831 / 40 | 同左 | 同左 |
| `MEGA_CAPACITY` 節の山 / 葉の山 / 可視一覧の山 | 903,745 / 903,737 / 1,679,084 | 同左 | 同左 |
| 描画コマンドの容量 / 可視一覧の容量 | 16,777,216 / 4,194,304 | 12,582,912 / 4,194,304 | 14,680,064 / 6,291,456 |
| `SW_RASTER_BIN` 1 回目 / HW | 1,577,558 / 101,486 | 同左 | 同左 |
| `RT_SCENE` 最後の skip / update / build / frames、instance | 52 / 0 / 0 / 52、65,536 | 11 / 0 / 0 / 11、65,536 | 30 / 0 / 0 / 30、65,536 |
| `RT_SCENE` 最後の 120 フレームの skip / update / build | 120 / 0 / 0 | 同左 | 同左 |
| `RT_SCENE` 範囲外 / 上限で除外 | 558,922 / 375,544 | 561,370 / 373,096 | 561,370 / 373,096 |
| `RTGI_HISTORY` age_cap / static_frames | 256 / 264 | 同左 | 同左 |
| `MEGA_TABLES` rows / instances / spare | 1,000,000 / 1,000,000 / 記録なし | 1,000,000 / 1,000,000 / 記録なし | 1,062,500 / 1,000,000 / 62,500 |
| `GEOMETRY_PAGES` resident / request_overflow / capacity / peak | 31 / 記録なし / 記録なし / 記録なし | 31 / 0 / 32,768 / 36 | 同左 |
| `RT_BLAS` built / blas / triangles / instances | 記録なし | 1 / 3 / 6,972 / 65,536 | 同左 |
| `RTGI_HITS` dispatches / rays / hits / mega_hits | 記録なし | 645 / 663,973,965 / 226,779,335 / 226,779,335 | 645 / 663,973,965 / 226,779,397 / 226,779,397 |

`RTGI_HITS` は起動からの累積で、数えているのは 1 回目の拡散光線だけ。10-29 と 11-01 の同じ 645 dispatch の比較では 1 dispatch あたり 1,029,417 本、当たりの差は累積で 62 件。10-14 にはこの統計がなく、2 回目の拡散光線・鏡面光線も数えていないため、全光線数が 10-14 と同じとは言えない。`RT_BLAS` は初期構築の行で、静止中の TLAS 更新の行とは別。`MEGA_TABLES` の増えた行は予備で、実インスタンスは 100 万のまま。

判定: 葉の列の山・描画クラスタ・席の数が同じなのに、葉の段だけが大きく増えているため、主な候補は葉のクラスタ 1 件あたりのコードの仕事。照明は同じ TLAS 件数でもコードに追加された光線と、1 件あたりの判定・統計の両方が候補になる。ドライバの表示バージョンは 3 回とも同じで、`VisRasterSw1`・`MegaGeometryDraw1`・VSM と BVH の節の中央値はほぼ変わらず、全パスに一様なクロック低下だけでは説明しにくい。ただし過去のクロック・ドライバ内部の状態は記録されておらず、クロックの差やドライバ実装の影響を完全には除けない。RT の範囲外と上限で除外した件数は変わっており、選ばれた 65,536 件の顔ぶれや BVH の形が同一である証拠もない。

差分からの候補（時間の寄与は個別には未計測）:

- `MegaBvhC1Leaf` と親の `MegaGeometryCull1`: `4a3a43d6`（10-21）は `Common/MegaGeometryCull.glsl` の `ProcessCluster` で、子に任せて描かないクラスタからも `RequestPage(instance.bvhInfo.w, usedChildPage)` を呼ぶ。ページが有効なら `RequestPage` が共有の `requestStamp` に `atomicExchange` し、最初の要求を列へ積む。葉の列の数を変えずに LOD で落ちたクラスタにも分岐と書き込みを足すため、葉だけの増加に最も合う候補。遮蔽で落ちた自分のページの記録も同変更で増えるが、それは 2 回目などの経路であり、1 回目の葉の増加にそのまま充てない。`e5e06e0d`（10-24）は `cluster_bvh_cull.comp` の `BindClusterInstance` で `instance.mainLodFloor` を読み、LOD の比較を `EffectiveLodBias()` の `max` に替える。こちらも 1 クラスタあたりの読み込み・計算の候補で、LOD の選択にも影響し得る。どちらの寄与が何 ms かは分からない。
- `LightingPass`: `baaf64b4`（10-25）は `RTGI/DiffuseIndirect.comp` の `ShadeDiffuseIndirect` に鏡面のある画素の `TraceSpecularRay` を足し、鏡面の当たりでは条件によりさらに `TraceSecondBounce` を呼ぶ。`lighting.frag` にも鏡面履歴の `texelFetch` を足す。拡散光線の件数が同じ場合でも、条件を満たす画素では鏡面光線と画素あたりの処理が増えるため、静止の増加に最も合う候補。`cff879b2`（10-20）は同シェーダーの `main` に 1 本目の拡散光線の共有 `atomicAdd`・barrier とグループ集約の `atomicAdd` を足すほか、`RenderingCoordinator.cpp` の `MakeRayTracingSelectBounds` を proxy の境界からメッシュ全体の境界へ替える。前者は開発ビルドの各光線・グループの単価、後者は TLAS の選択・たどりの候補。`9e922381`（10-25）は `TraceSpecularRay` と `lighting.frag` に広いローブの重みと正規化を足し、光線数を変えずに計算を増やす。`9a2fee63`（10-30）は鏡面が自分の面に当たった場合に判定と `TraceDiffuseRay` の引き直しを足し、10-29 → 11-01 の差の候補になる。これらの条件の成立件数と寄与は個別には分からない。
- `VisibilityResolvePass`: `30ec8456`（10-14）は `Common/VisibilityResolve.glsl` に地形材質の判定と条件付きの層の標本処理を足し、`8ba994b3`（10-18）は材質の印を調べて GBuffer へ書く処理を足す。`e5e06e0d`（10-24）・`93fa62d4`（10-24）は AOV の幾何法線・地形の印を条件付きで足すが、この撮影は AOV なし。非地形のバナナにも共通分岐やコンパイル結果の変化はあり得るものの、0.148 → 0.214 ms の増分を特定のコミットに割り当てる根拠はない。

`SCN-MEGA-FIX-TLAS-REBUILD` の静止の `LightingPass` 3 ms の閾値は、11-01 の median 2.857 ms・p95 2.883 ms では満たすが、最大 3.507 ms で 3 ms 以上のフレームが 11 件ある。基点の 1.657 ms は鏡面光線と統計の追加前の値なので、1.7 ms への復帰を TLAS の静止維持だけで要求する根拠にはならない。3 ms の閾値を変える根拠にもせず、カメラパスの TLAS の改善と静止の光線・統計の費用を分けて判定する。

限界: 10-29 は他の GPU 撮影との重なりの可能性がある。11-01 は 5 秒ごとの記録では対象の Game 以外の Game を検出せず、描画中の 4 標本の SM クロックは 2,760 MHz だったが、常駐アプリは GPU を保持しており、短い別処理の重なりまでは否定できない。11-01 の葉の p95 の山の原因も未特定。各版は 1 回の撮影で、過去の GPU クロック・ビルド時の未コミット差分・アセットの内容の同一性は確認できていない。

## カメラパスの再生の RTGI の粒（50000 本）

判定日: 2026-11-02。RelWithDebInfo、1920×1080、RTX 4080（ドライバ 610.88）。`mega-bananas` の horizon で 5 万本を配置し、カメラパスの再生と静止を撮った。再生・静止とも `CAPTURE_SCENE result=pass`、配置数 50,000、資産の欠け 0。撮影結果は `.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M-P1-before/` にある。

### 測り方

再生と静止の各 300 フレームを実時間で測り、履歴の採否と年齢は決定的な撮影の AOV から読む。`-Aov` の撮影では `-GpuTimingFrames` を使わず、落ち着いた後の既定 60 フレームで撮る。診断環境変数を切った画像と診断画像の両方を開いて確認した。

```powershell
cmake --build build --config RelWithDebInfo --target Game -- /m:1
$root = '.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M-P1-before'
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 1800 -GameArgs 'banana-count=50000,camera-path-play,camera-path-time=0' -OutDir "$root/path"
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 1800 -GameArgs 'banana-count=50000,camera-path-time=9.53' -OutDir "$root/static"
python Scripts/CheckCaptureMetrics.py "$root/path" --views horizon --timing --min-frames 300 --require-pass LightingPass
$env:NORVES_RTGI_HISTORY_DIAG = '1'
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Aov -TimeoutSeconds 1800 -GameArgs 'banana-count=50000,camera-path-play,camera-path-time=8.53' -OutDir "$root/diag-path"
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Aov -TimeoutSeconds 1800 -GameArgs 'banana-count=50000,camera-path-time=10.08' -OutDir "$root/diag-static"
$env:NORVES_RTGI_HISTORY_DIAG = $null
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-MEGA-ACCEPT-1M-P1 -Phase before -Motion
```

実時間の再生ログは `CAMERA_PATH_TIME time=7.962 frames=540`、`time=8.782 frames=600`、撮影要求 `rendered=655`。この 2 点の再生フレーム数と描画フレーム数を同一とみなす外挿は `8.782 + (8.782 - 7.962) × (655 - 600) / 60 = 9.53` 秒で、静止の計測をこの位置で撮った。ただし、描画フレームの数にはパス再生前のフレームが含まれる。再生開始 01:49:25.898 から撮影要求 01:49:35.165 までは 9.267 秒で、600 フレーム時点の時刻差 8.782 秒はログのパス時刻と一致する。したがって再生画像の実際の位置は約 9.27 秒であり、9.53 秒の静止画像とは約 0.26 秒ずれる。

診断の再生は 8.53 秒から開始したが、撮影の待機中もパスが進む。保存した AOV のカメラ位置をパスに照合した時刻は 10.079976 秒なので、診断の静止は 10.08 秒に置いた。2 枚の AOV のカメラ位置の差は 0.000041 m、前方向の差の長さは 0.0000068 で、診断の画素比較に使える。

### 直す前の計測と見た目

| 負荷 | `LightingPass` median | `LightingPass` p95 | 標本数 |
|---|---:|---:|---:|
| 再生 | 4.181 ms | 4.339 ms | 300 |
| 静止（9.53 秒） | 3.790 ms | 3.844 ms | 300 |

`CheckCaptureMetrics.py` は `CHECK_CAPTURE_METRICS result=pass views=horizon`。パスの名前とフレーム数の検査であり、画像の一致判定ではない。上の 2 枚は視点が約 0.26 秒ずれるので、見た目の同一画素の比較には使わない。

診断の再生と静止の PNG、および `diag-compare.png` を開くと、近景のバナナの周りの床は再生の方が粒状で、静止では滑らかに見える。比較画は全景と近景を、撮影・履歴の理由・年齢の順に並べる。床候補の位置は各 `horizon.history-floor-edge.png`、元の AOV は各 `horizon.aov.rtgi_temporal_code.pfm`、集計は `diag-history-stats.json` にある。

### 履歴の診断

開発ビルドの `NORVES_RTGI_HISTORY_DIAG=1` は遮蔽診断と同時に指定された場合、履歴診断を優先する。AOV の値を `floor(a / 2)` で符号に戻す。符号 0 は履歴無効、1 は速度が非有限または画面外、2 は距離・裏返り・非有限などで有効な標本が無い、3 は法線または材質、4 は残った重みの不足、6 は無効な表面を表す。8 以上は履歴採用で、年齢の整数部は符号から 8 を引く（255 に飽和）。棄却時の年齢は 0。診断は出力の半精度 a に空の可視率と同居するため、可視率の丸めによって診断画像の照明がわずかに変わり得る。

対象の床は画像下半分・深度 5 m 未満・法線 Y が 0.9 以上で、線形アルベドの `R > 0.05`、`G > 0.7R`、`B > 0.65R` を満たす画素。バナナ候補は `R > 0.1`、`B < 0.4R`、`G > 1.4B` とし、その周囲の正方形 ±12 画素または ±2 画素にある床候補を集計した。色と法線による近似であり、物体 ID による完全な領域分割ではない。

| 領域 | 状態 | 画素数 | 画面外・速度 | 距離・裏返り等 | 法線・材質 | 重み不足 | 採用 |
|---|---|---:|---:|---:|---:|---:|---:|
| 近景の床全体 | 再生 | 319,854 | 0.0456% | 0.4549% | 0.0022% | 0.0231% | 99.4741% |
| 周囲 ±12 画素 | 再生 | 300,972 | 0.0452% | 0.4834% | 0.0023% | 0.0246% | 99.4445% |
| 縁 ±2 画素 | 再生 | 115,710 | 0.0354% | 1.2575% | 0.0060% | 0.0640% | 98.6371% |
| 近景の床全体 | 静止 | 319,876 | 0% | 0% | 0% | 0% | 100% |
| 周囲 ±12 画素 | 静止 | 300,995 | 0% | 0% | 0% | 0% | 100% |
| 縁 ±2 画素 | 静止 | 115,705 | 0% | 0% | 0% | 0% | 100% |

いずれの領域も履歴無効の分類は 0%。年齢の分布は次のとおり（割合は各領域の全画素に対する値）。

| 領域 | 状態 | 年齢 0 | 年齢 1〜7 | 年齢 8 | 年齢 51 | 年齢 52 |
|---|---|---:|---:|---:|---:|---:|
| 周囲 ±12 画素 | 再生 | 0.556% | 7.341% | 92.103% | 0% | 0% |
| 縁 ±2 画素 | 再生 | 1.363% | 11.456% | 87.181% | 0% | 0% |
| 周囲 ±12 画素 | 静止 | 0% | 0% | 0% | 96.948% | 3.052% |
| 縁 ±2 画素 | 静止 | 0% | 0% | 0% | 93.091% | 6.909% |

比較画の近景の切り出し（x=500〜1459、y=720〜1079）の縁 ±2 画素でも、再生 46,178 画素の 98.343% が採用、85.307% が年齢 8。距離・裏返り等の棄却は 1.555% で、法線・材質の棄却は 0.004%。静止の同領域 46,180 画素は全て採用され、年齢 51 が 93.259%、52 が 6.741%。理由の画でも棄却は輪郭の細い線に集まり、その外側の粒が見える床は主に採用・年齢 8 になっている。

再生中は静止判定が毎フレーム切れて年齢上限が 8 に留まり、静止では上限が伸びる。確信度が 1 のとき、履歴の年齢 8 の重みは約 0.889、年齢 50〜51 なら約 0.980〜0.981 で、再生中は新しい光線のばらつきが残りやすい。広い床の粒については、履歴を全面的に棄却することより、採用した履歴の年齢上限が低いことが主な要因と考えられる。輪郭直近では距離等の棄却と若い履歴も寄与する。上限だけを変えた比較はないため、年齢上限だけで見た目の差の全てを説明できるとは断定しない。

### 既知の限界

- 診断は同じ視点で各 1 フレームを比較したもので、時間全体の棄却頻度は測っていない。診断 AOV の年齢は整数部で、静止の年齢 51〜52 はこの撮影のフレーム数に依存する。
- AOV の入力記録は他の資産セットも走査し、元資産候補の未取得を 103 件記録した。`mega-bananas` の 4 撮影自体は必要な資産の欠け 0 で完了した。
- 初期の回帰 before は `rendering3dtest`・`mega-bananas`・`sandbox` が成功した後、`night-village` の `Terrain/ValleyOfTheGods/height.json` が読めず資産欠けとなり中断した。「直した後」の回帰では、資産の接合後に全 5 組を完了した `before-u4` を比較元にしている。

### 直した後

判定日: 2026-11-02。対象は `e9a1c71`。シーン（環境光・instance）が同じでカメラだけが動くフレーム（`temporalState.x > 1u`）では、拡散の放射照度と空の可視率の履歴年齢上限を 16 にし、履歴を棄却付きバイリニアで読む。鏡面の上限は 8 のままとする。空間フィルタには同じフレームの RTGI パラメータを binding 7 で渡し、この条件では近傍の重みを年齢で下げず 1 に保つ。年齢だけを伸ばすと、従来の `8 / min(年齢, 16)` によって空間の均しが弱まり、粒が残るためである。棄却閾値と静止判定は変えていない。静止・golden を含め、カメラだけの動きでないフレームは従来の計算を通る。

#### 同じ視点での診断と採用理由

採用版の診断の再生時刻は 10.079976081 秒、対応する静止とのカメラ位置差は 0 m。帯の静止の粒は 0.1226。以下の輝度差は PNG を線形 Rec.709 輝度（0〜1）へ戻した「再生−静止」で、縁の周囲 ±12 画素の共通の床を集計した。帯の粒の比は再生を静止で割った値である。

| 版 | 帯の粒 | 比 | 輝度差の平均 | 輝度差の絶対値平均 | 輝度差の絶対値 p95 |
|---|---:|---:|---:|---:|---:|
| 直す前（年齢 8） | 0.1646 | 1.343 | −0.000246 | 0.008351 | 0.023479 |
| Catmull-Rom・年齢 16 | 0.1715 | 1.400 | −0.000212 | 0.008293 | 0.023473 |
| バイリニア 16（空間は従来） | 0.1387 | 1.131 | −0.000605 | 0.007818 | 0.022301 |
| バイリニア 24（空間は従来） | 0.1223 | 0.998 | −0.000751 | 0.007761 | 0.022480 |
| バイリニア 32（空間は従来） | 0.1157 | 0.944 | −0.000824 | 0.007844 | 0.022888 |
| **採用：バイリニア 16＋空間の重み 1** | **0.1236** | **1.009** | **−0.000610** | **0.007603** | **0.021817** |

Catmull-Rom は負の重みで粒を強めるため採用しない。年齢 24・32 は床の粒を減らす一方、バナナ脇の暗いにじみを広げる。採用版は年齢 16 と空間の均しを組み合わせ、帯の粒を静止とほぼ同じに抑える。同じ視点の拡大画を開くと、直す前に床へ広がる粗い粒状の暈が減り、静止に近い細かさになる。薄い暗さの差は残る。

診断の値と拡大画は `.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M-P1-u8/` の `luminance-difference-u8.json`・`grain-u8/rtgi-grain.json`・`zoom-matched-u8.png` にある。直す前の診断は `SCN-MEGA-ACCEPT-1M-P1-before/`。直す前・採用版・静止の比較画は `.harness/u7-diagnostic-before-after.png` に保存した。採用版とバイリニア 16（空間は従来）の静止の別撮影との差は最大 169/255・1,794 画素で、同じ版同士の別撮影（最大 169/255・1,749 画素）と同程度だった。

#### 300 フレームの計測と粒の判定

RelWithDebInfo の Game ビルドと、再生・静止 9.27 秒・時刻を合わせた静止の 3 撮影は全て終了コード 0、`CAPTURE_SCENE result=pass`。各撮影は 1920×1080、50,000 本、資産の欠け 0、300 標本。保存先は `.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M-P1/` の `path`・`static`・`static-matched`。

再生開始 `04:20:36.966`（`CAMERA_PATH state=playing time=0.000`）から撮影要求 `04:20:45.053`（`capture_png requested`）までの差で、保存フレームのパス時刻は **T=8.087 秒**。verify の静止 9.27 秒とは 1.183 秒ずれるため、8.087 秒の静止も撮った。時刻合わせはログのミリ秒精度によるもので、実時間の組は診断 AOV によるカメラ位置の完全一致を保証しない。

| 組 | 再生の粒 | 静止の粒 | 比 | 上限 1.5 の判定 |
|---|---:|---:|---:|---|
| 直す前：再生約 9.267 秒／静止 9.53 秒（時刻ずれあり） | — | — | 2.114 | fail |
| 直した後：再生 8.087 秒／静止 9.27 秒（verify 固定） | 0.116337 | 0.088238 | 1.318 | pass |
| 直した後：再生 8.087 秒／静止 8.087 秒（時刻合わせ） | 0.116337 | 0.089003 | 1.307 | pass |

`RtgiGrain.py` は仕様 `Scripts/Terrain/ExpectSpecs/rtgi-grain-motion.json` をそのまま使用した。固定の組と、一時の置き場 `u7-verify-matched` に `path`・`static-matched` を写した組の両方で `RTGI_GRAIN result=pass`。比較画は `.harness/u7-verify-before-after.png`。`CheckCaptureMetrics.py` は `--views horizon --timing --min-frames 300 --require-pass LightingPass` で `CHECK_CAPTURE_METRICS result=pass views=horizon`。

| 再生の LightingPass | median | p95 | 標本数 |
|---|---:|---:|---:|
| 直す前 | 4.181 ms | 4.339 ms | 300 |
| 直した後 | 4.147 ms | 4.337 ms | 300 |

単発の計測では LightingPass の増加は見られない。静止 9.27 秒の撮影はフレーム時間の最大値が 19.382 ms となり `within_budget=False` だったが、撮影と指定の計測チェックは成功した。これは LightingPass 以外も含むフレーム全体の記録で、今回のチェックは最大フレーム時間の上限を検査していない。

#### 回帰と材質ギャラリー

`CaptureRegression.ps1 -Item SCN-MEGA-ACCEPT-1M-P1 -Phase after -Motion` は 370 秒、終了コード 0、`CAPTURE_REGRESSION result=pass`・`REFERENCE_CHECK result=pass mode=regression scenes=5`。比較元は直す前の `before-u4` を `.harness/runs/regression/SCN-MEGA-ACCEPT-1M-P1/before` へ写したもの。`rendering3dtest`・`mega-bananas`・`sandbox`・`night-village`・`rendering3dtest-motion` の各撮影は終了コード 0、動きの 4 フレームも全て揃った。検証レイヤーのエラーはなく、ジオメトリのページは前後とも settled だった。

前後を並べた `.harness/runs/regression/SCN-MEGA-ACCEPT-1M-P1/before-after.png` と、各組の元の解像度の比較画を開いて確認した。4 シーンの配置・照明・輪郭に崩れは見られない。動きの組は `motion-0`・`motion-8`・`motion-30`・`static-30` を確認し、物体の後ろの尾、二重輪郭、フレーム間の明るさ・粒の変化の増加は見られなかった。`motion-0` の明るさと強い粒は前後共通で、後続フレームで落ち着く。これは保存した離散フレームの比較であり、連続動画全体のちらつきの保証ではない。比較の PSNR は `mega-bananas` 64.24 dB・`night-village` 69.97 dB・`rendering3dtest` 110.69 dB・`sandbox` ∞、動きの `motion-30` は 57.96 dB、平均輝度差は +0.002/255 だった。

`material-gallery` は RelWithDebInfo の既定視点 `overview` を 1280×720、`-RequireAssets` で撮影し、終了コード 0、`CAPTURE_SCENE result=pass`、資産の欠け 0。`material-gallery/overview.png` を開き、板と球の材質、金属度と粗さの球の格子、床と影を確認した。形の欠落、二重輪郭、テクスチャの崩れは見られない。

#### Golden の照合

`cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1` は終了コード 0。続く `ctest --test-dir build -C Debug --output-on-failure -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"` は終了コード 0、`100% tests passed, 0 tests failed out of 2`（113.22 秒）。`LastTest.log` でも Indoor・Outdoor とも `mean_flip=0.000000000 max_flip=0.000000000 raw_max=0`、`mode=on resolve_logs=1 fallback_logs=0` を確認した。静止の既存基準画像に完全一致したため、golden の再承認と基準画像の更新は行っていない。

指定のビルド・撮影・粒の判定・計測チェック・回帰・golden は全て成功した。同じ視点の診断と保存画像の目視で、近景のバナナ周囲と床の粗い粒状の暈の抑制を確認した。コマンドの終了値、保存時刻 T、目視の記録は `.harness/unit-verify.txt`。全撮影は開始 04:20:08 から 50 分以内に始めており、時間制限による未撮影はない。

#### 直した後にも残る限界

- 判定の帯の弁別は弱い。同じ視点の診断では直す前も比 1.343 と上限 1.5 以下であり、数値の pass だけで暈の解消とは判断できない。採用版の比 1.009 と拡大画の目視を合わせて判断する。判定の領域と上限は変更していない。
- 実時間の保存フレームの時刻は走らせるたびに揺れる（約 7.97〜9.27 秒）。verify の静止は 9.27 秒に固定されるため、固定の組の比は視点の違いも含む。今回の T=8.087 秒の組はログで時刻を合わせた追加撮影と併記し、同一視点の診断とは区別する。
- 縁の周囲 ±12 画素の床の輝度差の平均は、直す前の −0.000246 から −0.000610 へ少し暗い側に寄る。絶対値平均と p95 は小さくなるが、薄い暗いにじみは残る。
- 回帰の動きは 4 フレームの抜き出しであり、全時間・全カメラ経路における残像やちらつきは測っていない。GPU・ドライバと撮影回数も上記の範囲に限る。
