# TASKS — NorvesLib

Rendering R1完了後のR2実装タスク。仕様は `Docs/Plans/RenderingR2SkyAtmosphereCsmPlan.md` と `Docs/Plans/RenderingRoadmap.md` を参照する。R1の完了コミットと受入れ証跡はPROGRESS.mdを基点にし、R2では空・大気とCSMを別々の検証可能な契約として閉じる。運転ファイルは描画sourceとは別の変更として扱う。

実装・評価・進捗更新の運転は現在の AGENTS.md を適用する。旧計画の担当モデル、メイン実装禁止、反復ごとの再承認は現在の合意へ置き換える。数式、閾値、サンプル数、source範囲、画像と閾値の公開承認は保持する。作業一覧などの運転ファイルは描画sourceとは別の変更として扱う。運転ファイルだけのコミットを跨ぐときは、記録した描画コミットへの祖先関係とsource同一性で基点を確認する。

## R1-P5: 透明描画を物理ライト・GGX・IBLへ接続する
- status: done
- done-when: 条件付き計画v4の固定10行、direct/IBL/shadow/metallic mutation、P4回帰6条件、Indoor/Outdoorのobject-presenceが数値契約を満たす。focused8 build、CPU6、skip契約を通し、source18本を1論理変更としてコミットする。
- verify: `cmake --build build --config Debug --target DirectionalShadowPassWiringContractTest ForwardPassPipelinePlacementTest LightingLightBufferTest RenderGraphCompileTest LightingParamsLayoutTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowPassWiringContractTest|ForwardPassPipelinePlacementTest|LightingLightBufferTest|RenderGraphCompileTest|LightingParamsLayoutTest|RenderingValidationSceneContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingHdrSceneVulkanSkipContractTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=known-cd-lambert`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=ibl-prefilter-nonconstant`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=dfg-lut`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=ibl-roughness-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=white-furnace`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=direct-conductor-endpoint`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=transparent-physical-lighting`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- paths: Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/ShadowMapPass.h, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, Library/Core/Public/Rendering/ForwardPass.h, Library/Core/Private/Rendering/ForwardPass.cpp, Assets/Shaders/forward_transparent.vert, Assets/Shaders/forward_transparent.frag, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-5-conditional-implementation-plan-v4.md` の技術仕様を全文読む。GPUとobject-presenceは同計画§7の実コマンド・全行判定を追加実行しログを開く。P4のsource commit/report SHAはPROGRESS.mdを使用。P5 scope18とsingle-source shadow追加は2026-08-15の承認済み。source baselineとthresholdは更新しない。後続P6aが読むobject-presence log prefixは同計画とP6a入口に一致させる。
- notes: 全フェーズ統合では、承認済み最終fixtureのIndoor +4 EV/Outdoor main directional 10000 luxをobject-presenceのscenario-local stateへ先行適用する。P5ではBuildSceneLayoutの既定値を保持し、P6aで既定値を統合する。数値scenarioの露出・10行・対象ROI・thresholdは保持。固定物理geometryを実GPU行列規約で表すfixture補正も行う。根拠・完了条件は `Docs/Plans/RenderingR1P5FixtureIntegration20260908.md`。数値背景だけを全画面へ広げ、background-only ROIを `[16,47]²` に固定する不整合修正をメインの実装判断で採用する。根拠・変更前契約は `Docs/Plans/RenderingR1P5BackgroundProposal20260908.md`。追加確認は取り下げ、個別承認eventは作らない。

## R1-P6A: 最終fixtureと全数値キャプチャを統合する
- status: done
- done-when: P5完了後、条件付き計画v4の40 static rows/120 captures、全画素sRGB oracle、forced format行、GPU CTest exact21、3 script self-test、focused7 build/CPU6、Indoor/Outdoor object-presenceを満たす。source20本の変更をコミットし、P6bの基点と証跡を記録する。
- verify: `cmake --build build --config Debug --target ForwardPassPipelinePlacementTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest RenderingPerceptualDiffTest SceneSerializerTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ForwardPassPipelinePlacementTest|RenderingValidationSceneContractTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest|RenderingPerceptualArtificialDifferenceTest|SceneSerializerTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r1-fixture-contract`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- verify: `pwsh -NoProfile -File Scripts/CalibrateRenderingVisualThresholds.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/UpdateRenderingGoldenBaselines.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21`
- paths: Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Assets/Shaders/mesh3d.vert, Assets/Shaders/mesh3d.frag, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingPerceptualDiffTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.h, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.h, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.cpp, Test/Core/Rendering/CMakeLists.txt, Scripts/UpdateRenderingGoldenBaselines.ps1, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/TestRenderingGpuCTestContract.ps1, Docs/RenderingValidation/GoldenBaselines.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6a-conditional-implementation-plan-v4.md` の技術仕様を全文読む。plan SHA=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F は2026-08-15承認済み。§11のGPU/skip/各SelfTestR1Contractも実行し、各raw outputを開く。P6b floor diagnosisを受け、Indoorだけplane補正を適用するforward-fixを `2a50d4c`→`d4cbe3e` で確定した。P6bの正式候補生成・source publish・full build/full CTestはこのタスクでは行わない。
- verify: `pwsh -NoProfile -File .harness/runs/20260908-084154/p6a-verify-scene-freshness.ps1`
- notes: 独立recheck11件、bundle第2周評価、別枠のR1統合接続評価を完了。詳細と未読範囲はPROGRESS.mdおよびP6a acceptance receipt。追加2 verifyは原計画§9.2/11.4の既存条件。

## R1-P6B: 物理描画の基準画像・閾値と受入記録を確定する
- status: done
- done-when: 承認済みv3のcontrol/identityを固定し、baseline2枚とthreshold60行の候補をそれぞれ実物承認後に公開する。BackBuffer20回は全exit0、full build1回とfull CTest1回は219件中212 PASS/7意図的skip/0 fail、受入れ証跡のblocking指摘を修正し、P6b tracked assetsを確定する。
- paths: Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png, Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png, Test/Core/Rendering/Baselines/RenderingValidation/VisualThresholds.tsv, Docs/RenderingValidation/R1Acceptance.md
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6b-conditional-implementation-plan-v3.md` を適用した。baseline/thresholdを別々の承認event後に公開し、BackBuffer20は20/20 PASS、targetless full buildはexit 0、full CTestは219件中212 passed/7 intentional skipped/0 failed。現行証跡は`Docs/RenderingValidation/R1Acceptance.md`、候補reportはforward-fix名で旧discard reportを上書きしていない。P6b実装・受入れコミットは`eedec7a681489768645eabf77355886ae8e755c0`、Roadmap localもR1完了へ更新済み。

## R2-P1: 空パラメータと太陽方向の参照契約を定義する
- status: done
- done-when: `SkyAtmosphereParameters` がRenderThreadへ安全に渡せる値だけを持ち、太陽高度・方位角から正規化された方向を決定できる。無効な入力を有限な既定値へ正規化し、Hillaire 2020系参照評価の天頂/太陽近傍サンプルと太陽ディスク露出値をCPUテストで固定する。Rendering層からRHI/Vulkanへの依存を増やさない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^SkyAtmosphereModelTest$"`
- paths: Library/Core/Public/Rendering/SkyAtmosphere.h, Library/Core/Private/Rendering/SkyAtmosphere.cpp, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/SkyAtmosphereModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P2: 空LUTと太陽ディスクをLighting前段へ接続する
- status: done
- done-when: 同一の空スナップショットから透過率/空放射輝度を生成し、RenderGraph named resourceとしてLightingPassが読める。空背景は固定HDRへ暗黙に戻らず、空無効時だけ既存フォールバックを使う。太陽ディスクはR1プリエクスポージャを通り、有限性・安全域の飽和契約を満たす。
- verify: `cmake --build build --config Debug --target SkyAtmospherePassContractTest LightingParamsLayoutTest ForwardPassPipelinePlacementTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmospherePassContractTest|LightingParamsLayoutTest|ForwardPassPipelinePlacementTest)$"`
- paths: Library/Core/Public/Rendering/SkyAtmospherePass.h, Library/Core/Private/Rendering/SkyAtmospherePass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/sky_atmosphere.frag, Assets/Shaders/lighting.frag, Test/Core/Rendering/SkyAtmospherePassContractTest.cpp, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P3: 動的空を放射輝度IBLとEVレンジへ統合する
- status: done
- done-when: 空のequirectangular放射輝度から既存のDiffuseIrradiance/PrefilteredSpecularを生成し、太陽高度の変更で環境光も更新される。朝/昼/夕のEVケースで空・直接太陽・IBLの出力が有限で、R1の静的HDR経路と無効時フォールバックが回帰しない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereIblTest LightingLightBufferTest RenderingValidationSceneContractTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereIblTest|LightingLightBufferTest|RenderingValidationSceneContractTest|LightingParamsLayoutTest)$"`
- paths: Library/Core/Private/Rendering/SkyAtmosphere.cpp, Library/Core/Private/Rendering/SkyAtmospherePass.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/SkyAtmosphereIblTest.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P4: CSM分割とテクセル安定化のCPU契約を実装する
- status: done
- done-when: 4カスケードの分割距離、各ライト行列、受影対象の深度範囲、テクセル中心スナップが同じカメラ/方向ライトから決まる。near/far逆転、無効方向、非有限bounds、サブテクセル移動に対して有限な安全結果を返し、境界に隣接するカスケードの範囲が連続する。
- verify: `cmake --build build --config Debug --target CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest)$"`
- paths: Library/Core/Private/Rendering/CascadedShadowLightMatrices.h, Library/Core/Private/Rendering/CascadedShadowLightMatrices.cpp, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.h, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.cpp, Test/Core/Rendering/CascadedShadowLightMatricesTest.cpp, Test/Core/Rendering/DirectionalShadowLightMatricesTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P5: RHIの配列layer attachmentとShadowMapPassのCSM深度記録を実装する
- status: done
- done-when: 4層D32 array textureを作成し、各layerを1層Framebufferへ安全にattachできる。ShadowMapPassは各カスケードを別行列で記録し、RenderGraphの公開リソースは配列深度として一貫する。失敗時は部分リソースを公開せず、既存単一影の所有権/破棄順序を壊さない。
- verify: `cmake --build build --config Debug --target ShadowMapArrayLayerContractTest DirectionalShadowPassWiringContractTest RenderGraphTextureUsageContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ShadowMapArrayLayerContractTest|DirectionalShadowPassWiringContractTest|RenderGraphTextureUsageContractTest)$"`
- paths: Library/Core/Public/RHI/IFramebuffer.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Public/RHI/IGPUResourceAllocator.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.h, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.cpp, Library/Core/Public/Rendering/ShadowMapPass.h, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/CMakeLists.txt, Test/Core/Rendering/ShadowMapArrayLayerContractTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, Test/Core/Rendering/RenderGraphTextureUsageContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P6: Lighting/ForwardのCSMサンプリングへ移行する
- status: done
- done-when: GPU lighting params、descriptor、lighting/forward shaderが4行列・分割距離・sampler2DArrayを同じlayoutで使用する。カスケード境界にブレンドを適用し、影なし/単一層/不完全公開値では安全な既定へ戻る。既存R1の単一方向影契約と透明描画契約が維持される。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest LightingLightBufferTest ForwardPassPipelinePlacementTest DirectionalShadowPassWiringContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest)$"`
- paths: Library/Core/Private/Rendering/LightingPassGpuTypes.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/ForwardPass.cpp, Library/Core/Public/Rendering/ForwardPass.h, Assets/Shaders/lighting.frag, Assets/Shaders/forward_transparent.vert, Assets/Shaders/forward_transparent.frag, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, TASKS.md, PROGRESS.md

## R2-P7: 空・太陽・CSMの数値/画像受入れを固定する
- status: done
- done-when: 朝/昼/夕のgolden、天頂/太陽近傍float readback、太陽ディスク有限性、カスケード境界の欠落/二重化、サブテクセル移動の影エッジ変化率を実データで検証する。既存Indoor/Outdoor R1 baselineは上書きせず、R2用シナリオと証拠を分離する。
- verify: `cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrSceneCaptureTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest)$"`
- verify: `build\\Test\\Core\\Rendering\\Debug\\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract`
- verify: `build\\Test\\Core\\Rendering\\Debug\\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest|SkyAtmospherePassContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RHIImageLayoutVulkanDrawSceneTest|RHIImageLayoutVulkanDrawThenNoCasterSceneTest|RenderingHdrOutdoorSceneVulkanTest)$"`
- paths: Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/Baselines/RenderingValidation/R2*, Test/Core/Rendering/Thresholds/RenderingValidation/R2*, Docs/RenderingValidation/R2Acceptance.md, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/UpdateRenderingGoldenBaselines.ps1, TASKS.md, PROGRESS.md

## R2-P8: R2受入れ記録と後続計画の入口を確定する
- status: done
- done-when: R2の実装コミット、検証ログ、golden/threshold、既知の非対象を一つの受入れ記録へまとめ、RoadmapのR2行を完了へ更新する。R1証跡と候補を再利用せず、R3は未着手の新規M1として残す。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R2Acceptance.md, Docs/Plans/RenderingRoadmap.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md
\r\n# R3: ボリュメトリクス

### 採用する設計

- 既存のSceneColor/SceneDepthを使う全画面 `VolumetricsPass` とし、3D froxel texture・compute pass・Vulkan/RHI実装は追加しない。
- 高さ密度は指数関数モデル、視線方向のBeer-Lambert透過率は解析積分する。方向光の単一散乱だけを固定24ステップで積分し、既存の4カスケードCSMで遮蔽する。
- passはLighting後・Forward透明描画前に置く。R2 `SkyAtmosphere.Radiance` を遠景の散乱先へ連続的にブレンドし、透明物自体への距離フォグ適用はR3の対象外とする。
- GPU性能ゲートはRoadmapどおり延期する。R1 baseline/thresholdとR2証跡は変更・再利用しない。

## R3-P1: 高さフォグの解析モデルとスナップショット契約を実装する
- status: done
- done-when: 有限な既定値と不正入力の安全な正規化を備えた高さフォグパラメータをSceneProxy/FramePacket経由でRenderThreadへ渡し、指数密度に対する水平・上昇・下降レイの解析透過率をCPUテストで固定する。Rendering層にRHI/Vulkan依存を追加しない。
- verify: `cmake --build build --config Debug --target VolumetricFogModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^VolumetricFogModelTest$"`
- paths: Library/Core/Public/Rendering/VolumetricFog.h, Library/Core/Private/Rendering/VolumetricFog.cpp, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/VolumetricFogModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R3-P2: 解析フォグとR2遠景空を描画パスへ接続する
- status: done
- done-when: `VolumetricsPass` がSceneColor/SceneDepthを読み、解析透過率で不透明描画を合成し、遠距離ではR2 SkyAtmosphere radianceへ連続的に収束する。Lighting後・Forward透明前の配置、fog無効時の恒等動作、named resource/descriptor境界を契約テストで固定する。新しい3D/RHI資源を作らない。
- verify: `cmake --build build --config Debug --target VolumetricsPassContractTest ForwardPassPipelinePlacementTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VolumetricsPassContractTest|ForwardPassPipelinePlacementTest)$"`
- paths: Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Assets/Shaders/volumetrics.frag, Test/Core/Rendering/VolumetricsPassContractTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R3-P3: 方向光の単一散乱をCSM遮蔽へ接続する
- status: done
- done-when: RenderWorldから設定した高さフォグがCoordinator経由でFramePacket.Sceneへ値コピーされ、固定24ステップの方向光散乱が既存の4カスケード深度配列を安全に参照する。遮蔽物の背後ではshaftが抑制され、非遮蔽領域ではライト方向に沿って現れる。影なし/不完全なCSM資源では既存のLightingPass fallbackを使って散乱だけを安全に抑制し、解析高さフォグとR1/R2の描画契約を保つ。
- verify: `cmake --build build --config Debug --target VolumetricsPassContractTest LightingParamsLayoutTest DirectionalShadowPassWiringContractTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VolumetricsPassContractTest|LightingParamsLayoutTest|DirectionalShadowPassWiringContractTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=shadowed-shafts`
- paths: Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Public/Rendering/RenderWorld.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Assets/Shaders/volumetrics.frag, Test/Core/Rendering/VolumetricsPassContractTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationApplication.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, blocked/R3-P3.md
- notes: FramePacket境界を守り、GameThreadの高さフォグ設定は既存SetSkyAtmosphereと同じRenderWorld→RenderingCoordinator経路で値コピーする。既定の無効状態は維持し、検証fixtureから明示的に有効化する。CSM不在時も既存のLightingPass fallback配列をdescriptorへ渡し、散乱だけを無効化して解析高さフォグを維持する。停止記録`blocked/R3-P3.md`は解消済みで、最終検証ログは`.harness/runs/20260920-r3-p3-final/`に保存する。

## R3-P4: フォグ密度・影・遠景空のGPU受入れを固定する
- status: done
- done-when: 3段階密度のGPU golden sweep、CSM遮蔽物あり/なしのA/B、遠方の空/地平線とR2大気のブレンドを専用R3証拠で検証する。R1/R2 baselineは不変とし、GPU性能は未計測・後続ゲートとして明記する。
- verify: `cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=shadowed-shafts`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=occluder-ab`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=distant-atmosphere-blend`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|LightingParamsLayoutTest|ForwardPassPipelinePlacementTest|VolumetricFogModelTest|VolumetricsPassContractTest|RenderingGoldenImageComparatorTest)$"`
- paths: Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/Baselines/RenderingValidation/R3*, Test/Core/Rendering/Thresholds/RenderingValidation/R3*, Docs/RenderingValidation/R3Acceptance.md, TASKS.md, PROGRESS.md
- notes: P3評価の非blocking指摘は、R3-P4で非遮蔽散乱上限と飽和画素0をR3専用閾値へ追加して解消した。独立評価はPASS。

### R5の選定と実行順

- R4のS4はDDGIとし、依存するR5を先行する。probe更新はcompute shaderのray queryを使い、R5初弾の不透明ハードシャドウはRT pipelineで実証する。
- BLAS/TLASの所有・更新は`GEngine`が所有するRayTracingSceneSubsystemへ集約する。RenderThreadはFramePacketの不変スナップショットだけを読む。
- RT非対応または無効時は既存ラスタ経路を維持する。R5はRT影までを対象とし、GI本実装とGPU性能ゲートは後続へ分離する。
- 方式の技術背景: [NVIDIA DDGI Integration Guide](https://developer.nvidia.com/blog/an-engineers-guide-to-integrating-ddgi/)

## R5-P1: Buffer Device Addressを独立して有効化する
- status: done
- done-when: RHIバッファでBDAを明示要求でき、対応デバイスでは要求したバッファだけが非ゼロのdevice addressを返す。未要求・非対応時は0を維持する。BDA/RHIのGPU検証と独立評価が通り、全体ビルド・全CTestの結果とR5-P1対象外の失敗がPROGRESS.mdに記録されている。
- verify: `cmake --build build --config Debug --target Game RHIBufferDeviceAddressVulkanTest -- /m:1`
- verify: `cmake --build build --config Debug --target ALL_BUILD -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIBufferDeviceAddressVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IBuffer.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Test/Core/Rendering/RHIBufferDeviceAddressVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P2: RT機能を任意機能として検出する
- status: done
- done-when: acceleration structure、ray query、RT pipelineの拡張とfeature chainを個別に照会し、利用可能な組合せだけを論理デバイスで有効化する。未対応・無効時はデバイス初期化を維持し、既存ラスタ描画へ戻る。
- verify: `cmake --build build --config Debug --target Game RayTracingCapabilityContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RayTracingCapabilityContractTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RenderingHdrIndoorSceneVulkanTest)$"`
- paths: Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Test/Core/Rendering/RayTracingCapabilityContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P3: RT shader stageをshadercへ接続する
- status: done
- done-when: RayGen/Miss/ClosestHit/AnyHit/Intersection/Callableの各stageがRHIからshadercへ一意に写像され、最小shader fixtureのコンパイルが全stageで成功する。
- verify: `cmake --build build --config Debug --target RHIRayTracingShaderStageCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingShaderStageCompileTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IShaderCompiler.h, Library/Core/Private/RHI/Vulkan/VulkanShaderCompiler.cpp, Test/Core/Rendering/RHIRayTracingShaderStageCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, Assets/Shaders/RayTracing/*, TASKS.md, PROGRESS.md

## R5-P4: 加速構造のRHI契約を定義する
- status: done
- done-when: BLAS/TLAS geometry・instance・build/update descriptorsと加速構造resourceがbackend-neutral APIで表現され、無効入力とRT非対応時の戻り値契約がテストで固定される。
- verify: `cmake --build build --config Debug --target RHIRayTracingApiContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingApiContractTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Public/RHI/IAccelerationStructure.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Test/Core/Rendering/RHIRayTracingApiContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P5: Vulkan BLAS構築を実装する
- status: blocked
- done-when: BDA対応vertex/index bufferから不透明triangle BLASを構築し、GPU queryで既知の交差/非交差を判定できる。RT無効時はresourceを公開せず、従来のmesh描画を維持する。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Public/RHI/IAccelerationStructure.h, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 実装と指定検証は完了。独立評価をClaudeのセッション上限で完了できず、受入れ状態は保留する。再開条件はblocked/R5-P5.md。

## R5-P6: TLASのinstance build/updateを実装する
- status: done
- done-when: BLAS instance配列からTLASを構築し、既知transformの変更をupdateまたはrebuildで反映する。2フレーム間のGPU query結果が移動後の解析位置と一致し、build/update間のbarrierと寿命が検証される。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Public/RHI/IAccelerationStructure.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Private/RHI/Vulkan/VulkanCommandList.*, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, TASKS.md, PROGRESS.md

- notes: Updateの適合性は容量だけでは判定できない。TLASはsourceの直近Buildで使った実instance数を記録し、Update入力との一致を検証する。

## R5-P7: RT pipelineとShader Binding Tableを実装する
- status: done
- done-when: RayTracing pipeline descriptorがshader groupsを記述し、Vulkan backendがraygen/miss/closest-hit groupとShader Binding Tableを生成する。最小pipelineを作成し、無効なgroup構成を拒否する。
- verify: `cmake --build build --config Debug --target RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingPipelineVulkanTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IPipeline.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Private/RHI/Vulkan/VulkanPipeline.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanRayTracingPipeline.*, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.*, Test/Core/Rendering/RHIRayTracingPipelineVulkanTest.cpp, blocked/R5-P7.md, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P8: TraceRaysをコマンドリストへ接続する
- status: done
- done-when: RHI ICommandListのTraceRaysがVulkanの各軸上限と総呼出し数上限以内のdispatchだけを記録し、同一deviceのTLAS descriptor binding/updateと最小raygen/miss/hit shaderを通じて既知triangleのhit/missをGPU readbackできる。readback両要素は未書込み番兵値から期待値へ更新される。descriptor bindingはnull・BLAS・foreign-device resource・別descriptor種を拒否し、回帰テストで固定する。
- verify: `cmake --build build --config Debug --target RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingPipelineVulkanTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanRayTracingPipeline.h, Assets/Shaders/RayTracing/RayTracingVisibilityRayGen.glsl, Assets/Shaders/RayTracing/RayTracingVisibilityMiss.glsl, Assets/Shaders/RayTracing/RayTracingVisibilityClosestHit.glsl, Test/Core/Rendering/RHIRayTracingPipelineVulkanTest.cpp, blocked/R5-P8.md, TASKS.md, PROGRESS.md

## R5-P9: GEngine所有のray-tracing sceneをFramePacketへ接続する
- status: done
- done-when: GEngine所有のRayTracingSceneSubsystemがdraw snapshot由来のmesh geometryとinstance transformからBLAS/TLASを管理し、RenderThreadはFramePacket以外のWorld/SceneViewを参照しない。生成・更新・破棄の寿命が契約テストを通る。
- verify: `cmake --build build --config Debug --target RayTracingSceneSnapshotTest RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RayTracingSceneSnapshotTest|RHIAccelerationStructureVulkanTest)$"`
- paths: Library/Core/Public/Engine/NorvesEngine.h, Library/Core/Private/Engine/NorvesEngine.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Library/Core/Private/Rendering/RayTracingSceneSubsystem.cpp, Test/Core/Rendering/RayTracingSceneSnapshotTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P10: RT pipelineでハードシャドウを描画する
- status: todo
- done-when: opaque fixtureでRT visibilityがLightingへ接続され、PCF/PCSSを無効にしたraster hard shadowとのA/BがR5専用閾値内になる。RT無効時は同一fixtureがraster結果を維持する。
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingRayTracingShadowVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=raster-rt-shadow-ab`
- paths: Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingShadowPass.h, Library/Core/CMakeLists.txt, Assets/Shaders/lighting.frag, Assets/Shaders/RayTracing/*, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P11: 動的TLAS更新とRT無効fallbackをGPU受入れする
- status: todo
- done-when: opaque occluder移動の複数フレームcaptureでTLAS更新後の影位置が解析範囲へ移り、RT無効化時はraster shadowへ復帰する。既存R1/R2/R3 baselineと全CTestに回帰がない。
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=dynamic-occluder-fallback`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Private/Rendering/RayTracingSceneSubsystem.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P12: R5の受入れ記録を確定する
- status: todo
- done-when: R5の設計選定、RHI/Vulkan/API変更、RT影A/B、動的TLAS、非対応fallback、検証ログ、性能gate保留をR5Acceptanceへ集約し、R4/DDGIを後続として記録する。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R5Acceptance.md, TASKS.md, PROGRESS.md

## R5-P13: VulkanTexture::Updateの同期失敗時staging資源を解放する
- status: todo
- done-when: EndSingleTimeCommandsの終了・送信・待機失敗時もVulkanTexture::Updateがstaging bufferとmemoryを確実に解放する。失敗経路を再現する検証と正常なtexture updateの回帰を通す。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHITextureUpdateVulkanTest$"`
- paths: Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Test/Core/Rendering/RHITextureUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
