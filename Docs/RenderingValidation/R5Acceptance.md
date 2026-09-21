# R5 ハードウェアレイトレーシング受入れ記録

## 対象と方式選定

R5では、backend-neutralなRHI APIとVulkan実装を用意し、不透明ジオメトリのハードシャドウまでを検証した。R5-P1〜P13の実装範囲と検証結果を本書に集約する。

- 加速構造の所有・更新は`GEngine`所有の`RayTracingSceneSubsystem`に集約する。GameThreadからRenderThreadへは`FramePacket`の不変スナップショットを渡し、RenderThreadはライブのWorld/Sceneを参照しない。
- R5の影はRT pipelineで実証する。R4/DDGIのprobe更新にはray queryを使う。
- RT機能は任意機能として検出する。RT非対応またはRT無効時は既存のラスタ影へ戻す。
- R5はRT影までを対象とし、GI本実装とGPU性能gateは後続へ分離する。実装バックエンドはVulkan。

本書は検証済み範囲を示す。R5全体の完了は宣言しない。R5-P5は独立評価待ちで`blocked`。R5-P13の同期失敗時資源寿命所見は`1545a41`で修正済み。

## RHI・Vulkan・描画経路の変更

| 領域 | 変更内容 |
|---|---|
| Buffer Device Address | RHIバッファで明示要求できるようにし、Vulkan側は要求時だけusage・割り当て・device address取得を有効化する。未要求または非対応時のaddressは0。 |
| capability | acceleration structure、ray query、RT pipelineを個別に照会する。Vulkan 1.2、BDA、deferred host operationsを含む依存を満たす機能だけを有効化する。 |
| shader / pipeline API | `ShaderStage`にRayGen、Miss、ClosestHit、AnyHit、Intersection、Callableを追加し、shadercへ一意に写像する。`PipelineType::RayTracing`、shader group descriptor、RT pipelineとSBTを追加する。 |
| acceleration structure / dispatch API | backend-neutralなBLAS/TLAS resource、geometry・instance・Build/Update descriptor、command listの`TraceRays`を追加する。Vulkan側でBuild/Update、SBT、dispatch上限、descriptor bindingを実装する。 |
| resource binding / synchronization | RT shader stageのdescriptor visibilityを追加する。Vulkan barrierはRT pipeline対応時にRay Tracing stageを含め、非対応デバイスには拡張stage bitを渡さない。 |
| scene snapshot | `RayTracingSceneSubsystem`がdraw snapshot由来のgeometryとtransformからBLAS/TLASを管理する。フレーム資源はGPU完了後、参照解放後に破棄する。 |
| RT影 | `RayTracingShadowPass`のvisibilityをLightingへ接続する。通常描画はラスタ影を既定とし、RT無効・生成失敗時もラスタ影を使う。 |

## GPU受入れ結果

### RT影A/Bと無効fallback

`RenderingRayTracingShadowVulkanTest`の不透明fixtureで、PCF/PCSSを無効にしたラスタハードシャドウとRT影を比較した。影・照明ROIは`shadow_luma=0`、`lit_luma=255`。ラスタ対RTは`mean_lsb=0`、`outlier_fraction=0`、`max_lsb=0`で、閾値のmean 4 LSB以下・outlier 2.5%以下を満たした。RT visibility sentinelもPASS。RT無効fallbackも同じfixtureで`max_lsb=0`だった。

### 動的TLAS更新

複数フレームcaptureで、frame 78の初期影を確認した後、frame 80のTLAS更新で旧位置が`previous_luma=255`、移動先が`moved_luma=0`、照明領域が`lit_luma=255`となった。frame 82のラスタ参照との比較は`mean_lsb=0`、`outlier_fraction=0`、`max_lsb=0`。RT無効fallbackも同じく`max_lsb=0`だった。

このfixtureは高コントラストのため、影位置と出力一致を検証する。半影や階調差は検出しない。また、影passの受入れ範囲は有効な方向光が一灯の構成。

### RHI・Vulkan契約検証

- BDA、capability、shadercの6 RT stage、backend-neutralな加速構造APIを個別テストした。
- Vulkan ray queryはhit=1 / miss=0をreadbackした。同期TLAS Build後のinstance数更新と、旧instance数を使うUpdate拒否を確認した。
- RT pipelineとSBTを生成し、無効group構成を拒否した。`TraceRays`はdispatch各軸・総呼出し上限、TLASの同一device bindingと無効descriptor拒否を確認した。
- FramePacket由来のscene snapshot、TLASの再利用・更新、GPU完了後のresource寿命を契約テストで確認した。
- `LightingParamsLayoutTest`はpre-exposureの7許可modeを個別検証し、RT visibilityとRAW250/251を除外した。

## 検証ログ

以下の記録先はリポジトリrootからの相対パス。記載したログの出力を開き、buildとCTestの終了コード、capture sentinel、readback結果を確認した。

| 範囲 | 記録先 | 検証ログ・結果 |
|---|---|---|
| P1: BDA | `.harness/runs/20260920-141707/` | `verify-R5-P1-2.txt`（BDA CTest 1/1）、`verify-R5-P1-4.txt`（対象build exit 0）。 |
| P2: 機能検出 | `.harness/runs/20260920-151245/` | `verify-R5-P2-6.txt`〜`verify-R5-P2-8.txt`（capability 1/1、RHI image layout/HDR 2/2）。 |
| P3: shader stage | `.harness/runs/20260920-151245/` | `verify-R5-P3-1.txt`〜`verify-R5-P3-2.txt`（build exit 0、stage compile 1/1）。 |
| P4: RHI契約 | `.harness/runs/20260920-151245/` | `verify-R5-P4-6.txt`〜`verify-R5-P4-7.txt`（build exit 0、API contract 1/1）。 |
| P5: BLAS / ray query | `.harness/runs/20260920-174410/` | `verify-R5-P5-1.txt`〜`verify-R5-P5-3.txt`（build exit 0、CTest 1/1、hit=1 / miss=0）。 |
| P6: TLAS Build/Update | `.harness/runs/20260920-174410/` | `verify-R5-P6-5.txt`〜`verify-R5-P6-6.txt`（CTest 1/1）、`gpu-R5-P6-query-final.txt`（移動前後のhit切替、instance数不一致のUpdate拒否）。 |
| P7: RT pipeline / SBT | `.harness/runs/20260920-174410/` | `verify-R5-P7-5.txt`〜`verify-R5-P7-6.txt`（pipeline CTest 1/1）。 |
| P8: TraceRays | `.harness/runs/20260921-r5-p8-resume/` | `verify-R5-P8-resume-build-3.txt`、`verify-R5-P8-resume-ctest-3.txt`（build exit 0、CTest 1/1、hit=1 / miss=0）。 |
| P9: FramePacket scene接続 | `.harness/runs/20260921-r5-p9-resume/` | `verify-R5-P9-final-build.txt`、`verify-R5-P9-final-ctest.txt`（build exit 0、加速構造/scene snapshot 2/2）。 |
| P10: RT影 | `.harness/runs/20260921-073235/` | `verify-R5-P10-17.txt`〜`verify-R5-P10-23.txt`。RT影CTest 1/1、Raster/RT A/B、RT無効fallbackがPASS。 |
| P11: 動的TLAS・fallback・照明契約 | `.harness/runs/20260921-073235/` | `verify-R5-P11-contract-fix-target-ctest.txt`（2/2）、`verify-R5-P11-contract-fix-dynamic-sync.txt`（移動後captureとRaster/RT/fallback A/B）、`verify-R5-P11-contract-fix-full-ctest.txt`。全CTestは235件中6件失敗・7件skip。6件の失敗はP11開始baselineの8件の部分集合で、新規失敗はない。 |
| P13: texture同期失敗時の寿命 | `.harness/runs/20260921-125655/` | `verify-R5-P13-1.txt`〜`verify-R5-P13-10.txt`（Debug build exit 0、直接GPU実行 exit 0、専用CTest 1/1）。 |

## 既知の制限と保留

- RT無効fallbackはRT対応GPU上でRTを無効化して検証した。非対応deviceそのものでは実行せず、能力契約と機能無効時の経路を検証した。
- R5-P3のstage compile検証はshaderc fixtureの6 stageを対象とする。SlangのRT stage compile結果はこの証拠に含めない。
- `VK_LAYER_VALIDATE_SYNC=1`をRenderingValidation全体へ適用した診断で、`RHIImageLayoutVulkanNoCasterSceneTest`と`RHIImageLayoutVulkanDrawThenNoCasterSceneTest`のswapchain画像に`SYNC-HAZARD-WRITE-AFTER-READ`が報告された。RT影専用テストは同期validation下で成功しており、swapchain acquire/present経路は別件として追跡する。
- R5-P5はray query build/hit/missの検証ログがあるが、独立評価が未完了のためstatusは`blocked`。
- R5-P13では`VulkanTexture::Update`の終了・送信・待機失敗時にstaging資源と転送先texture資源をGPU完了まで保持し、非device-lostのteardown待機失敗ではdevice資源を破棄しない。

## 性能gate

GPU性能は未計測。パス別GPU時間の評価は将来のCI GPU性能回帰トラックへ分離し、R5本体の受入れ条件には含めない。

## R4 / DDGIへの引継ぎ

R4のS4はDDGIを選定済み。R5のray-query実装を利用し、probe更新とGI本体はR4で実装する。R5-P13は完了したが、P5の独立評価待ちは残るためR5全体の完了とは扱わない。R4ではray-query hit/miss経路を専用テストで再確認する。
