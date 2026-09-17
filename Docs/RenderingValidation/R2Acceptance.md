# R2 空・太陽・CSM 受入れ記録

## 受入れ範囲

R2-P7 は、R2 の空・太陽・CSM について、朝・昼・夕の数値と画像を固定する。R1 の `Indoor.png` / `Outdoor.png` と `VisualThresholds.tsv` は入力・既存基準として保持し、R2 の画像と閾値は別名・別ディレクトリへ保存する。

R2 の受入れデータは次の3層で構成する。

- CPU の float 参照 readback: 天頂、太陽近傍、太陽ディスクの有限性と EV15/EV14 安全域。
- CSM 契約: 4分割の境界、ブレンド区間の欠落・二重化、サブテクセル移動時の影エッジ変化率。
- RGBA8 golden: `R2SkyMorning.png`、`R2SkyNoon.png`、`R2SkyEvening.png`。

## R2 専用シナリオ

`RenderingHdrSceneCaptureTest` の `--r2-scenario=sky-time-sweep` は、`outdoor` と `back-buffer` を明示した R2 専用の決定的な受入れシナリオである。CPU 参照モデルから実測した float 値と CSM サンプルを出力し、R1 のキャプチャや baseline を変更しない。

```text
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep
```

昼の参照値は天頂 `(463.567047, 944.840820, 1808.168945)`、太陽近傍 `(3283.929199, 3988.980225, 5179.038574)` で、設計アンカーに対して相対誤差5%以内である。朝・昼・夕の太陽ディスク値は有限であり、CSM は4カスケード、欠落0、二重化0、最大影差分0.005000、サブテクセル影エッジ変化率0.000000（256サンプル）を記録する。

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

R2-P7 の実行結果は `.harness/runs/20260917-161136/verify-R2-P7-1.txt` から `verify-R2-P7-4.txt` に保存する。

```text
cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrSceneCaptureTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest)$"
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract
build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep
```

スクリプトの契約自己検査は R2 用ファイルの存在・スキーマと R1 baseline の不変性を確認する。

```text
pwsh -NoProfile -File Scripts/CalibrateRenderingVisualThresholds.ps1 -SelfTestR2Contract
pwsh -NoProfile -File Scripts/UpdateRenderingGoldenBaselines.ps1 -SelfTestR2Contract
```
