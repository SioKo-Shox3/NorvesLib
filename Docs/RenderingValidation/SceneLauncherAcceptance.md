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
- メモリ（GPU Scene まわりの構造の最大の確保。`VRAM_LEDGER`・`MEGA_CAPACITY`・`MEGA_SCENE_UPLOAD` の最大値の和）: バナナだけ 2,007 MB（インスタンス・影・席の表 320、BVH の列 145、間接描画 320、描画情報 128、可視の一覧と印 80、ソフトウェアラスタ 32、ID の記録 512、影の BVH の列 240、送りの窓 229）。混在 2,722 MB（表 641、間接描画 640、描画情報 256、ソフトウェアラスタ 128 など。送りの窓 79）。表は容量を倍に広げて写すので、1M → 2M に広げる瞬間は古い表 320 MB が回収待ちで残り、足すと約 3,042 MB で上限 3 GB の境目。回収待ちはログに独立して出ないので、この値は見積もり。表の容量 2,097,152 に対し使う行は約 105 万。
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
