# 描画検証 Golden Baseline

## 固定条件

R0 の描画回帰検証は、`indoor` と `outdoor` の2 sceneを256×256 RGBA8 PNGとして保持する。scene seedは`0x4E525630`、warmupは固定60 step、描画はsingle-threaded renderingで実行する。debug overlayはcapture対象に含めない。

比較対象はPNGの圧縮byte列やmetadataではなく、常にdecode後のRGBA8値である。現在の実scene gateは、LDR-FLIP error mapのmeanとraw `MaximumChannelDelta<=8`の独立した二段判定とする。`MaxFlipError`とその座標、raw最大値と座標は診断値として記録する。Task 2のstrict comparatorと1 px negative契約は、PNG/raw経路の独立検証として残す。

## LDR-FLIP入力契約

tone-mapped PNGからdecodeした各RGB byteを`byte/255.0f`のnormalized sRGBとし、IEC 61966-2-1 sRGB EOTFでlinear RGBへ変換してからFLIPへ渡す。alphaはFLIP入力には使わず、raw comparatorが検査する。

```text
linear = encoded <= 0.04045
    ? encoded / 12.92
    : pow((encoded + 0.055) / 1.055, 2.4)
```

【実測】pinned `FLIP.h` 2428〜2431行は、simplified LDR APIの入力を`[0,1]`、3 floats/pixel interleaved、linear RGBと明記する。【外部】同pinの公式READMEのversion表示はv1.7である。ただしdependency identityの正本はfull commit SHA `b475eb4bf394ab877c42166c9eb0a84a02cc5b14`、vendored byte hash、BSD-3-Clause licenseとする。

## FLIP dependency provenance

- full pin: `b475eb4bf394ab877c42166c9eb0a84a02cc5b14`
- 取得日: 2026-08-10
- `FLIP.h` 取得元: `https://raw.githubusercontent.com/NVlabs/flip/b475eb4bf394ab877c42166c9eb0a84a02cc5b14/src/cpp/FLIP.h`
- `FLIP.h` SHA-256: `412B118DB343A3A0D030104F17F46C1E3EDC7455D2B83A7398A42E25BEFED104`
- `LICENSE` 取得元: `https://raw.githubusercontent.com/NVlabs/flip/b475eb4bf394ab877c42166c9eb0a84a02cc5b14/LICENSE`
- `LICENSE` SHA-256: `13B955078FFB4A3215757038AB2E5FCF0CC66349D0990FAEC9ADBA8AD034E578`

## 通常検証

通常実行はsource baselineをread-onlyで参照し、sourceへ書き込まない。

```powershell
cmake --build build --config Debug --target RenderingGoldenImageTest RenderingGoldenImageComparatorTest
ctest --test-dir build -C Debug --output-on-failure -R '^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenVulkanSkipContractTest)$'
& .\Scripts\RunRenderingValidation.ps1 -Iterations 10 -RequireGpu
```

CTestのexit 125はskip契約の確認には使えるが、GPU acceptance passには数えない。最終確認では`RunRenderingValidation.ps1 -RequireGpu`を使い、各scene executableのexit codeを直接検査する。

成功した通常golden実行は`NORVESLIB_VISUAL_METRICS`をexact 1行出す。連続実行scriptはscene一致、finiteなmean/max、raw整数、threshold超過を検査し、sceneごとの成功回数、max mean、max rawを集計する。`-RequireGpu`時のexit 125は失敗とする。

## Visual threshold校正と承認

candidate生成とsource publishは別invocation、別の人間承認gateである。

```powershell
& .\Scripts\CalibrateRenderingVisualThresholds.ps1 -GenerateCandidate -Iterations 10 -RequireGpu
$candidateHash = (Get-FileHash -LiteralPath '.\build\RenderingValidation\Calibration\VisualThresholds.candidate.tsv' -Algorithm SHA256).Hash
# candidate、60行実測表、両sceneのnoise／人工差／式を人間がreviewし、decision recordへ承認原文とhashを記録する。
& .\Scripts\CalibrateRenderingVisualThresholds.ps1 -PublishApprovedCandidate -CandidateSha256 $candidateHash
```

`-GenerateCandidate`はbuild配下のcandidate／実測表だけを作り、source thresholdへ書かない。publishはcandidate hash、decision recordの承認marker、current HEAD、両baseline hash、2scene row、式、PPD、pinをsource変更前に検証し、承認されたexact candidateだけをatomic publishする。承認前、別hash、再生成candidate、identity不一致ではpublishしない。

threshold transactionの`before-publish`／`after-publish` seamは、失敗後にsource hashが不変でtemp／backup／rollback-discardが残らないことを検証する。承認済みcandidateと60行実測表は監査証拠としてbuild配下に保持する。

sRGB transfer、PPD、FLIP pinの変更、またはR1 presentation gamma修正時は、既存thresholdを流用しない。indoor/outdoor両baseline、通常10回ずつのnoise、全40人工差候補、thresholdをすべて再生成し、candidate実測表に対する人間承認を取り直す。

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
