# R0 描画検証受入れ記録

## 記録の範囲

このtracked文書自身をR0受入れの一次記録とする。各gateのcommand、exit、pass／skip、negative marker、Git object identity、visual metricsを以下へ要約し、補助reportやbuild logが無いcheckoutでも監査可能にする。ログ全文はbuild生成物のままcommitしない。

- branch: `feature/rendering-r0-validation`
- 実走HEAD: `1d122fb732870d59c7cdb33cfffe446bdfd536b3`
- 採用HEAD: `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`
- GPU: `NVIDIA GeForce RTX 4080`
- driver: `591.86`
- 証拠採取日: 2026-08-12 JST

G04／G05／G07のfocused GPU／10回連続は実走HEADで取得した。2026-08-12にユーザー／gate ownerが承認した限定evidence-transfer契約に基づき、保護対象のsource／dependency／assetのGit object identity一致、独立したsource-text contract test 1pathだけの差分、採用HEADのnormal full suite exit 0を条件として採用HEADへ適用する。保証範囲は後述のGit object identityであり、それを超える同一性は主張しない。

通常GPU acceptanceでは実GPU testのexit 125または`Skipped`を受入れない。normal full suiteの7件の`Skipped`は実GPU testではなく、skip挙動を検査する専用contract testである。

| Evidence ID | ゲート | ステータス | command／結果の要約 |
|---|---|---|---|
| R0-G01 | branch／commit chain／Deferred決定 | Passed | `git branch --show-current`、`git rev-parse HEAD`、`git log --oneline --decorate -12`、tracked／all `git status`、`git stash list`、tracked performance path検索は各exit 0。decision文書あり、branch／採用HEAD一致、tracked clean、all statusはacceptance 1path、stash exact 1件、performance path 0件。 |
| R0-G02 | CPU harness／negative | Passed | strict 1px、known floatとNaN／+Inf／-Inf、perceptual artificial、staging 2件、parser self-testは各exit 0。negative 7pathはapproved visual commitから採用HEADまでdiff exit 0。exact markerは本文に記録。 |
| R0-G03 | GPU CTest contract | Passed | `Scripts/TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21`: exit 0、GPU count 21。forced full suiteで同21件が明示`Skipped`。 |
| R0-G04 | float／HDR SceneColor GPU | Passed | 実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3`、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。focused commandはexit 0、通常3 Passed／通常skip 0、forced contract 2 Skipped。GPU／driverは上記。限定evidence transferを適用。 |
| R0-G05 | golden 20 compare | Passed | 実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3`、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。`Scripts/RunRenderingValidation.ps1 -Iterations 10 -RequireGpu`: exit 0、20 compare、skip 0、両scene max mean FLIP `0.000000000`／max raw `0`。限定evidence transferを適用。 |
| R0-G06 | GPU性能予算 | Deferred | 2026-08-12のユーザーdecision。test過多を避けて将来CI GPU runnerの性能回帰トラックへ移管する。tracked performance pathは0。R0を阻害しないが、性能評価を成功扱いするものではない。 |
| R0-G07 | 開発機10回連続 | Passed | 実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3`、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。indoor／outdoor各`successful_runs=10`、各`skipped_runs=0`、合計20 compare、exit 0。限定evidence transferを適用。 |
| R0-G08 | baseline／threshold identity | Passed | `RebaselineNotRequired`。tracked 2026-08-10実走記録、3 SHA-256、7 trigger path、`git diff --quiet` exit 0、実走HEADから採用HEADへの限定evidence transferを照合。再生成／再承認は不要。 |
| R0-G09 | forced GPU skip full suite | Passed | `NORVESLIB_FORCE_GPU_TEST_SKIP=1`で`ctest --test-dir build -C Debug --output-on-failure --timeout 180`: exit 0、`219 total = 198 Passed + 21 Skipped`、failure 0、175.36 sec、終了後`FORCE_ENV_CLEARED`。 |
| R0-G10 | normal full suite | Passed | 採用HEADで`ctest --test-dir build -C Debug --output-on-failure --timeout 180`: exit 0、`219 total = 212 Passed + 7 Skipped`、failure 0、272.81 sec。7件はintentional skip-contract、通常GPU test skip 0。 |
| R0-G11 | 独立review round 2 | Passed | 2026-08-12、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。独立Codex round 2 `r0_final_impl_reviewer` とClaude fable round 2を実施。Codexの残件はparser command／marker 1件、ClaudeのB1–B9はall ADDRESSEDで残件は`trigger diff exit 0` marker 1件だった。final mechanical fix後、G08 identity gateは`G08_IDENTITY_GATE=PASS trigger_diff_exit=0 g05_g07_transfer=True`、exit 0。open blocking 0。最大2周のためround 3は未実施。 |
| R0-G12 | hygiene／最終scope | Passed | hygiene before G12: ChangedPath=`Docs/RenderingValidation/R0Acceptance.md`、Utf8Bom=True、CrlfOnly=True、TrailingWhitespace=False、DiffCheckExit=0、IgnoreExit=0、Status=`?? Docs/RenderingValidation/R0Acceptance.md`。最終scope exact 1path、plans ignored、stageなし。 |

## R0-G01 Git／decision provenance

| command | exit | 記録結果 |
|---|---:|---|
| `git branch --show-current` | 0 | `feature/rendering-r0-validation` |
| `git rev-parse HEAD` | 0 | `3d86104aa2d18a9f941a21aece45ec6ee3f89f65` |
| `git log --oneline --decorate -12` | 0 | 下記commit chain |
| `git status --short --untracked-files=no` | 0 | 出力なし。tracked tree clean。 |
| `git status --short` | 0 | `?? Docs/RenderingValidation/R0Acceptance.md` のみ。 |
| `Test-Path Docs/RenderingValidation/PerceptualDiffSelection.md` | — | `True`。tracked decision文書あり。 |
| `git stash list` のmessage照合 | 0 | `保留: R0 GPU性能予算ゲート 2026-08-12` exact 1件。 |
| `git ls-files \| Where-Object { $_ -match 'RenderingPerformance\|PerformanceBudget' }` | 0 | tracked performance path 0件。 |

新しい順のR0 commit chain:

- `3d86104` test: 空shadowの描画パス契約を更新する
- `1d122fb` feat: RenderGraph pass GPU 計測を追加する
- `17991bf` fix: 空のshadow passでもdepthを確定する
- `fa11f51` fix: Vulkan image layout契約を整合させる
- `1cbb259` test: LDR FLIP 描画差分ゲートを追加する
- `2da856a` docs: S1 知覚差分方式を選定する
- `b491d4a` feat: SceneColor capture を FramePacket 化する
- `dc51733` feat: HDR float readback 検証を追加する
- `6ee7300` test: PNG golden 描画回帰を追加する
- `ff91dfb` test: R0 描画 fixture を固定化する
- `8b0ae77` GPU CTest のスキップ契約を整備する

## 限定evidence-transfer certificate

- 承認日: 2026-08-12
- 承認者: ユーザー／R0 gate owner
- 実走HEAD: `1d122fb732870d59c7cdb33cfffe446bdfd536b3`
- 採用HEAD: `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`
- `git diff --name-status 1d122fb732870d59c7cdb33cfffe446bdfd536b3 3d86104aa2d18a9f941a21aece45ec6ee3f89f65`: exit 0、出力は`M\tTest/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp`の1件だけ。
- 上記変更fileは`ShadowMapPass.cpp`のsource textを検査する独立executableの唯一のsourceであり、G04／G05／G07 targetのsourceではない。
- 採用HEADのnormal full suiteはexit 0、`219 total = 212 Passed + 7 Skipped`、failure 0。変更されたcontract targetを含むsuite全体を実行済み。

両HEADでexact一致した保護対象Git object:

| path | object ID |
|---|---|
| `Library` | `425dc247a63cfb25ba48ba9d62275f0b72856f80` |
| `Assets` | `a4c73ac12f47cbbf2768dc6cbdd7f4ade0e60eb3` |
| `Scripts` | `5dd29797f5b2ee2f0c3196b61f53fca2c4ee0b51` |
| `Test/Core/Rendering/Baselines` | `8d3b83b56b07f2e73313e77b76df4727306127b0` |
| `Test/Core/Rendering/CMakeLists.txt` | `d99f54b2b1ce199509d3f7d416366d4a9d8ee5e3` |
| `Test/Core/Rendering/FrameCaptureFloatReadbackVulkanTest.cpp` | `47029785a8166d6528b03a9e1706b871de29f3e7` |
| `Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp` | `b5c41aa02246c30585abc8104fe607170688b72f` |
| `Test/Core/Rendering/RenderingGoldenImageTest.cpp` | `4b4a60b27b704069b3231bc5e3360ff0cdf150a7` |
| `Test/Core/Rendering/RenderingValidation` | `815a0d4adadad26ed8165b7feb9847e798c37932` |

このcertificateにより、G04／G05／G07の実走revisionを書き換えず、保護対象source／dependency／asset identityと採用HEADのfull-suite結果に限定して証拠を移管する。

## R0-G02 negative provenance

| 対象 | command | exit／result marker |
|---|---|---|
| strict 1px | `ctest --test-dir build -C Debug --output-on-failure -R '^RenderingGoldenImageComparatorTest$'` | exit 0、1 Passed／0 Skipped、`one_pixel_negative=rejected max_coordinate=(0,0)` |
| known float／non-finite | `ctest --test-dir build -C Debug --output-on-failure -R '^RenderingFloatImageTest$'` | exit 0、1 Passed／0 Skipped、`known_float=green nan_negative=rejected positive_inf_negative=rejected negative_inf_negative=rejected` |
| perceptual artificial | `ctest --test-dir build -C Debug --output-on-failure -R '^RenderingPerceptualArtificialDifferenceTest$'` | exit 0、1 Passed／0 Skipped、`RenderingPerceptualArtificialDifferenceTest Passed` |
| corrupt staging | `.\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-fixed-staging-negative=corrupt-indoor` | exit 0、`DecodeFailed`としてreject、両source baseline hash不変 |
| wrong-size staging | `.\build\Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe --self-test-fixed-staging-negative=wrong-size-outdoor` | exit 0、`InvalidDimensions`としてreject、両source baseline hash不変 |
| metrics parser | `.\Scripts\CalibrateRenderingVisualThresholds.ps1 -SelfTestMeasurementParser` | exit 0、全negativeを理由付きreject、`measurement_parser_self_test=PASS` |

negative artifact identity commandは次の7pathを対象にした。

```powershell
git diff --quiet 1cbb259c030df3afde11f5960591cbf0e69f2c59 3d86104aa2d18a9f941a21aece45ec6ee3f89f65 -- `
  Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp `
  Test/Core/Rendering/RenderingFloatImageTest.cpp `
  Test/Core/Rendering/RenderingPerceptualDiffTest.cpp `
  Test/Core/Rendering/RenderingValidation/RenderingFloatImage.cpp `
  Test/Core/Rendering/RenderingValidation/RenderingFloatImage.h `
  Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.cpp `
  Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.h
```

結果はexit 0であり、上記negative artifactはapproved visual commitから採用HEADまで不変だった。

## R0-G03／G04 GPU provenance

GPU CTest contract:

```powershell
.\Scripts\TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21
```

結果はexit 0、GPU count 21。全21件がforced full suiteで明示`Skipped`となった。

float／HDR focusedは実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3` で次を実行した。

```powershell
ctest --test-dir build -C Debug --output-on-failure -R '^(FrameCaptureFloatReadbackVulkan(Test|SkipContractTest)|RenderingHdr(Indoor|Outdoor)SceneVulkanTest|RenderingHdrSceneVulkanSkipContractTest)$'
```

結果はsuite exit 0。通常3件（known float、indoor HDR、outdoor HDR）は3 Passed／0 Skipped、forced contract 2件は2 Skipped。通常GPU実行はGPU `NVIDIA GeForce RTX 4080`、driver `591.86`で、known FP16、実RGBA16F textureのNaN／+Inf／-Inf reject、indoor／outdoor finite SceneColorを確認した。採用HEADへの適用根拠は限定evidence-transfer certificateに記録した。

## R0-G05／G07 10回連続

実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3` で次を実行した。

```powershell
.\Scripts\RunRenderingValidation.ps1 -Iterations 10 -RequireGpu
```

結果はexit 0、合計20 compare、GPU skip 0。

| scene | successful_runs | skipped_runs | max_mean_flip | max_raw |
|---|---:|---:|---:|---:|
| indoor | 10 | 0 | `0.000000000` | 0 |
| outdoor | 10 | 0 | `0.000000000` | 0 |

採用HEADへの適用根拠は限定evidence-transfer certificateに記録した。

## R0-G06 性能保留decision

- 日付: 2026-08-12
- ステータス: Deferred
- 決定者: ユーザー／R0 gate owner
- 判断: R0の性能予算、性能negative、改定手順はtest過多を避けるため実装・実走しない。
- 移管先: 将来のCI GPU runnerによる独立した性能回帰トラック。
- 永続根拠: このtracked acceptanceにdecision、理由、移管先、R0非阻害を記録する。
- 補助整合証拠: stash message `保留: R0 GPU性能予算ゲート 2026-08-12` exact 1件。
- tracked実装確認: `git ls-files | Where-Object { $_ -match 'RenderingPerformance|PerformanceBudget' }` はexit 0、0path。
- R0への影響: このDeferredはR0を阻害しない。性能を評価済みとは扱わない。

## R0-G08 identity／再校正trigger

tracked `GoldenBaselines.md` の実走記録は日時`2026-08-10 00:27 JST`、2回publish、2scene compare、`-Iterations 10 -RequireGpu`による20 scene exit 0を保持する。tracked `PerceptualDiffSelection.md` は2026-08-10のユーザー承認「承認します」と下記threshold hashを保持する。

| artifact | SHA-256 |
|---|---|
| `Indoor.png` | `545E745CE9958F310A551B0D71BEAB4DAD35743C6367E0DA0724F9930E6F49E7` |
| `Outdoor.png` | `3676A470814C68841BF1C8E4BF8612802042AB936AAA77E1A9937C5EC642BA7E` |
| `VisualThresholds.tsv` | `01755EC8E6091400B3589802411DCF1AD0AB9D3AF6EA5D9658390AC820DC414F` |

再校正triggerの照合command:

```powershell
git diff --quiet 1cbb259c030df3afde11f5960591cbf0e69f2c59 3d86104aa2d18a9f941a21aece45ec6ee3f89f65 -- `
  Library/ThirdParty/flip/FLIP.h `
  Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.h `
  Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.cpp `
  Assets/Shaders/tonemapping.frag `
  Library/Core/Public/Rendering/ToneMappingPass.h `
  Library/Core/Private/Rendering/ToneMappingPass.cpp `
  Library/Core/Private/Rendering/ToneMappingPassGpuTypes.h
```

7 trigger pathの照合結果は`trigger diff exit 0`。baseline／threshold identity、sRGB transfer、PPD、FLIP pin、presentation gammaの再校正入力は不変である。G05／G07は実走HEADから採用HEADへの承認済みtransferを経た同一source／dependency／asset identity証拠である。したがって`RebaselineNotRequired`とし、このbranchでcandidate生成や人間再承認を行わない。identityまたはtriggerが変わる場合はG08をFailedとして停止し、Task 6のcandidate生成／人間承認境界へ戻る。

## R0-G09／G10 full suite

normal full suiteは採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65` で実行した。

```powershell
ctest --test-dir build -C Debug --output-on-failure --timeout 180
```

結果はexit 0、`219 total = 212 Passed + 7 Skipped`、failure 0、272.81 sec。7 Skippedは次のintentional skip-contract testであり、通常GPU testのskipは0。

- `RHIGPUTimestampVulkanSkipContractTest`
- `RHITextureToBufferReadbackVulkanSkipContractTest`
- `RenderingValidationFixtureVulkanSkipContractTest`
- `RHIImageLayoutVulkanValidationSkipContractTest`
- `FrameCaptureFloatReadbackVulkanSkipContractTest`
- `RenderingHdrSceneVulkanSkipContractTest`
- `RenderingGoldenVulkanSkipContractTest`

forced-skip full suiteは同じ採用HEADで、`NORVESLIB_FORCE_GPU_TEST_SKIP=1`をtry/finallyで設定・除去して実行した。

```powershell
ctest --test-dir build -C Debug --output-on-failure --timeout 180
```

結果はexit 0、`219 total = 198 Passed + 21 Skipped`、failure 0、175.36 sec。GPU label 21件がすべて明示`Skipped`となり、終了後に`FORCE_ENV_CLEARED`を確認した。この結果は実GPU acceptanceの代用ではない。

## review状態とcommit前gate

engine実装auditはR0 blocking 0、test auditの実装test指摘はnonblockingまたはinvalidと裁定済み。2026-08-12の独立Codex round 2 `r0_final_impl_reviewer` とClaude fable round 2後に残った2件の機械修正を完了し、G08 identity gateもexit 0で確認した。round 2後のopen blockingは0件であり、レビュー最大2周のためround 3は実施していない。R0-G11は`Passed`とする。

hygiene before G12はBOM／CRLF／末尾空白、diff check、plan ignore、changed path exact 1件、stageなしを満たしたため、R0-G12は`Passed`とする。completion commit直前には同じhygieneを再実行する。manual push前にロードマップ上のR0を完了として更新しない。
