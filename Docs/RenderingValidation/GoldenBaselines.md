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

## R1 最終数値検証

R1の統合fixtureはIndoor/Outdoorの公開露出・照明条件を使用し、P2 1行、P3 6行、P4 23行、P5 10行の計40 static rowsを、各行で`BackBuffer → PresentationColor → SceneColor`の3 source（計120 capture）として検証する。BackBufferは256×256の全画素・全RGBA channelをIEC 61966-2-1 sRGB oracleと±1 LSBで照合し、PresentationColor/SceneColorはRGBA16Fの全channelを走査する。forced format行は`R8G8B8A8_SRGB`と`R8G8B8A8_UNORM`を各1回実readbackする。

```powershell
& .\build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical
```

成功sentinelはstatic40、numerical68、capture120、各source40、actual byte channels 10485760、float channels 20971520、forced rows 2、forced channels 56、capture latency exact 2である。Indoor/Outdoorの`--measure-visual`実行はBackBufferを指定し、object-presenceのdeltaも同じ実出力で検査する。

## Visual threshold校正と承認

candidate生成とsource publishは別invocation、別の人間承認gateである。P6Aではformal生成・publishを実行せず、scriptのSelfTestだけをbuild配下の固定synthetic fixtureで検証する。

```powershell
& .\Scripts\CalibrateRenderingVisualThresholds.ps1 -GenerateCandidate -Iterations 10 -RequireGpu -CodeHead $codeHead -BaselineIndoorSha256 $indoorBaselineSha256 -BaselineOutdoorSha256 $outdoorBaselineSha256 -DecisionPath Docs/RenderingValidation/R1Acceptance.md -DecisionSection 'R1 visual approval'
$candidateHash = (Get-FileHash -LiteralPath '.\build\RenderingValidation\Calibration\VisualThresholds.candidate.tsv' -Algorithm SHA256).Hash
# candidate、60行実測表、両sceneのnoise／人工差／式を人間がreviewし、R1 visual approval節へ承認lineとhashを記録する。
& .\Scripts\CalibrateRenderingVisualThresholds.ps1 -PublishApprovedCandidate -CandidateSha256 $candidateHash -CodeHead $codeHead -BaselineIndoorSha256 $indoorBaselineSha256 -BaselineOutdoorSha256 $outdoorBaselineSha256 -DecisionPath Docs/RenderingValidation/R1Acceptance.md -DecisionSection 'R1 visual approval'
```

`-GenerateCandidate`はbuild配下のcandidate、60行実測表、`VisualThresholdCandidateManifest.json`だけを作り、source thresholdへ書かない。F9値は`\A(0\.[0-9]{9}|1\.000000000)\z`を厳密受理し、fraction digitsからnanounitsへ直接変換する。thresholdはinteger millionthsのcanonical F6とし、strict separationを満たす最初の人工差候補を再計算する。publishはbaseline manifest、candidate／measurement hash、decision節、current HEAD、両baseline hash、2 scene row、式、PPD、pinをsource変更前に同じpure validatorで検証し、承認されたexact candidateだけをatomic publishする。

threshold candidate manifestは`build/RenderingValidation/Calibration/VisualThresholdCandidateManifest.json`、baseline candidate manifestは`build/RenderingValidation/R1/BaselineCandidate/Manifest.json`であり、duplicate／unknown／case mismatch／type mismatch、path traversal、hash不一致を拒否する。threshold transactionの`before-publish`／`after-publish`／success seamはproductionと同じpure prepublish validatorおよびtransaction helperをsynthetic pathへ渡し、失敗後にsource hashが復元され、成功後はcandidate hashと一致し、temp／backup／rollback-discardが残らないことを検証する。

sRGB transfer、PPD、FLIP pinの変更、またはR1 presentation gamma修正時は、既存thresholdを流用しない。indoor/outdoor両baseline、通常10回ずつのnoise、全40人工差候補、thresholdをすべて再生成し、candidate実測表に対する人間承認を取り直す。

## Baseline更新

candidate生成とpublishを分離し、旧来の直接`-Approve`入口は使用しない。formal baseline candidate生成は次の入口で行う。

```powershell
& .\Scripts\UpdateRenderingGoldenBaselines.ps1 -GenerateCandidate -CodeHead $codeHead
```

`-GenerateCandidate`はcurrent HEADを`CodeHead`と厳密照合し、Indoor/Outdoorを`--scene=<scene> --capture-source=back-buffer --write-baseline-staging`でcaptureする。fixed stagingをvalidatorへ渡し、candidateへbyte bridgeした後にcandidate hashとsource開始hashをmanifestへatomic発行する。source baselineは変更しない。

人間承認後のpublishだけが次の入口を使う。

```powershell
& .\Scripts\UpdateRenderingGoldenBaselines.ps1 -PublishApprovedCandidate -CodeHead $codeHead -ApprovedIndoorSha256 $approvedIndoorSha256 -ApprovedOutdoorSha256 $approvedOutdoorSha256 -DecisionPath Docs/RenderingValidation/R1Acceptance.md -DecisionSection 'R1 visual approval'
```

publishはmanifest、candidate hash、source開始hash、current HEAD、`R1 visual approval`節のheading／approval line（各exactly once）をsource mutation前に検証する。publish入口はmanifest済みのcandidateと既存source 2枚だけを受け取り、staging capture／validationは行わない。片側publish failureでは両sourceを同じ開始hashへ戻し、rollback failure時だけapplication backupを残す。

アプリケーションが書けるのはbinary root内の次の固定stagingだけである。

- `build\RenderingValidation\BaselineStaging\Indoor.png.tmp`
- `build\RenderingValidation\BaselineStaging\Outdoor.png.tmp`

更新scriptのGenerate入口だけが両sceneをcaptureしてstaging validatorを通過させ、candidate hash／source開始hashとmanifestを最後に発行する。Publish入口は`Test\Core\Rendering\Baselines\RenderingValidation\Indoor.png`と`Outdoor.png`が存在することを確認し、両方を`File.Replace`する1 transactionとしてpublishする。片sceneの失敗、GPU skip、validation failure、publish failureでは両sourceを元の状態へ戻す。

transaction seamは`NORVESLIB_BASELINE_TRANSACTION_TEST_FAILURE=before-publish`と`after-first-publish`で検証できる。P6Aで実行する`-SelfTestR1Contract`は、formal pathやtracked source／decisionを変更せず、production pure prepublish validatorとtransaction helperへsynthetic source／candidateを渡して、partial／corrupt／hash／path／reparse／decision／exit codeの拒否、同じ復旧条件、成功publishを検証する。staging validatorのnegativeは次のcommandで実行し、source baselineのhashが変化していないことを併せて確認する。

```powershell
& .\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-fixed-staging-negative=corrupt-indoor
& .\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-fixed-staging-negative=wrong-size-outdoor
```

corrupt PNGは`DecodeFailed`、decode可能な128×256 PNGは`InvalidDimensions`としてrejectされる。

## P6A script契約

P6Aで実行するSelfTestは次の3本で、いずれもformal candidate・publish、tracked source、decision recordを変更しない。

```powershell
& .\Scripts\CalibrateRenderingVisualThresholds.ps1 -SelfTestR1Contract
& .\Scripts\UpdateRenderingGoldenBaselines.ps1 -SelfTestR1Contract
& .\Scripts\TestRenderingGpuCTestContract.ps1 -SelfTestR1Contract
& .\Scripts\TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21
```

GPU CTest contractはexact21 name、label集合`GPU;RenderingValidation`、`RESOURCE_LOCK=NorvesLibGPU`、`SKIP_RETURN_CODE=125`、force-skip 7件、family breakdown `2+2+3+6+2+3+3`、command sequenceを検証する。

SelfTestのpure validator coverageは、Baselineが`Get-R1BaselinePublishInputs`、Thresholdが`Get-R1ThresholdPublishInputs`を正のsynthetic fixtureで通過させ、各fixtureを1項目だけ変えて拒否する。Baselineはpartial／corrupt candidate、candidate／source-start hash、duplicate／unknown／case／type、repo外／staging境界／reparse、HEAD、heading、approval 0／2／section外、exit 125／nonzeroを含む。Thresholdは10+10 noise、40 artificial、fixed order、F6、raw max、HEAD／CodeHead、baseline bridge、candidate／measurement hash、P5 manifest、duplicate／unknown／case／type、decision section、approval 0／2／section外を含む。いずれもsource非変更と残留0を確認し、transactionはfailure 2経路とsuccess経路を確認する。

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
