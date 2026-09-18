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
| R2-P7 | `9e1c953` + follow-up | P6適用後の空・太陽・CSM再検証、カメラ前方CSM深度、実GPU BackBuffer取得 |

R2のコード完了基点は `90c99ab` とP7の追補修正であり、R2専用の受入れ記録とRoadmap更新はこの実装および追補後の検証を根拠とする。

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

## 非対象と後続

- R2は晴天の太陽とdirectional light向けCSMまでを対象とし、夜空・天候・雲・ボリューム、点/スポット光の影、GPU性能予算は対象外とする。性能評価は将来のGPU回帰トラックへ分離する。
- R1の `Indoor.png` / `Outdoor.png`、R1のthreshold、R1のcandidate/approvalは変更・再利用していない。
- R3（ボリュメトリクス）は未着手であり、R3は新規M1の設計・計画から開始する。
