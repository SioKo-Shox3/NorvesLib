# R2 空・太陽・CSM 受入れ記録

## 受入れ範囲

R2-P1〜P7で、空パラメータと太陽方向、空LUT、空由来IBL、4分割CSMのCPU構築、配列深度、GPUサンプリング、安全なフォールバック、および朝・昼・夕の数値/画像受入れを完了した。R1 の `Indoor.png` / `Outdoor.png` と `VisualThresholds.tsv` は既存基準として保持し、R2 の画像と閾値は別名・別ディレクトリへ保存する。

## 実装コミット

| 工程 | コミット | 内容 |
|---|---|---|
| R2-P1 | `76a0c15` | 空パラメータ、太陽方向、太陽ディスクのCPU参照契約 |
| R2-P2 | `53a58dc`（実装 `07a0cd9`） | 空LUTと太陽ディスクのLighting前段接続 |
| R2-P3 | `a235411`（実装 `5daf556`） | 動的空放射輝度からのIBL更新 |
| R2-P4 | `f51384f` | 4分割CSMの距離・行列・テクセル安定化 |
| R2-P5 | `9e015a3` | 4層配列深度とlayer別Framebuffer |
| R2-P6 | `90c99ab` | Lighting/ForwardのCSM GPUサンプリングと安全なフォールバック |
| R2-P7 | `9e1c953` + `ca9204d` | P6適用後の空・太陽・CSM再検証、カメラ前方CSM深度、実GPU BackBuffer取得 |

R2のコード完了基点は `90c99ab` とP7追補 `ca9204d` であり、R2専用の受入れ記録とRoadmap更新はこの実装および追補後の検証を根拠とする。

## P6 GPU契約の検証

`90c99ab` では次を確認した。`cameraForward`の追加とview-depth規約のCPU/GLSL整合は、P7追補修正で追加確認した。

- `LightingParamsLayoutTest`、`LightingLightBufferTest`、`ForwardPassPipelinePlacementTest`、`DirectionalShadowPassWiringContractTest` のDebugビルドは終了コード0。`SkyAtmospherePassContractTest`も同じゲートで確認した。
- 同5テストのCTestは5/5 passed。`GPULightingParams`は`cameraForward`をoffset 704、size 720へ、透明Forward UBOはoffset 784、size 800へ拡張し、CPU/GLSLのview-depth規約を一致させた。
- `forward_transparent.vert`、`forward_transparent.frag`、`lighting.frag` の `glslangValidator` は3/3終了コード0。結果は `.harness/runs/20260918-r2-p6/verify-R2-P6-shaders-final.txt` に保存した。
- `sampler2DArray` と4カスケードの行列/分割距離をLighting/Forwardで統一し、影なし・単一層・不完全公開値は1x1x4の配列型フォールバックへ戻す。

R2 の受入れデータは次の3層で構成する。

- CPU の float 参照 readback: 天頂、太陽近傍、太陽ディスクの有限性と EV15/EV14 安全域。
- CSM 契約: 4分割の境界、ブレンド区間の欠落・二重化、サブテクセル移動時の影エッジ変化率。
- RGBA8 golden: `R2SkyMorning.png`、`R2SkyNoon.png`、`R2SkyEvening.png`。

## R2 専用シナリオ

`RenderingHdrSceneCaptureTest` の `--r2-scenario=sky-time-sweep` は、`outdoor` と `back-buffer` を明示した R2 専用の決定的な受入れシナリオである。`RenderWorld::SetSkyAtmosphere`からFramePacketを経由して空の3ケースを設定し、実GPU BackBufferを取得する。R1のキャプチャやbaselineは変更しない。

```text
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep
```

昼の参照値は天頂 `(463.567047, 944.840820, 1808.168945)`、太陽近傍 `(3283.929199, 3988.980225, 5179.038574)` で、設計アンカーに対して相対誤差5%以内である。朝・昼・夕の太陽ディスク値は有限であり、CSM は4カスケード、欠落0、二重化0、最大影差分0.003438（閾値0.005000）、サブテクセル影エッジ変化率0.000000（256サンプル）を記録する。実GPU取得では3ケースすべてが `R2_GPU_CAPTURE ... passed=1`、ケース間平均RGB差分も noon 34.4486、evening 7.52983となり、最終行が `R2_SKY_TIME_SWEEP=PASS ... gpu_capture=1` になった。

## Golden と閾値

golden は次のコマンドで同じ CPU 参照モデルから再生成できる。R1 の画像は上書きしない。

```text
build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --write-r2-sky-goldens
build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-r2-artifacts
```

固定ファイルは次のとおり。

- `Test/Core/Rendering/Baselines/RenderingValidation/R2SkyMorning.png`
- `Test/Core/Rendering/Baselines/RenderingValidation/R2SkyNoon.png`
- `Test/Core/Rendering/Baselines/RenderingValidation/R2SkyEvening.png`
- `Test/Core/Rendering/Thresholds/RenderingValidation/R2SkyTimeSweep.tsv`
- `Test/Core/Rendering/Thresholds/RenderingValidation/R2CsmAcceptance.tsv`

`RenderingGoldenImageComparatorTest` は3枚を読み戻し、PNG形式、256x256 RGBA8、非黒、朝・昼・夕の相互分離、および閾値スキーマを検証する。R1 の `Indoor.png` / `Outdoor.png` はこの処理で書き換えない。

## 指定検証と証拠

R2-P7の実GPU再検証結果、ケース間平均値、RHI/Vulkan回帰結果は `.harness/runs/20260918-r2-p7/verify-R2-P7-gpu-followup.txt` に保存して読み戻し確認した。CPU参照再検証は `.harness/runs/20260918-r2-p7/verify-R2-P7-p6-rerun.txt` に保持し、P6のlayout/lighting/forward契約とシェーダーコンパイルはそれぞれのfocused検証ログに分離している。

```text
cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrSceneCaptureTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest)$"
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RHIImageLayoutVulkanDrawSceneTest|RHIImageLayoutVulkanDrawThenNoCasterSceneTest|RenderingHdrOutdoorSceneVulkanTest)$"
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest|SkyAtmospherePassContractTest)$"
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep
```

スクリプトの契約自己検査は R2 用ファイルの存在・スキーマと R1 baseline の不変性を確認する。R2のgolden/thresholdはR1の承認済みcandidateやapprovalを再利用せず、R2専用のファイルとして分離されている。

```text
pwsh -NoProfile -File Scripts/CalibrateRenderingVisualThresholds.ps1 -SelfTestR2Contract
pwsh -NoProfile -File Scripts/UpdateRenderingGoldenBaselines.ps1 -SelfTestR2Contract
```

## 空のモデルの置き換え（2026-09-30）

R2-P1の平行大気・単一散乱の近似（光学的深さ τ×e^(-τ)、地平線より下は0）を、球殻の大気のレイマーチと多重散乱の近似（Hillaire 2020）へ置き換えた。R2-P1の固定値（天頂 (463.6, 944.8, 1808.2) nits など）は古いモデルの回帰値であり、物理的な期待値ではないので引き継がない。

### モデル

- 惑星（`PlanetRadiusMeters`）と大気の上端（`+AtmosphereHeightMeters`）を同心球とし、観測点は地表から100 m（`SkyAtmosphereModel::ObserverAltitudeMeters`）。視線に沿って32点（始点の側が細かい2乗の配置）でレイマーチし、各区間を媒質一定の (1 - e^(-σt·Δt)) / σt で積分するので、長い光路は飽和して白っぽくなる。
- 媒質: Rayleigh（散乱係数 (5.802, 13.558, 33.100)×10^-6 /m、尺度高さ8 km）、Mie（散乱 1.0×10^-4 /m、吸収 1.11×10^-5 /m、尺度高さ1.2 km、HGの g=0.8）、オゾンの吸収（(0.650, 1.881, 0.085)×10^-6 /m、25 kmを中心に±15 kmのテント形）。Mieは晴天の典型的なエアロゾル（550 nmの散乱の光学的深さ約0.12、単散乱アルベド0.9）で、Hillaire 2020の既定値（光学的深さ約0.005、非常に澄んだ空）では天頂が約1100 nits、水平面の空の照度の割合が約9%となり晴天の実測より暗かったため、この値にした。地面のアルベドの既定値は0.3（Hillaire 2020の既定値、地球の平均に近い）。
- 太陽への透過率は、同じ密度の光学的深さを大気の上端まで数値積分した表（高度32×余弦128、光学的深さで補間）から引く。光路が惑星に当たれば0（地球の影）。`ComputeAtmosphereTransmittance`・`ComputeSunGroundTransmittance`・`ComputeSunGroundIlluminance`（空の太陽の方向光とPTの太陽）と透過率LUTは同じ積分を使う。表示する太陽円盤も地表の透過率で減光・着色する。
- 多重散乱は等方の Ψ_ms = L_2nd / (1 - f_ms) の表（高度8×太陽の余弦32、64方向・16点）で、地面の反射を含む。
- 地平線より下の視線は、地面（`GroundAlbedo` のランバート面を、透過した太陽と地表の空の照度で照らしたもの）にそこまでの透過率を掛け、そこまでの散乱を足した値を返す。
- 空のradiance LUT（256×128）と空由来のIBLは、仰角80×太陽からの方位差40の表（空は太陽を含む鉛直面について対称）を作って補間する。radiance LUTは視線の透過率と地面を含むので、ラスタの背景とPTの不交差は別に透過率を掛けない。LUTは空のパラメータ（太陽の向きを含む）が変わったときだけ作り直す。

### 数値の根拠（`SkyAtmosphereModelTest`、`.harness/runs/20260930-200402/verify-SS-SKY-MODEL-P1-10.txt`）

テストは、モデルの表や刻みを使わない独立な実装（等間隔800点の視線、各点で200点の直接積分による太陽への透過率、高度12×余弦64×144方向の多重散乱の格子）と比べる。

| 項目 | 値 | 基準 |
|---|---|---|
| 独立な積分との差（太陽 仰角40°・3°、天頂〜地平線より下の8方向） | 最大2.2%（40°）、2.8%（3°） | 5%以内 |
| 地表の太陽の透過率と独立な直接積分（仰角90°・40°・8°・3°） | 0.5%以内 | 1%以内 |
| sky-view の表の補間と直接の評価（仰角45°・3°、154方向） | 最大1.8% | 3%以内 |
| 天頂（太陽 仰角40°） | (1534, 2115, 3663) nits、輝度2103 nits、B > G > R | 約2000〜8000 nits |
| 水平面の空の照度 / 太陽を含む全天の照度（仰角40°） | 23.8%（空 14.8 klx相当、太陽の水平面 47.2 klx） | 10〜30% |
| 太陽と反対側の地平線（仰角1°）の R/B | 0.76（天頂は0.42） | 0.5〜1.2 |
| 地平線より下（仰角-5°・-30°・真下） | 0でなく有限（真下 (6296, 5885, 5578) nits） | 0でない |
| 太陽 仰角3°の太陽の周り（仰角5°） | (40502, 15180, 3120) nits | R > G > B、R > 2B |

### 起動画面（`.harness/runs/startup-capture/SS-SKY-MODEL-P1`・`SS-SKY-MODEL-P1-toward-sun`）

昼（45°）は空が上ほど濃い青で地平線が淡く明るく、太陽の側に白い光冠が出る。既定視点の地面の外（地平線より下の遠景）は黒くなく、霞んだ灰褐色になる。影の中は空の光で青みを帯び、黒くつぶれない。夕（3°）は太陽側の地平線が橙〜黄に光り、上空はオゾンの吸収で青紫に残る。LUTの生成は起動画面の解像度（radiance 256×128、transmittance 128×32）で1回 117〜140 ms（Debug、各撮影の `Game.log` の `Sky LUT generated`）。

空を使う検証（R2の空のgolden、PTの屋外、R7屋外の参照比較）は空の値が変わるため、新しい空で再照合する（SS-SKY-MODEL-P2）。空を使わない Indoor/Outdoor の golden は変わらない（`verify-SS-SKY-MODEL-P1-7.txt`）。

## 非対象と後続

- R2は晴天の太陽とdirectional light向けCSMまでを対象とし、夜空・天候・雲・ボリューム、点/スポット光の影、GPU性能予算は対象外とする。性能評価は将来のGPU回帰トラックへ分離する。
- R1の `Indoor.png` / `Outdoor.png`、R1のthreshold、R1のcandidate/approvalは変更・再利用していない。
- R3（ボリュメトリクス）は未着手であり、R3は新規M1の設計・計画から開始する。
