# 描画検証 Golden Baseline

## 固定条件

R0 の描画回帰検証は、`indoor` と `outdoor` の2 sceneを256×256 RGBA8 PNGとして保持する。scene seedは`0x4E525630`、warmupは固定60 step、描画はsingle-threaded renderingで実行する。debug overlayはcapture対象に含めない。

比較対象はPNGの圧縮byte列やmetadataではなく、常にdecode後のRGBA8値である。現在の実scene gateは`MaximumDifferingPixelCount=0`かつ`MaximumChannelDelta=0`のstrict比較とする。将来perceptual gateへ移行しても、strict comparatorの1 px negative契約は残す。

## 通常検証

通常実行はsource baselineをread-onlyで参照し、sourceへ書き込まない。

```powershell
cmake --build build --config Debug --target RenderingGoldenImageTest RenderingGoldenImageComparatorTest
ctest --test-dir build -C Debug --output-on-failure -R '^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenVulkanSkipContractTest)$'
& .\Scripts\RunRenderingValidation.ps1 -Iterations 10 -RequireGpu
```

CTestのexit 125はskip契約の確認には使えるが、GPU acceptance passには数えない。最終確認では`RunRenderingValidation.ps1 -RequireGpu`を使い、各scene executableのexit codeを直接検査する。

## Baseline更新

更新は明示的な`-Approve`を必須とし、次のcommandだけを使う。

```powershell
& .\Scripts\UpdateRenderingGoldenBaselines.ps1 -Approve
```

scriptはsource publish直前に、内部で次の固定staging gateを必ず実行する。このvalidator単独ではsourceをpublishしない。

```powershell
& .\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --validate-fixed-staging
```

アプリケーションが書けるのはbinary root内の次の固定stagingだけである。

- `build\RenderingValidation\BaselineStaging\Indoor.png.tmp`
- `build\RenderingValidation\BaselineStaging\Outdoor.png.tmp`

更新scriptは両sceneをcaptureしてstaging validatorを通過させた後、`Test\Core\Rendering\Baselines\RenderingValidation\Indoor.png`と`Outdoor.png`だけを1 transactionとしてpublishする。既存baselineは`File.Replace`、初回baselineはsame-volumeの`File.Move`を使う。片sceneの失敗、GPU skip、validation failure、publish failureでは両sourceを元の状態へ戻す。

transaction seamは`NORVESLIB_BASELINE_TRANSACTION_TEST_FAILURE=after-indoor-staging`と`after-first-publish`で検証できる。staging validatorのnegativeは次のcommandで実行し、source baselineのhashが変化していないことを併せて確認する。

```powershell
& .\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-fixed-staging-negative=corrupt-indoor
& .\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-fixed-staging-negative=wrong-size-outdoor
```

corrupt PNGは`DecodeFailed`、decode可能な128×256 PNGは`InvalidDimensions`としてrejectされる。

## Review

baseline差分のreviewではscene、capture request ID、frame number、differing pixel数、最大channel delta、最大差の座標、mean absolute channel deltaを確認する。strict gateではdiffering pixel数と最大deltaがともに0でなければならない。

baseline更新を通常のコード変更と同じ変更単位へ混ぜない。2 PNGは必ず同時にreviewし、意図した描画変化、256×256 RGBA、更新手順の実走証拠を確認する。

## R0再ベースライン実走記録

- 日時: 2026-08-10 00:27 JST
- GPU: NVIDIA GeForce RTX 4080
- 基準commit: `ff91dfbe7b29744ca0662979162c02308def984e`
- 初回publish: indoor/outdoor両scene成功、fixed staging validation成功
- 2回目publish: indoor/outdoor両scene成功、続くread-only compare 2件成功
- Indoor SHA-256: `545E745CE9958F310A551B0D71BEAB4DAD35743C6367E0DA0724F9930E6F49E7`
- Outdoor SHA-256: `3676A470814C68841BF1C8E4BF8612802042AB936AAA77E1A9937C5EC642BA7E`
- forced skip contract: exit 125をCTest skipとして確認
- 連続検証: `-Iterations 10 -RequireGpu`で20 scene実行すべてexit 0
