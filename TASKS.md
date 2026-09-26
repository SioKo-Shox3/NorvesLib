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
# R3: ボリュメトリクス

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

# R4: プローブGI

### 採用する設計

- S4の選定はDDGI。手動配置の有限な3Dプローブ格子を使い、既定は無効、RT対応Vulkanでのみ更新する。RT非対応・無効・資源作成失敗時は既存のラスタ/IBL描画を維持する。
- RenderWorldからの設定はSceneProxy/FramePacketへ値コピーし、RenderThreadはそのフレームの不変スナップショットとR5のTLASだけを読む。プローブ資源と履歴はLightingPassがRenderThread上で所有する。
- 1 volume最大1024 probes、probeあたり64本の準一様rayを毎フレーム更新する。octahedral irradiance/distanceをprobeごとの8x8 array layerへ格納し、1 texel境界、scene-linear RGBA16F irradiance、RG16F距離1次/2次モーメントを使う。
- Probe hitはlinear BaseColorとemissive、既存LightProxyのdirectional/point/spot照明、直前のDDGI irradianceを使って拡散radianceを求める。missは有効な空/環境radianceを使い、無ければ黒とする。反射・透過・鏡面GI・probe relocation/classification/自動配置は対象外。
- Atlasはscene-referred linearで保存し、LightingPassの最終合成で現在フレームのpre-exposureを一度だけ適用する。Volume内部では既存diffuse IBLをDDGIで置換し二重加算を防ぐ。Volume外またはDDGI無効時は既存diffuse IBLを使う。
- 静的受入れは[Cornell大学Computer Graphicsの公開geometry/reflectanceと合成RGBE](https://bowers.cornell.edu/computer-graphics/data)を参照する。直接照明の白ROIから露出scaleを一度だけ決め、shadow-floor/red-bounce/green-bounceの平均Y相対誤差を各25%以内、red/green bounce ROIの優勢chroma比差を各0.10以内とする。R1/R2/R3のbaselineは変更しない。
- 動的受入れはライトまたは不透明物体を動かしてprobe更新を確認する。変更後8フレーム以内に間接光ROIの最終変化量の80%以上へ単調に収束する。GPU性能gateはRoadmapどおりDeferred。
- R5-P5の独立評価はPASSで完了した。R4はR5の既存GPU hit/miss経路を最初のray-query検証で再確認し、P5の受入れ完了を前提にDDGIへ接続する。

## R4-P1: DDGI volume設定と格子/方向変換のCPU契約を作る
- status: done
- done-when: DDGI volumeの有限値検証、1..1024 probeのchecked grid indexing、octahedral direction mapping、ray方向列と無効入力fallbackをCPU契約テストで固定する。既定volumeは無効である。
- verify: `cmake --build build --config Debug --target DDGIVolumeModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIVolumeModelTest$"`
- paths: Library/Core/Public/Rendering/DDGIVolume.h, Library/Core/Private/Rendering/DDGIVolume.cpp, Test/Core/Rendering/DDGIVolumeModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R4-P2: volume設定とray-hit材質をFramePacketへ値スナップショットする
- status: done
- done-when: RenderWorldで設定したvolumeとlinear BaseColor/emissiveがSceneProxy/FramePacketへ値コピーされ、packet clear後に参照が残らない。BaseColor既定値1は既存の描画を変えず、RHI未対応時の設定も無効化される。
- verify: `cmake --build build --config Debug --target DDGISnapshotContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGISnapshotContractTest$"`
- paths: Library/Core/Public/Rendering/DDGIVolume.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/RenderWorld.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Private/Rendering/RenderMaterialStore.h, Library/Core/Private/Rendering/RenderMaterialStore.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/DDGISnapshotContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: GameThreadからRenderThreadへのライブ参照は禁止し、加速構造・材質値・volume設定をFramePacketの値所有データで渡す。危険地帯の独立評価対象。

## R4-P3: compute ray queryでprobe rayのhit属性を取得する
- status: done
- done-when: LightingPassが記録する同一command listのTLAS build後にCompute shaderのray queryをdispatchし、FramePacketのTLASに対する既知のhit/miss、距離、instance/primitive属性を返す。frame slot再利用と欠落dispatchの番兵値readbackを検証する。RT無効時とDDGI資源作成例外時はdispatchを停止し、既存描画と同一SceneColorを維持する。
- verify: `cmake --build build --config Debug --target Game DDGIProbeRayQueryVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRayQueryVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Assets/Shaders/DDGI/ProbeRayQuery.comp, Test/Core/Rendering/DDGIProbeRayQueryVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: descriptorはRHI abstractionのCompute stageで宣言し、RenderingからRHI/Vulkanをincludeしない。Vulkan同期・GPU resource寿命・FramePacket境界は独立評価対象。

## R4-P3A: ray hitの拡散radianceを材質と直接光から計算する
- status: done
- done-when: ray hitの三角形normalとFramePacketのBaseColor/emissiveを使い、既存directional/point/spot lightの遮蔽付きLambert radianceをscene-linear ray結果へ保存する。missは設定済み空/環境radianceを使う。probe間接光の再帰寄与は次タスクで加える。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/DDGI/ProbeRadiance.comp, Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: GPU geometry/material寿命、direct-light単位、RT query同期は危険地帯の独立評価対象。

## R4-P4: ray結果をirradiance/distance atlasへ積分して更新する
- status: done
- done-when: probe ray hitで直前フレームのDDGI irradianceを1段だけ加算し、radiance ray結果をoctahedral irradiance/distance atlasへvisibility重み付きで積分する。1 texel borderとhysteresis 0.8を適用し、既知の単一平面/遮蔽ケースのGPU readbackで値・距離モーメント・境界を固定する。
- verify: `cmake --build build --config Debug --target DDGIProbeUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeUpdateVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Assets/Shaders/DDGI/ProbeIrradianceUpdate.comp, Test/Core/Rendering/DDGIProbeUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R4-P5: DDGI irradianceをLightingPassへ接続する
- status: done
- done-when: GBuffer world position/normalを使ってprobeをvisibility-weightedに補間し、active volume内でdiffuse IBLを置換して間接拡散項を加える。volume外・無効・RT非対応・不完全resourceでは既存diffuse IBLと同じ出力契約を保つ。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest RenderingDDGILightingContractTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderingDDGILightingContractTest|RenderGraphCompileTest)$"`
- paths: Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/lighting.frag, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/RenderingDDGILightingContractTest.cpp, Test/Core/Rendering/RenderGraphCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: direct lightingと現在のpre-exposureは変更せず、DDGI有効/無効、volume内/外、sky IBL二重加算を契約テストする。DDGIProbePassが無効化・失敗フレームで前frame-slotのatlas有効状態を残さないことも確認してからLightingPassへ接続する。危険地帯の独立評価対象。

## R4-P6: Cornell参照と動的更新を実GPUで受け入れる
- status: done
- done-when: 公開Cornell RGBEとgeometry/reflectanceを使うHDR captureで3つの間接ROIの平均Y相対誤差が25%以内、red/green bounceのchroma比差が0.10以内になる。ライト/occluder移動後は8 frame以内に最終間接ROI変化の80%以上へ単調に収束し、DDGI無効時は既存描画の比較閾値内で一致する。
- verify: `cmake --build build --config Debug --target RenderingDDGIVulkanTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingDDGIVulkanTest.exe --capture-source=scene-color --scenario=cornell-reference`
- verify: `build\Test\Core\Rendering\Debug\RenderingDDGIVulkanTest.exe --capture-source=scene-color --scenario=dynamic-update`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGIVulkanTest|RenderingGoldenImageComparatorTest)$"`
- paths: Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingDDGIVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/Baselines/RenderingValidation/R4*, Test/Core/Rendering/Thresholds/RenderingValidation/R4*, TASKS.md, PROGRESS.md
- notes: 20260922-r4-p7-final3でHDR scene-color captureを再検証した。Cornellのshadow/red/green ROI相対誤差は0.147881/0.152345/0.106696、red/green chroma差は0.0194377/0.0229986、DDGI有効A/Bのmean/max deltaは0.168949/6.5625、無効A/Bはmean/max delta=0/0、VUID_COUNT=0である。dynamic-updateは4 warmup sample後のred/green ROI平均を使い、FrameNumber差8時点のprogress=0.971325/0.820146で単調収束した。Cornell quadは従来どおり室内向きのvertex normalを保持し、GBufferのFrontFace::Clockwiseに合わせたラスタ巻き順をfixture内で明示した。P4期待値を現行のwrap重み床とhysteresisへ再基準化し、閾値・完了条件は変更していない。

## R4-P7: R4受入れ記録と独立評価を確定する
- status: done
- done-when: R4Acceptanceへ選定、scope、Cornell参照URL/asset hash/ROI閾値、動的更新結果、対象build/CTest/GPU captureの読戻しログ、既知の制限、性能gate保留を記録する。FramePacket/RHI/Vulkan/Rendering境界の独立評価blocking指摘を処理し、未処理はNEXT_FINDINGS.mdに残す。
- verify: `git diff --check`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DDGIVolumeModelTest|DDGISnapshotContractTest|DDGIProbeRayQueryVulkanTest|DDGIProbeUpdateVulkanTest|LightingParamsLayoutTest|RenderingDDGILightingContractTest|RenderingDDGIVulkanTest|RenderingDDGIVulkanDynamicTest|RenderingGoldenImageComparatorTest)$"`
- paths: Docs/RenderingValidation/R4Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: 20260922-r4-p7-final3でR4Acceptance、RGBE/threshold asset、実GPUログを確定した。対象Debug build、Cornell static/dynamic capture、R4指定9件CTestを再実行し、9/9 passed、VUID_COUNT=0を確認した。独立評価2周目で指摘された帳簿・行末・成果物追跡の不整合を修正し、最終成果物へ反映した。性能gateはDeferred、P4由来のvalidation陽性対照とhalf範囲上限はNEXT_FINDINGS.mdへ非blockingとして残す。

# R5: ハードウェアレイトレーシング基盤

### R5の選定と実行順

- R4のS4はDDGIとし、依存するR5を先行する。probe更新はcompute shaderのray queryを使い、R5初弾の不透明ハードシャドウはRT pipelineで実証する。
- BLAS/TLASの所有・更新は`GEngine`が所有するRayTracingSceneSubsystemへ集約する。RenderThreadはFramePacketの不変スナップショットだけを読む。
- RT非対応または無効時は既存ラスタ経路を維持する。R5はRT影までを対象とし、GI本実装とGPU性能ゲートは後続へ分離する。
- 方式の技術背景: [NVIDIA DDGI Integration Guide](https://developer.nvidia.com/blog/an-engineers-guide-to-integrating-ddgi/)

## R5-P1: Buffer Device Addressを独立して有効化する
- status: done
- done-when: RHIバッファでBDAを明示要求でき、対応デバイスでは要求したバッファだけが非ゼロのdevice addressを返す。未要求・非対応時は0を維持する。BDA/RHIのGPU検証と独立評価が通り、全体ビルド・全CTestの結果とR5-P1対象外の失敗がPROGRESS.mdに記録されている。
- verify: `cmake --build build --config Debug --target Game RHIBufferDeviceAddressVulkanTest -- /m:1`
- verify: `cmake --build build --config Debug --target RHIGPUTimestampVulkanTest RHIBufferDeviceAddressVulkanTest RHIAccelerationStructureVulkanTest RayTracingSceneSnapshotTest RHIRayTracingPipelineVulkanTest RayTracingCapabilityContractTest RHITextureToBufferReadbackVulkanTest RenderingValidationFixtureVulkanTest RHIImageLayoutVulkanValidationTest FrameCaptureFloatReadbackVulkanTest RenderingHdrSceneCaptureTest RenderingRayTracingShadowVulkanTest RenderingGoldenImageTest -- /m:1`
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
- status: done
- done-when: BDA対応vertex/index bufferから不透明triangle BLASを構築し、GPU queryで既知の交差/非交差を判定できる。RT無効時はresourceを公開せず、従来のmesh描画を維持する。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Public/RHI/IAccelerationStructure.h, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 20260922-r5-p5-resumeでDebug build exit 0、専用CTest 1/1 passed、直接GPU実行のBLAS/TLAS hit/missとresource寿命を確認した。独立評価はPASS。容量再問い合わせ、build-input usageの追加検証、行末整理はNEXT_FINDINGS.mdへnon-blockingとして残す。

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
- paths: Library/Core/Public/Engine/NorvesEngine.h, Library/Core/Private/Engine/NorvesEngine.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RayTracingSceneSnapshotTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P10: RT pipelineでハードシャドウを描画する
- status: done
- done-when: opaque fixtureでRT visibilityがLightingへ接続され、PCF/PCSSを無効にしたraster hard shadowとのA/BがR5専用閾値内になる。RT無効時は同一fixtureがraster結果を維持する。
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `cmake --build build --config Debug --target RHIGPUTimestampVulkanTest RHIBufferDeviceAddressVulkanTest RHIAccelerationStructureVulkanTest RayTracingSceneSnapshotTest RHIRayTracingPipelineVulkanTest RayTracingCapabilityContractTest RHITextureToBufferReadbackVulkanTest RenderingValidationFixtureVulkanTest RHIImageLayoutVulkanValidationTest FrameCaptureFloatReadbackVulkanTest RenderingHdrSceneCaptureTest RenderingRayTracingShadowVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingRayTracingShadowVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=raster-rt-shadow-ab`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- paths: Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingShadowPass.h, Library/Core/CMakeLists.txt, Library/Core/Public/RHI/RHITypes.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/lighting.frag, Assets/Shaders/RayTracing/*, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: 通常描画は既存のラスタ影を既定とし、RT影は明示設定またはR5検証モードで有効にする。単一visibility textureの適用対象は有効な方向光が一灯の構成に限る。

## R5-P11: 動的TLAS更新とRT無効fallbackをGPU受入れする
- status: done
- done-when: opaque occluder移動の複数フレームcaptureでTLAS更新後の影位置が解析範囲へ移り、移動後のRT影とraster影が比較可能な範囲で一致し、RT無効化時はraster shadowへ復帰する。R5 shaderのプリエクスポージャmodeをLightingParamsLayoutTestが期待値で検証し、全CTestに新規回帰がない。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=dynamic-occluder-fallback`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Assets/Shaders/RayTracing/RayTracingShadowRayGen.glsl, Assets/Shaders/RayTracing/RayTracingShadowMiss.glsl, Assets/Shaders/RayTracing/RayTracingShadowClosestHit.glsl, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md, blocked/R5-P11.md

## R5-P12: R5の受入れ記録を確定する
- status: done
- done-when: R5の設計選定、RHI/Vulkan/API変更、RT影A/B、動的TLAS、非対応fallback、検証ログ、性能gate保留をR5Acceptanceへ集約し、R4/DDGIを後続として記録する。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R5Acceptance.md, TASKS.md, PROGRESS.md

## R5-P13: VulkanTexture::Updateの同期失敗時staging資源を解放する
- status: done
- done-when: EndSingleTimeCommandsの終了・送信・待機失敗時もstaging bufferと転送先texture資源をGPU完了まで保持し、device teardownの非device-lost待機失敗ではallocator・command pool・device・instanceを破棄しない。失敗経路・teardown保護・Validation error検出と正常なtexture updateを検証する。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHITextureUpdateVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RHITextureUpdateVulkanTest.exe`
- paths: Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Test/Core/Rendering/RHITextureUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: VulkanDeviceのWaitIdleと共有command poolを使う単発コマンドは、同一device上で呼出側が直列化する。並行更新の内部同期は本タスクの対象外。

## SCENE-P1: 起動時の既定シーン経路を保持し、テストAABBを既定表示から外す
- status: done
- done-when: `GameApplicationHandler::CreateGameModeStateMachine` が従来どおり `Rendering3DTest` を開始し、`Rendering3DTestRoutine::Enter` が球・地面・岩・ライト・HDR環境を構成する経路を変更しない。既定フレームへ常時投入されていた検証用黄色AABBだけを外し、選択表示のAABBは維持する。保存済みのEditorレンダリングシーンを推測して別ファイルへ差し替えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingValidationSceneContractTest|SceneViewViewportCommandTest|WorldCameraSyncTest)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- verify: `Select-String Game.log -Pattern "Sphere Entity created and added to World|Ground Entity created and added to World|Boulder model loaded and added to World|Environment source and derived IBL resources created|exit-after-rendered-frames reached"`
- stop-when: 実行ログに既定シーン構成または120フレーム終了の証拠がなく、保存済みのレンダリングシーン源を確認できない場合は、別のシーンを発明して起動経路へ接続せず、未実装の永続化入口を後続タスクとして記録する。
- paths: Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, TASKS.md, PROGRESS.md

## AUDIT-RM-P1: RenderingRoadmapと実装・受入れ履歴の整合を監査する
- status: done
- done-when: R0〜R8のRoadMap依存関係・ステータス表・完了コミットを、追跡対象のコード、受入れ記録、PROGRESS.md、完了トレーラーと突き合わせる。R0〜R5の実装済み範囲、RoadMap表の遅れ、R6以降の着手入口、保存済みレンダリングシーンの有無を明示し、RoadMap本体を変更せず追跡対象の監査記録へ固定する。
- verify: `git check-ignore -v Docs/Plans/RenderingRoadmap.md`
- verify: `git log --all --format="%H%n%s%n%b%n---" --grep="RenderingRoadmap:"`
- verify: `git diff --check`
- stop-when: RoadMapの依存関係または完了証拠が現行履歴から再構成できない場合は、未確認のフェーズを完了扱いにせず、不足証跡を監査記録へ残す。
- paths: Docs/RenderingValidation/RenderingRoadmapAudit.md, TASKS.md, PROGRESS.md

## R6A-M1: velocityの方式と検証契約を定義する
- status: done
- done-when: R6-aのvelocity符号、device用clip行列、初回/無効履歴のゼロ契約、R16G16_FLOATと`GBuffer_Velocity`、FramePacketでのカメラ/オブジェクト履歴、初回対象経路、完了条件、停止条件を追跡対象の設計文書へ固定する。
- verify: `git diff --check`
- verify: `rg -n "currentUV - previousUV|GBuffer_Velocity|FramePacket|停止条件" Docs/RenderingValidation/R6aVelocityPlan.md`
- paths: Docs/RenderingValidation/R6aVelocityPlan.md, TASKS.md, PROGRESS.md

## R6A-P1: velocityのFramePacket履歴とGBuffer出力を実装する
- status: done
- done-when: 通常の遅延不透明MeshProxyについて、現在/前フレームのオブジェクト行列とメインカメラをFramePacketだけでRenderThreadへ渡し、既存GBuffer 4面の契約を維持したまま`GBuffer_Velocity`へR16G16_FLOATのvelocityを書き出す。移動後停止したMeshProxyのvelocityが次フレームでゼロになり、カメラのみ/物体のみ/併用の既知点を解析投影値の0.002以内でreadbackできる。物体のみは対象外背景がゼロで、初回無効履歴と安定静止を別frameで検証し、起動経路とRendering3DTestのシーン構成を変更しない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(FrameCaptureReadbackHelperTest|FramePacketManagerTest|RenderFrameExecutorPlanTest|MeshBatcherTest|MeshBatcherInstancingTest|InstanceDataFlattenTest|GBufferMaterialDescriptorCacheTest|SceneViewViewportCommandTest|BoardTransformTest|CanvasViewRenderTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RenderGraphNamedResourceTest|RenderGraphAttachmentStateTest|WorldCameraSyncTest|PhysicsArchitectureContractTest|PhysicsFixedStepPipelineTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure -V --no-tests=error -R "^RenderingVelocity(Static|Motion|Camera|Object|FirstFrame|MoveThenStop)VulkanTest$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- verify: `rg -n "CreateGameModeStateMachine|Sphere Entity created and added to World|Ground Entity created and added to World|Boulder model loaded and added to World|Environment source and derived IBL resources created|exit-after-rendered-frames reached" Game.log`
- stop-when: 既存のRHI/RenderGraph境界を保ったままvelocity attachmentを作成できない、またはGPU readbackで解析契約を観測できない場合は、RTGIや別起動経路へ拡張せずAPI不足を記録する。
- paths: Library/Core/Public/Component/MeshComponent.h, Library/Core/Private/Component/MeshComponent.cpp, Library/Core/Private/Object/World.cpp, Library/Core/Public/Rendering/MeshTypes.h, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/DrawCommand.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/GBufferPass.h, Library/Core/Private/Rendering/GBufferPass.cpp, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Library/Core/Public/Rendering/FrameCaptureTypes.h, Library/Core/Private/Rendering/RenderFrameExecutor.cpp, Library/Core/Private/Rendering/FrameCaptureReadbackHelper.cpp, Assets/Shaders/gbuffer.vert, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingVelocityVulkanTest.cpp, Test/Core/Rendering/RenderGraphCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Modules/Physics/PhysicsArchitectureContractTest.cpp, Test/Modules/Physics/PhysicsFixedStepPipelineTest.cpp, Docs/RenderingValidation/R6aVelocityPlan.md, Docs/Rendering/R6aVelocityAcceptance.md, TASKS.md, PROGRESS.md

## R6-M1: RTGIとテンポラルデノイザの方式を選定する
- status: done
- done-when: RoadMapのR6本体について、RTGIの1〜2バウンス方式、ray query/RT pipelineの用途分担、テンポラル蓄積、デノイザ方式、履歴寿命、固定フレーム数ウォームアップ、R6性能gate保留を選定記録へ固定する。R6-aのvelocity契約とR5のRT基盤を前提にし、R7/R8の未実装機能を先取りしない。
- verify: `cmake --build build --config Debug --target Game RHIRayTracingPipelineVulkanTest -- /m:1`（exit 0）
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIRayTracingPipelineVulkanTest|RenderingVelocityCameraVulkanTest)$"`（2/2 passed）
- verify: `git diff --check`
- verify: `rg -n "RTGI|デノイザ|R6-a|ウォームアップ|性能" Docs/RenderingValidation/R6TechniquePlan.md`
- stop-when: R6本体の方式選定がR5/R6-aの公開境界やR7のパストレーサー実装を要求する場合は、境界を越えず未決定事項として記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, TASKS.md, PROGRESS.md

## R6-P1: RTGI結果形式とfallback契約を実装する
- status: done
- done-when: R6の1 bounce diffuse出力、ray-query capability、TLAS snapshot、FramePacketのscene/light revision、履歴resourceのcurrent/history公開、R4 DDGI/既存IBL fallbackをbackend-neutralな契約として接続し、RT無効・資源失敗時に既定rasterを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest)$"`
- stop-when: R5のTLAS所有権またはR6-aのFramePacket/velocity契約を変更しないと接続できない場合は、必要なAPI不足を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P1-FIX: revisionとRTGI履歴契約の意味論を修正する
- status: done
- done-when: SceneRevisionが毎フレームの物体変換・PreviousWorldではなくシーン構成の変更を表し、前フレーム履歴のrevision差を動的移動中の即時fallback条件にしない。履歴のrevision差はR6-P3の棄却・weight抑制へ渡せる形で保持し、構成変更時は履歴を不採用にする。履歴revisionが1つ前でもRTGI選択が維持され、構成変更では不採用となる契約テストを追加する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest|RenderingVelocityCameraVulkanTest)$"`
- stop-when: R5のTLAS所有権またはR6-aのvelocity符号・FramePacket値所有を変更しないと修正できない場合は、必要な境界差分を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P2: 1 bounce ray-query GIを接続する
- status: done
- done-when: compute shader内のray queryでTLAS hit/missを処理し、有限なdiffuse indirect radianceをLightingPassへ渡す。RT非対応・RT無効・TLAS不完全・dispatch失敗時はR4 DDGIまたは既存IBLへ戻り、R5 RT pipeline/SBTを変更しない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DDGIProbeRayQueryVulkanTest|RenderingRayTracingShadowVulkanTest)$"`
- stop-when: ray queryのscene snapshotとR5のTLASを共有できず、RT pipelineの新規SBT設計が必須になった場合はR7境界として未決定へ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P1-FIX-2: SceneRevisionをカリング・ソート非依存へ修正する
- status: done
- done-when: SceneRevisionがカリング済みDrawCommandや奥行きソート順、物体変換、UI表示順に依存せず、全MeshProxy/SkinnedMeshProxyの構成集合と材質・環境設定だけで決まり、カメラ移動・物体移動・同一集合の並べ替えでは変化しない。構成追加・削除・メッシュ/材質変更では変化する性質テストを追加する。R6-P1の履歴revision契約とFramePacket値所有を維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest|RenderingVelocityCameraVulkanTest)$"`
- stop-when: 全proxy集合をFramePacketへ値コピーする既存境界だけでは構成集合を観測できない場合は、RenderThreadからWorldを参照せず不足するsnapshot項目を記録して停止する。
- paths: Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/RenderingDDGILightingContractTest.cpp, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P2-FIX: RTGI陽性経路をGPU readbackで検証する
- status: done
- done-when: RTGI capability・完全TLAS・出力textureを設定した専用GPUテストがLightingPassのray-query computeを実行し、hit/miss結果のfinite radiance、RTGI公開状態、RTGI無効・TLAS不完全時の既存間接光fallbackをreadbackで反証可能にする。既存R5 RT pipeline/SBTとRendering3DTest起動経路を変更しない。
- verify: `cmake --build build --config Debug --target Game RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|DDGIProbeRayQueryVulkanTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `glslangValidator -V --target-env vulkan1.2 -S comp Assets/Shaders/RTGI/DiffuseIndirect.comp -o build/RTGI-DiffuseIndirect.spv`
- stop-when: テストfixtureからLightingPassのRTGI入力を公開できず実GPU陽性経路を観測できない場合は、fallbackだけを合格にせず必要なtest seamを記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders/RTGI, Test/Core/Rendering/RTGIDiffuseIndirectVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P3: velocity再投影と8フレーム履歴を実装する
- status: done
- done-when: `previousUV = currentUV - velocity` を使うcurrent/history ping-pong、depth/normal/material/revision棄却、age上限8、confidence、light revisionの2フレームweight制限、resize/初回/TLAS失敗時の無効化を実装する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingVelocityCameraVulkanTest|RenderingVelocityObjectVulkanTest|RenderGraphCompileTest)$"`
- stop-when: velocityの符号またはFramePacketの値所有を変更しないと履歴が成立しない場合は、R6-a契約との差分を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P4: 3x3 cross-bilateralデノイザを実装する
- status: done
- done-when: テンポラル出力へdepth/normal/material境界を越えない3x3 cross-bilateral filterを1回適用し、history confidenceをweightへ反映し、pre-exposureを二重適用せずLightingPassへ合成する。外部NRDや複数段SVGFを導入しない。
- verify: `cmake --build build --config Debug --target Game LightingParamsLayoutTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RTGIDiffuseIndirectVulkanTest)$"`
- stop-when: 既存GBuffer/Lightingの公開境界を壊さずfilterへ入力できない場合は、追加attachmentを推測で増やさず契約不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: テンポラルconfidenceをcross-bilateral weightへ反映し、RTGI GPUテストで公開・hit/miss・fallbackを確認した。出力textureの直接readbackと遠景depth閾値の確認はNEXT_FINDINGS.mdへ記録した。

## R6-P5: 動的GIとfallbackのGPU受入れを固定する
- status: done
- done-when: 固定8 rendered-frame warmup後の静止golden、カメラ/物体移動、ライト移動4フレーム以内の追従、RT無効/R4/IBL fallback、履歴棄却と移動後停止をGPU readbackまたはHDR captureで検証する。既定のRendering3DTest起動経路は不変とする。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 既定シーンの球・地面・ライト球・方向ライト・boulder・HDR環境を失う変更が必要になった場合は、シーン起動経路を戻してfixtureを別経路へ分離する。
- paths: Test/Core/Rendering, Assets/Shaders, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-09-23の独立評価（NEEDS_WORK）で再開。(a) `Assets/Shaders/RTGI/DiffuseIndirect.comp`のレイ方向がpixel座標だけで決まり静止中に同じレイを再評価するため、rendered frameごとに決定論的に異なるサンプルへ直す。(b) ライト追従率の分母を収束後の変化量にし、間接光ROIで4 rendered frame以内の80%到達を判定する。(c) 履歴棄却を履歴age/confidenceのreadbackなど棄却そのもので反証する。静止収束画像の参照比較はR6-P5-REFで行う。

## R6-P5-REF: 静止収束画像をR7自前PT参照と知覚diffで比較する
- status: done
- done-when: R6受入れと同じ解像度・camera・geometry・material・light・HDR環境・exposure/pre-exposureで、R6 RTGIの静止収束SceneColorとR7 PTの収束SceneColorを事前に固定したFLIP pool/max-pixel閾値で比較し、結果と閾値根拠を`R6Acceptance.md`へ記録する。R7-P3Dの起動時PT選択と同一シーン比較の仕組みを使う。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIPathTracingReferenceVulkanTest$"`
- stop-when: 閾値超過が出た場合はRoadmap更新ルール5に従いR6を再オープン扱いにし、閾値やgoldenを緩めない。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md


## TEST-RGC: RenderGraphCompileTestのShadowMapPass初期化失敗を直す
- status: done
- done-when: `RenderGraphCompileTest`の`TestShadowMapNativeDeclareImportsDepthOutput`でFakeDevice上の`ShadowMapPass::Initialize`が失敗する原因（R2のCSM配列深度導入以降の既存失敗）を特定し、テストの契約を弱めずに直す。R6/R7の変更前から失敗していることは`c1474fc`時点の再実行で確認済み。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: 失敗がShadowMapPassの実装不具合ではなくFakeDeviceの不足による場合は、fakeへ必要な振る舞いを足し、ShadowMapPassの実資源検査を緩めない。
- paths: Test/Core/Rendering/RenderGraphCompileTest.cpp, Library/Core/Private/Rendering/ShadowMapPass.cpp, TASKS.md, PROGRESS.md

## R8-P1: ACES 2.0 SDRの出力変換を3D LUTへ焼き込み、OCIOの基準画像を作る
- status: done
- done-when: `Scripts/BakeAcesOutputLut.py` がOCIO（pipの`opencolorio`、2.4以上の組み込みACES 2.0 studio config）で、scene-linear Rec.709（D65）からdisplay「sRGB - Display」・view「ACES 2.0 - SDR 100 nits (Rec.709)」への変換を、log2 shaper（範囲をスクリプトの定数と記録に固定）付きの65³ RGBA16F LUTへ焼き、`Assets/ColorManagement/` に置く。LUTの値はsRGBの符号化を外したdisplay-linearにする（既存のsRGB提示をそのまま使う）。同じスクリプトが決定論的なHDR試験チャート（露出段のグレー・原色/補色・肌/空の色票）と、そのOCIO厳密変換の基準画像（`Test/Core/Rendering/Baselines/RenderingValidation/`）を作る。`--verify` は再生成がbyte一致し、LUT補間（CPU、三線形）とOCIO厳密変換の差がチャートの全画素でsRGB符号化後2/255以内なら0で終わる。OCIOの版・config名・shaper範囲・SHA-256とS8の決定（ベイクLUT）を `Docs/RenderingValidation/R8ColorManagement.md` に記録する。
- verify: `python Scripts/BakeAcesOutputLut.py --verify`
- stop-when: OCIOの組み込みconfigにACES 2.0のSDR viewがない、またはpipで入らない場合は理由を記録してユーザーへ戻す。
- paths: Scripts/BakeAcesOutputLut.py, Assets/ColorManagement/*, Test/Core/Rendering/Baselines/RenderingValidation/R8Aces*, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 2026-09-26 ユーザー承認のR8計画（S8=ベイクLUT、1280×720・1024spp、フィルムグレインは既定オフで入れる、連番の検査はScripts＋短いCTest）。pipでopencolorioを入れてよい（ユーザー承認済み）。承認済みgolden・閾値は変えない。既定の起動（Rendering3DTest）とトーンマップの既定は変えない。

## R8-P2: 3D textureのLUTでACES 2.0 SDRへ変換するトーンマップを加える
- status: done
- done-when: `ToneMappingPass` に新しい演算子（ACES 2.0 SDRのLUT）を加え、R8-P1のshaperで3D texture（RHIの`Texture3D`をGPUで初めて標本化する）を引く。この演算子では既定のグレーディング（Contrast/Saturation）を掛けない。起動引数（例 `--tone-map=aces20-lut`）で選べ、既定の演算子は変えない。新しいGPUテストがR8-P1の試験チャートをSceneColorとしてpassへ入れ、ToneMappedColorをOCIO基準画像とsRGB符号化後2/255以内で一致させる（閾値は比較の前に固定）。Indoor/Outdoor goldenは変わらない。
- verify: `cmake --build build --config Debug --target Game R8AcesLutToneMappingVulkanTest ToneMappingParamsLayoutTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8AcesLutToneMappingVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 承認済みgoldenが変わる場合、または2/255に収まらずLUTの解像度・shaperを変える必要がある場合は、測定値を記録してユーザーへ戻す。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/RHI/*, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Test/Core/Rendering/R8AcesLutToneMapping*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHIの3D texture）。評価者を通す。Rendering層からRHI/Vulkanをincludeしない。

## R8-P3: PTの連番の1フレームを、前後のカメラ・変換とシャッター区間を固定して累積する
- status: done
- done-when: PTに連番の1フレームを描く経路を加える。1フレームの累積（1 spp × N dispatch）の間、前後のカメラ・instance変換と、シャッター区間の基準の長さ（起動引数のフレーム長、例 1/24 s。実時間のDeltaTimeを使わない）を固定し、dispatchごとにレンズとシャッター時刻を引き直す。起動引数で絞り（f値）・焦点距離（focus distance）・シャッター時間・フレーム長を指定できる。新しいGPUテストが、既知の速度で動く物体の動きぼけの幅がシャッター時間×速度に1 px以内、焦点外の点のCoCが解析値（`ComputePathTracingCocPixels`）に2%以内で一致し、累積の途中で履歴が消えず、再実行でbyte一致することを確かめる。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: 既存のR6/R7のPT参照比較の結果が変わる場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Public/Rendering/PathTracingCamera.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8PathTracingSequenceFrame*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・PT・FramePacket）。評価者を通す。GameThread→RenderThreadはFramePacketのsnapshot越しだけ。

## R8-P3-FIX: 連番の1フレームの累積を、同じフレーム内のinstanceの並び替えで捨てない
- status: todo
- done-when: R8-P3の評価（反復5）の指摘を直す。`HashPathGeometry`はinstanceの配列順で前後の変換・材質・customIndexを署名にするため、物体ごとの状態が同じでも同じSequenceFrameの中で`P,A,B`→`P,B,A`と並びが変わると累積が捨てられる。連番の経路のRTスナップショットを安定した物体キーの順に揃え、customIndex・材質texture表・発光instance表を整合させる（または署名を順序に依らない形にし、光源標本の表の順序も揃える）。`R8PathTracingSequenceFrameVulkanTest`へ、SequenceFrameと各物体の前後変換を固定したまま並びを変えた2回のdispatchで試料数が1→2と続き、固定順で描いた画像とbyte一致する検査を加える。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest)$"`
- stop-when: 並びの正規化がR6/R7のPT参照比較の結果を変える場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Test/Core/Rendering/R8PathTracingSequenceFrame*, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・RenderThread）。評価者を通す。

## R8-P4: ラスタの被写界深度をPTの薄レンズ参照と比べる
- status: todo
- done-when: ラスタに被写界深度のpass（深度からCoCを求め、PTと同じ薄レンズの式・撮像面高24 mm・FOVから焦点距離）を加える。focus distanceが0（ピンホール）なら働かず、承認済みgoldenは変わらない。新しい比較テストが、手前・焦点面・奥に物体を置いた静止シーンで、ラスタのDoFとPT参照（R8-P3の経路、独立な3組の画素ごとの中央値）を比べる。閾値は比較の前に固定する規則で作る: PT参照と、f値を±20%変えたPTとの知覚差（FLIP平均・一致画素の画素単位最大・8×8区画最大）のうち小さい方。±40%の変化が3つとも閾値の外にあることを比較のたびに確かめる。
- verify: `cmake --build build --config Debug --target Game R8DepthOfFieldPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8DepthOfFieldPathTracingReferenceVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー判断、`blocked/R8-P4.md`の選択肢A→B）まず前景と背景を別々のCoCの層に分けるgather（Jimenez 2014系: タイルごとの最大CoC、前景の縁の外側と背景の穴を背景の層を広げた値で埋める）へ作り直して比べ直す。それでも規則の閾値を超える場合は、R7の太陽の可視の除外と同じ考え方で、薄レンズのPTがピンホールの像に無い面を見る画素（PTの薄レンズの1次命中がピンホールの可視面と一致しない画素と、ピンホールで未命中の画素の近傍）を一致画素の画素単位最大と8×8区画最大の判定から除いて数を記録し、除外の外に置いた局所欠陥を検出する負の対照を比較のたびに確かめる。閾値の規則は変えない。この後もFLIP平均が閾値を超える場合、または除外が画像の大部分に及ぶ場合は、測定値を記録してユーザーへ戻す。
- paths: Assets/Shaders/*DepthOfField*, Library/Core/Public/Rendering/*DepthOfField*, Library/Core/Private/Rendering/*DepthOfField*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Component/CameraComponent.h, Library/Core/Private/Component/CameraComponent.cpp, Test/Core/Rendering/R8DepthOfField*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 取得（PTの3組の中央値と±20%/±40%の変種、約20分）は1反復で1回までにし、shaderやCPU比較の変更は取得済みのダンプに対する`--compare-dumps`だけで評価する（shaderは実行時に読む）。取得をやり直すのは、pass本体やC++の変更で画像が変わるときだけ。差の分類と現状は`blocked/R8-P4.md`。

## R8-P5: ラスタの動きぼけをPTのシャッター参照と比べる
- status: todo
- done-when: ラスタに動きぼけのpass（R6-aのvelocityにシャッター時間/フレーム長を掛け、空の画素はカメラの動きから求める）を加える。シャッター時間が0なら働かず、承認済みgoldenは変わらない。新しい比較テストが、カメラのpanと既知の速度で動く物体のシーンで、ラスタの動きぼけとPT参照（R8-P3の経路、3組の中央値）を比べる。閾値の規則はR8-P4と同じで、シャッター時間を±20%変えたPTを物差しにし、±40%が閾値の外にあることを確かめる。
- verify: `cmake --build build --config Debug --target Game R8MotionBlurPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8MotionBlurPathTracingReferenceVulkanTest|RenderingVelocityObjectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 規則で作った閾値に収まらない場合は差の分類を記録してユーザーへ戻す。閾値や規則を変えない。
- paths: Assets/Shaders/*MotionBlur*, Library/Core/Public/Rendering/*MotionBlur*, Library/Core/Private/Rendering/*MotionBlur*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/R8MotionBlur*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R8-P6: フィルムグレインを既定オフで加える
- status: todo
- done-when: 出力変換の後（display空間）に、画素・フレーム番号・seedから決まるフィルムグレインを加える。強さは起動引数で指定し、既定はオフ（承認済みgoldenは変わらない）。新しいGPUテストが、オフでは出力が従来と一致し、オンでは中間グレーの平均の変化が0.5/255以内・標準偏差が指定の±10%以内、隣のフレームで模様が変わり、同じフレームの再実行でbyte一致することを確かめる。
- verify: `cmake --build build --config Debug --target Game R8FilmGrainVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8FilmGrainVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8FilmGrain*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R8-P7: EXR連番の検証exeを作る
- status: todo
- done-when: C++の検証exe（TinyEXRとThirdPartyのFLIPを使う）が、連番のdirectoryと期待するフレーム数から、欠番・寸法の不一致・NaN/Inf画素（フレームと座標を出す）を検出し、R8-P1のLUT（CPU）でdisplayへ変換した隣接フレームのLDR-FLIP平均を並べ、中央値の3倍（かつ下限0.01）を超える組をポッピングとして失敗にする。規則は検査の前に固定する。単体テストが合成の連番で、正常な連番は合格、欠番・NaN・寸法違い・差し込んだ1フレームの跳びをそれぞれ検出することを確かめる。
- verify: `cmake --build build --config Debug --target R8ExrSequenceValidator R8ExrSequenceValidatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8ExrSequenceValidatorTest$"`
- stop-when: FLIPやTinyEXRの取り込みで標準ライブラリ型の規則に反する変更が要る場合は、ThirdPartyの閉じ込め方を記録してユーザーへ戻す。
- paths: Test/Core/Rendering/R8ExrSequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R8-P8: 屋内・屋外の決定論的なアニメーションとEXR連番の書き出し、8フレームのCTestを加える
- status: todo
- done-when: 検証アプリのhandlerが、フレーム番号iから時刻 i/24 のカメラと物体の変換を決め（屋内: Cornellで箱が滑りカメラがdollyする。屋外: 空・霧の屋外シーンでカメラが回り球が動く）、R8-P3の経路（絞り・シャッター1/48 s・フレーム長1/24 s）で各フレームを累積し、`WritePathTracingExrFrame`で書き出し、manifest（シーン・解像度・spp・フレーム範囲・seed）を残す。起動引数でシーン・フレーム範囲・解像度・spp・出力directoryを指定できる。CTest `R8SequenceSmokeTest` が屋内・屋外それぞれ8フレームを256×144・64 sppで書き、R8-P7の検証exeに合格する。
- verify: `cmake --build build --config Debug --target Game R8SequenceRenderer R8ExrSequenceValidator -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8SequenceSmokeTest$"`
- stop-when: 8フレームの連番で検証exeが跳びを検出し、アニメーションでは直せない場合は原因を記録してユーザーへ戻す。
- paths: Test/Core/Rendering/R8Sequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, Library/Core/Public/Rendering/PathTracingExrOutput.h, Library/Core/Private/Rendering/PathTracingExrOutput.cpp, TASKS.md, PROGRESS.md
- notes: GameThread→RenderThreadはFramePacket越しだけ。

## R8-P9: 屋内の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: todo
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene indoor` が屋内の240フレーム（24 fps・10秒）を1280×720・1024 sppで`build/R8Sequences/indoor/`へ書き、R8-P7の検証exeで欠番0・NaN/Inf画素0・ポッピング0を確かめ、所要時間・容量・検証結果を`.harness/runs/<日付>-r8-sequences/`へ残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene indoor`
- stop-when: 1フレームの描画が想定（数秒〜十数秒）を大きく超えて1反復に収まらない場合は、分割書き出しの方法を記録してユーザーへ戻す。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md

## R8-P10: 屋外の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: todo
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene outdoor` が屋外の240フレームを同じ条件で`build/R8Sequences/outdoor/`へ書き、欠番0・NaN/Inf画素0・ポッピング0を確かめ、記録を残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene outdoor`
- stop-when: R8-P9と同じ。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md

## R8-P11: R8の受入れ記録を確定する
- status: todo
- done-when: `Docs/RenderingValidation/R8Acceptance.md` に、ACES基準画像との一致（R8-P2）、DoF・動きぼけのPT参照比較（R8-P4/P5）、フィルムグレイン（R8-P6）、屋内・屋外の240フレームの連番の検査（R8-P9/P10）の結果とログ、既知の制限、GPU性能はDeferredを記録し、完了コミットの本文末尾に `RenderingRoadmap: R8 complete` trailerを付ける。PROGRESS.mdに完了を記録する。
- verify: `git diff --check`
- stop-when: R8-P1〜P10のどれかがdoneでない場合はtrailerを付けず残課題を記録する。
- paths: Docs/RenderingValidation/R8Acceptance.md, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 危険地帯を含む機能の完了判定。評価者を通す。

## TEST-SKINNED: SkinnedRenderPathContractTestの停止を直す
- status: todo
- done-when: `SkinnedRenderPathContractTest`がCPU 0のまま戻らない（2026-09-24にctest 1350秒で強制終了）原因を特定し、契約を弱めずに完走させる。R5-P12時点でもpending件数assertで失敗していた既存問題として扱う。
- verify: `cmake --build build --config Debug --target SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 300 -R "^SkinnedRenderPathContractTest$"`
- stop-when: 停止がスキニングの実装不具合ではなくテストの待機条件による場合は、待機条件を明示的な上限付きにし、検証内容を減らさない。
- paths: Test/Core/Rendering/SkinnedRenderPathContractTest.cpp, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md

## R6-P7: RTGIで発光三角形を光源標本する
- status: done
- done-when: RTGIの1次面と命中点の直接光に、レイトレーシングシーンの発光三角形の光源標本（影の問い合わせ付き）を加え、面光源の直接光と面光源に照らされた面からの1バウンスを雑音の少ない推定にする。`R6RTGIPathTracingReferenceVulkanTest`がR6-P5-REFで固定した閾値（間接光±20%の物差し）内に入り、R6受入れと既存のRTGI・DDGI・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R6RTGIPathTracingReferenceVulkanTest R6RTGIAcceptanceVulkanTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R6RTGIAcceptanceVulkanTest|RTGIDiffuseIndirectVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR6-P5-REFの閾値を変えないと通らない場合は、方式の再選定としてユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI shader・LightingPass・RenderThread）。R6-P5-REFで発見。ラスタは発光三角形を光源として扱わず、面光源の直接光がRTGIの偶然の命中だけで入るため斑点状の雑音になる。
- result: 光源標本（`128294a`、評価対応`02eaa7e`）、RTGIへSSAOを重ねない（`81bbf24`）、デノイズの外れ値抑制（`3f4f39c`）、PTの画素中心標本（`a15c43a`）に加え、ユーザーの判断で画素単位最大を幾何が一致する画素で判定し直接光を解析BRDFで揃え（`c162366`・`f7d0b59`・`775261a`）、静止時だけRTGIの履歴を延長し年齢に応じてデノイズの近傍の重みを下げた。R6参照比較は平均0.0466・一致画素の最大0.150・区画0.100で閾値内（閾値と物差しは不変）。記録は`Docs/RenderingValidation/R6Acceptance.md`の「R6-P7の完了」。

## FIX-NEURAL-BRDF-STREAK: ニューラルBRDFの直接光が点光源の近くに作る筋を直す
- status: todo
- done-when: 通常表示の直接光（ニューラルBRDF）と解析BRDFの差が、Cornellの天井のように点光源に近い粗い面でも筋を作らない。原因（学習範囲外の入力、かすめ角の鏡面項など）を特定し、学習データか評価の範囲を直すか、範囲外では解析BRDFへ戻す。
- verify: 同じCornell条件で、ニューラルBRDFの通常表示と解析BRDF（検証mode 254の直接光）の画素差を比べる専用テストを追加して通す。
- stop-when: 学習済み重みの再生成が必要でデータがない場合は、範囲外の入力で解析BRDFへ戻す方針をユーザーへ提案する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。点光源だけの画像でラスタにだけ2画素幅の筋が出る。

## FIX-SSAO-ROOM-SCALE: 部屋の大きさのシーンでSSAOが壁をほぼ全遮蔽にする原因を直す
- status: todo
- done-when: Cornell（5.5 m四方）の壁でSSAOがほぼ0になる原因（半径・bias・深度の再構成の尺度など）を特定して直し、IBL fallbackの間接光が壁で消えない。既存goldenは変えない。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenが変わる場合は基準の更新を提案して止まる。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。RTGIには`81bbf24`でSSAOを掛けないようにしたが、IBL fallbackは引き続きSSAOを掛ける。

## TEST-R6-RESIDUAL-TIMING: R6停止残留の判定が起動の早さで変わる原因を直す
- status: done
- done-when: `R6RTGIAcceptanceVulkanTest`の停止残留（`R6_STOP_RESIDUAL_CHECK`）が、単体実行とRenderingValidationラベル内の実行で同じ物体影響の大きさになり、ラベル全件を3回続けて実行して毎回通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIAcceptanceVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 物体影響の差がRTGI/DDGIの実装の不具合（履歴や更新順）による場合は、テストの待ち時間で隠さずR6の不具合として記録し直す。
- paths: Test/Core/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。初回取得は資産読み込みの完了（壁時計に依存）で始まり、以降の段階はフレーム数で進む。単体実行は最初の段階がframe 153前後で物体影響0.018〜0.06（残留比0.001〜0.003）、ラベル内はframe 76前後で物体影響が約1.1e-4まで落ち、残留比が閾値0.5付近（R7-P3Cのラベル実行で0.477、R7-P3Dで0.506）になる。R6-P6の前に直す。
- result: 物体影響の差はRTGIの不具合（面光源の直接光がRTGIの偶然の命中だけで入る）によるもので、R6-P7の光源標本で解消した。テストの待ち時間は変えていない。単体実行2回は最初の段階がframe 172〜175・物体影響0.0251〜0.0280、RenderingValidationラベル全件の3回連続実行は各回frame 166〜177・物体影響0.0267〜0.0289・残留0.0058前後（残留比約0.2、閾値0.5）で毎回通過した。ラベルの2・3回目に失敗した1件は、実行中に追加した未完成のR4再照合テスト（R4-REOPEN）で、R6系は3回とも通過。ログは`.harness/runs/20260924-test-r6-residual/`。

## R6-P6: R6受入れと性能gate保留を確定する
- status: done
- done-when: R6-P1〜P5のコードコミット、受入れログ、golden/threshold、fallback、既知の制限、R7暫定参照の再照合条件を記録し、完了コードコミットへ `RenderingRoadmap: R6 complete` trailerを付ける。GPU性能はDeferredとして残す。
- verify: `git diff --check`
- verify: `git log -1 --format=%B`
- verify: `rg -n "R6|RTGI|性能|Deferred|fallback" Docs/RenderingValidation/R6TechniquePlan.md Docs/RenderingValidation/R6Acceptance.md PROGRESS.md`
- stop-when: R6の機能gateが未完、またはR7/R8の実装を前提にしないと受入れできない場合は、完了trailerを付けず残課題を記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, Docs/RenderingValidation/R6Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- result: `R6Acceptance.md`の判定を受入れへ確定し、R6-P5-REF・R6-P7のコミットと検証ログ、既知の制限を加えた。全体gate（`.harness/runs/20260925-r6-p6/`）はtargetless Debug build BUILD_EXIT=0、RenderingValidation 56件中47 passed・8 skipped・1 failed（再オープン中のR4の再照合だけ）。R6の完了判定の独立評価はPASS（blockingなし。non-blocking 3件の記述の正確さは同じコミットで直した）。GPU性能はDeferred。

## R4-REOPEN: R4 DDGIのprobe由来の斑点と漏れを直し、自前PT参照で再照合する
- status: done
- done-when: `R4DDGIPathTracingReferenceVulkanTest`（R4の規定の指標: direct white ROIで露出を1回決め、影・赤・緑ROIの相対輝度誤差≤0.25、赤・緑ROIの優勢色度の差≤0.10）が同じ閾値で通り、R4の受入れ・DDGI系・R6・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R4DDGIPathTracingReferenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R4DDGIPathTracingReferenceVulkanTest|DDGI.*|RenderingDDGILightingContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 閾値かR4の指標を変えないと通らない場合、または承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（DDGI shader・LightingPass）。R7-P7の再照合（更新ルール5、2026-09-25）で発見。R4受入れと同じCornell状態（DDGI有効、RTGI無効、点光源なし、天井の面光源あり、512×512、128フレーム後）のラスタを、4096 sppの自前PT（全輸送）と比べ、赤ROIの相対輝度誤差0.257（上限0.25）、影0.162、緑0.0566、色度差0.0091/0.0167、露出0.846。ラスタのROI平均はR4受入れ時の記録（direct 0.393677、shadow 0.15236、red 0.0687421、green 0.0951394）と同値で、劣化ではない。画像全体ではprobeの位置に格子状の明るい斑点、奥の壁の暗転、画面の縁の暗い帯があり、64画素区画のラスタ/PT輝度比は0.07〜2.15に散らばる（`.harness/runs/20260925-r7-p7-r4/pt-vs-ddgi.png`、`ratio-map.txt`）。probe格子（原点-0.1、間隔0.82、8×8×8）の外側の層は壁の裏と開口の外にある。
- result: `2588b47`・`effd5e3`・`355f8df`・`3b1f3f4`・`ce9d10f`。probeの分類（面の裏のhitが25%を超えるprobeを無効）、probeでの発光面の直接照度（Lambertの式と影の可視率）、照度atlasの全体/間接光だけの2組のlayer（hit面は間接光の組を読み、直接光の二重計上をなくす）、RTXGIの補間（法線方向0.225倍のずらし、押しつぶし、平方根の補間）、距離のcosineの指数8。`R4DDGIPathTracingReferenceVulkanTest`は影0.117・赤0.224・緑0.130・色度差0.030/0.011で合格（閾値不変）、公開Cornell参照0.104/0.117/0.019、動的0.875/0.871。全target build後のRenderingValidationラベル57件中0件失敗（8件はGPU skip契約）、DDGI系9/9。危険地帯の独立評価2周の指摘（鏡映の表裏、発光面の近くの過大評価、頂点の順による放射の側）は修正して回帰の場面を追加した。記録は`Docs/RenderingValidation/R4Acceptance.md`、ログは`.harness/runs/20260925-r4-reopen/`。

## R6-GATE-DDGI-ORACLE: DDGI放射輝度比較のシナリオ履歴を分離する
- status: done
- done-when: 非遮蔽と点/スポット遮蔽のreadbackがそれぞれ固定の直接照明期待値に一致し、二つ目のケースへ前ケースのprobe irradiance蓄積を持ち越さない。rendererのradiance計算・期待値・閾値は変更しない。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- stop-when: pass状態を分離しても期待値とreadbackが一致しない場合、radianceや閾値を調整せず追加原因を記録する。
- paths: Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, TASKS.md, PROGRESS.md

## R6-GATE-OUTDOOR: 承認済みOutdoor goldenとの差分を原因診断する
- status: done
- done-when: `RenderingGoldenOutdoorVulkanTest`が既存golden/thresholdを変更せず通過する。原因と修正を記録し、修正後の`RenderingValidation`全体で新規失敗がないことを確認する。既定のRendering3DTest起動経路と保存シーンは維持する。
- verify: `cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingGoldenOutdoorVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 原因が既承認baselineの改定を要する場合はgoldenを書き換えず根拠を保存する。RHI image-layout等の独立不具合を検出した場合は範囲を分けて記録し、該当経路を隠さない。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering/RenderingValidation, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-M1: NEE/MISとEXR出力方式を選定する
- status: done
- done-when: power heuristic MIS、ReSTIR除外、TinyEXR C API version、EXR channel/precision/compression、seed決定論性、R7 core/outdoor分割を技術選定記録と実装計画に固定する。
- verify: `rg -n "S7|NEE|MIS|ReSTIR|TinyEXR|6f470c9|RenderingRoadmap: R7 complete" Docs/RenderingValidation/R7SamplingAndExrSelection.md Docs/RenderingValidation/R7TechniquePlan.md`
- verify: `git diff --check`
- stop-when: 選定したEXR APIが必要なWindows/CMake buildとfloat scanline出力を満たさない場合、実装開始前に代替を根拠付きで記録する。
- paths: Docs/RenderingValidation/R7SamplingAndExrSelection.md, Docs/RenderingValidation/R7TechniquePlan.md, TASKS.md, PROGRESS.md

## R7-P1: ラスタとPTのBRDF・texture評価を共有する
- status: done
- done-when: raster opaque/transparentとRT shaderが共通BRDF・texture evaluationを参照し、R1数値契約とIndoor/Outdoor goldenを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|ForwardPassPipelinePlacementTest|LightingParamsLayoutTest)$"`
- stop-when: include機構が既存shader compilerで成立しない場合、全shaderを一括移行せず最小の共有境界を記録する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P2: 独立path-tracing pipelineとprogressive accumulationを実装する
- status: done
- done-when: 明示選択のPT pipelineがR5 RT pipeline/SBTとFramePacket snapshotだけで1 sample/frameを累積し、静止時に収束、camera/scene revision変更時に履歴をresetする。raster pipelineとRendering3DTest起動経路は不変。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingVulkanTest$"`
- stop-when: RHI/Vulkan APIやRenderThreadからWorldへの参照が不可避となった場合、境界を越えずsnapshot不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R7-P3A: RHIに配列combined image samplerとnon-uniform indexingを追加する
- status: done
- done-when: `RHI::DescriptorBinding`に配列数`count`（既定1）と配列要素単位のcombined image sampler bindを追加し、Vulkanのset layout・pipeline layout・descriptor pool・writeが`count`を反映する。Vulkan 1.2の`shaderSampledImageArrayNonUniformIndexing`を対応時だけ有効化して`DeviceCapabilities`へ公開する。`count=1`の既存bindingのlayout・pool容量・writeは変えない。専用GPUテストで4要素配列を呼び出しごとに異なる添字で標本化し、各要素の既知色をreadbackで一致させる。範囲外要素・非配列bindingへのbindは拒否する。
- verify: `cmake --build build --config Debug --target Game RHIDescriptorArrayVulkanTest RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIDescriptorArrayVulkanTest|RHIRayTracingPipelineVulkanTest|RHIRayTracingApiContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 対応GPUでnon-uniform indexingを有効化できない、または既存descriptor setの割り当て・更新が回帰する場合は、RHI変更を戻して不足した能力を記録する。
- paths: Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Test/Core/Rendering/RHIDescriptorArrayVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI公開API・Vulkan）。独立評価を通す。

## R7-P3B: PTの材質snapshot・texture・シェーディング法線を接続する
- status: done
- done-when: `RayTracingHitMaterialSnapshot`へGBufferと同じ規則のinstance色（custom dataの非0成分、既定1）とalbedo・normal・metallic・roughnessのtexture handleを値として加える。PTはRenderThreadでtexture handleを解決し、重複を除いた配列descriptorへ束ね、未設定はGBufferと同じ既定値（白・平坦法線・metallic 0・roughness中間灰）にする。closest-hitは`Mesh3DVertex`の法線・UVを重心補間し、共通shaderの材質評価でalbedo・法線マップ・metallic・roughnessを得る。発光はGBuffer同様`色×nits`にpre-exposureを掛ける。既存のposition-only頂点（stride 12）は幾何法線・UV 0へfallbackする。GPUテストでtexture UV標本化、metallic/roughness snapshot、既定値、instance色、発光のpre-exposureをreadbackで固定し、既存PT/屋外/霧/カメラ/EXRテストに回帰がない。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingMaterialVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest PathTracingCameraVulkanTest PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingExrOutputTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: texture資源の寿命をFramePacketとRenderResourcesの既存所有境界で保証できない、またはラスタgoldenが変わる場合は、材質APIを広げず不足を記録する。
- paths: Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/PathTracing, Assets/Shaders/Common/PbrMaterialEvaluation.glsl, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/PathTracingMaterialVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（Rendering公開API・RT資源寿命）。独立評価を通す。

## R7-P3C: NEE/MISとpoint・spot・directional・area light・環境光輸送を実装する
- status: done
- done-when: FramePacketのLightProxyからラスタと同じ単位・減衰・spot円錐の光源表をPTへ渡し、delta lightはNEEだけ（weight 1）、発光三角形と太陽円盤はlight sampleとBSDF sampleをpower heuristic β=2で合成する。BSDF sampleは拡散cosineとGGX可視法線分布の混合で、PDFと共通BRDF評価を同じ方向で整合させる。環境光（一様値またはHDR equirect）はBSDF sampleで評価し、固定0.05の仮背景を廃止する。GPUテストで、R1白炉と同じ15行（roughness 5段×metallic 3段、平均相対誤差1%・最大3%）、既知cdの点光源によるLambert面の解析輝度、面光源のNEEのみ・BSDFのみ・MISが同じ期待値へ収束すること、spot円錐外0、directional照度を固定seedで検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingLightingVulkanTest PathTracingVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingLightingVulkanTest|PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest)$"`
- stop-when: 白炉・解析照明が共通BRDFの近似に起因して閾値を超える場合は、閾値を緩めず、PT推定量の誤りと共通BRDFのエネルギー差を分けて記録する。
- paths: Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Assets/Shaders/PathTracing, Assets/Shaders/Common, Test/Core/Rendering/PathTracingLightingVulkanTest.cpp, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（シェーダーパイプライン・RenderThread）。独立評価を通す。

## R7-P3D: 起動時PT選択とラスタ/PT同一シーン比較を固定する
- status: done
- done-when: RenderingCoordinatorの初期化設定でmain SceneViewをPT pipelineにでき（既定はraster、`Rendering3DTest`起動は不変）、PTはLightingPassと同じ環境マップ設定と検証用一様環境（debug mode 252）を使う。描画検証appに`--renderer=path-tracing`と累積試料数の指定を加え、capture時の実累積試料数をcapture結果へ記録する。R1の白炉と解析点光源の行をPTで実行して同じ評価関数に通し、同じ行のラスタSceneColorとPT SceneColorの差を事前に固定した閾値で比較する。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingRasterParityVulkanTest|RenderingHdrSceneCaptureVulkanTest.*)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- stop-when: PT選択がRenderThreadのpass寿命やFramePacket境界を変えないと成立しない場合は、実行時切替を追加せず起動時選択の不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・pass寿命）。独立評価を通す。

## FIX-NORMAL-MATRIX-SCALE: 法線行列の小スケール退化判定を尺度不変にする
- status: todo
- done-when: `MatrixUtils::CreateNormalMatrix`が一様スケール約0.005未満の物体にも逆転置を返し（特異かどうかは尺度に対する比で判定）、R1室内フィクスチャの平面メッシュの頂点法線を幾何と一致させ、PTの閉包命中シェーダから同じ退化規則の写しを外す。R1数値検証、Indoor/Outdoor golden、`PathTracingRasterParityVulkanTest`が変わらず通る。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR1の数値が変わる場合は、基準の更新を提案して止まる。
- paths: Library/Core/Public/Math, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。3x3の行列式の絶対値がFLT_EPSILON未満だと単位行列になり、回転した小さな物体の法線が回らない。R1室内の平面はこの規則を前提に頂点法線を+Zへ書き換えているため、PTも同じ規則でラスタと揃えている。

## R7-P4: SPP収束とCornell参照を固定する
- status: done
- done-when: 同じseedのnested 16/64/256 spp prefixで256 spp自己収束画像に対するMSEが単調減少し、Cornell boxが固定公開参照と規定誤差内で一致する。
- verify: `cmake --build build --config Debug --target PathTracingConvergenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingConvergenceVulkanTest$"`
- stop-when: monotonic結果がseed探索だけに依存する場合、閾値を緩めずsample estimatorと測定手順を再検討する。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- result: 収束は合格（入れ子の接頭列のMSEが全体と64区画すべてで単調、MSE比の中央値4.374、引き直しでbyte一致）。公開RGBEは色の符号化が公開されておらず色の壁の彩度を再現できないため、ユーザーの判断で輝度の判定を白い面・影・発光面の位置に限り、赤・緑の壁とR4の赤・緑ROIは優勢色度で判定する範囲へ見直した（数値の上限は事前固定のまま）。影ROI 0.0025、赤・緑ROIの色度差0.0044・0.0217、壁の優勢な成分が一致、発光面のずれ1画素、580区画の中央値0.0101・90%点0.0340で合格。記録は`Docs/RenderingValidation/R7CoreAcceptance.md`。

## R7-P5: thin-lensとshutter time samplingを実装する
- status: done
- done-when: FramePacketのcurrent/previous camera・geometry snapshotからshutter時刻を決定論的に評価し、薄レンズCoCの数値誤差と静止/移動goldenを固定する。
- verify: `cmake --build build --config Debug --target Game PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingCameraTest|PathTracingCameraVulkanTest)$"`
- stop-when: 変換補間が不正なshear/scaleを生む場合、live World参照や別のmotion systemを追加せず対応可能なtransform範囲を定義する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P6: seed固定EXR sequence出力を実装する
- status: done
- done-when: TinyEXR v3 C APIでlinear RGB float/ZIP scanlineを出力し、NaN/Infを拒否する。同じscene/seed/SPP/frameの2出力がbyte一致または事前閾値内である。
- verify: `cmake --build build --config Debug --target Game PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingExrOutputTest$"`
- stop-when: independent EXR readerがchannel/precision/windowを一致して読めない場合、sequence APIを広げずwriter integrationを修正する。
- paths: CMakeLists.txt, Library/ThirdParty, Library/Core, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P7: R7 coreを受入れ、R4/R6暫定参照を再照合する
- status: done
- result: `Docs/RenderingValidation/R7CoreAcceptance.md`（2026-09-25）。R6は自前PT（拡散2バウンス、median of means）と閾値内で合格、R4は赤ROI 0.257（上限0.25）で更新ルール5により再オープン（R4-REOPEN）。全体CTestは`.harness/runs/20260925-r7-gate/ctest-all.txt`。
- done-when: R7 coreの完了条件、公開Cornell参照、R1数値検証、CoC、決定論EXR、既知制限を集約し、同一条件のself PTでR4 DDGIとR6 RTGIを各1回再照合する。Outdoor extension完了前にR7 trailerは付けない。
- verify: R7 core関連Debug build/CTestとEXR finite-scanを実行し、全出力ログを開いて閾値結果を確認する。
- verify: `git diff --check`
- stop-when: R4/R6比較がRoadmap閾値を超えた場合、phaseを完了扱いにせず再オープン理由と再検証単位を記録する。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md

## RTGI-HIT-SPECULAR: RTGIの命中面を光沢のある反射でも照らす
- status: todo
- done-when: RTGIの命中面の直接光が材質の粗さ・金属度の鏡面葉を含み、R7屋外比較の既知差（夕の緑の球の影側の面。命中点をLambertにしたPTとは一致）が全輸送との比較で縮む。R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 命中面の材質をRTのsnapshotへ持たせる方法（粗さ・金属度のtextureの扱い）に設計判断が要る場合はユーザーへ戻す。判定の参照の輸送範囲を変える場合もユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差2。命中面の反射率は`3c80458`でinstance色にした。

## RTGI-MULTI-BOUNCE: 接地部の3回目以降のバウンスをRTGIで扱う
- status: todo
- done-when: 球の下の接地部で、ラスタの間接光と全輸送のPTの差（拡散2バウンスの約1.5倍）が縮む。RTGIの光線数の増え方を記録し、R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 光線数の増加が大きい、または方式（放射輝度の再利用・probe併用など）の選択が要る場合はユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差1。

## FIX-CSM-TERMINATOR: 球の明暗境界でCSMの可視が数画素かけて下がるのを直す
- status: todo
- done-when: 屋外シーンの球の明暗境界で、ラスタのCSMの可視（検証表示245）がPTの可視（1画素で1→0）に近づき、R7屋外比較で影の縁として除く画素が減る。承認済みgoldenが変わる場合はユーザーの承認を得る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R7OutdoorPathTracingReferenceVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: goldenの再承認が要る場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差3。例: 夕の緑の球で0.96→0.65→0.48→0.18（PTは1→0）。法線方向のずらし（normal offset）や比較の余裕の見直しが候補。

## FIX-GRAZING-IBL-SPECULAR: 斜めから見た地面のIBLの鏡面反射が強すぎる原因を調べる
- status: todo
- done-when: 昼の屋外シーンの地平線近くの地面で、ラスタのIBLの鏡面反射による間接光（全輸送の約1.5倍）の原因（split-sumの近似、地平線より下の環境、DFGの補償など）を特定し、直すか既知差として根拠を記録する。
- verify: R7屋外比較の`_mean_luminance`と地平線近くの領域の比を開いて確認する。
- stop-when: 承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差4。朝・夕は1.07〜1.11倍。

## FIX-CSM-MEGA-CASTER-BOUNDS: CSMの遮蔽物の境界球にShadowMapPassが描かないMegaGeometryを含めない
- status: todo
- done-when: CSMの深度範囲に含める境界球が、影の地図に実際に描く物体と一致する（MegaGeometryを影に描くまでは含めない、または描くようにする）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowLightMatricesTest|CascadedShadowLightMatricesTest)$"`
- stop-when: MegaGeometryを影に描くかの判断が要る場合はユーザーへ戻す。
- paths: Library/Core/Private/Rendering, Test/Core/Rendering
- notes: `382489f`の評価のnon-blocking指摘。過大収集で深度範囲とPCSSの探索半径が広がるだけで、影は欠けない。

## TEST-FULL-CTEST-BASELINE: 全体CTestの既存の失敗を直す
- status: todo
- done-when: 全体CTestで、R7の作業前（`c9a3e33`）から失敗している次のテストが通るか、失敗の理由と扱いが記録される: `VolumetricsPassContractTest`（霧の設定行の文字列）、`RenderResourcesDomainContractTest`（`WaitIdleWithoutResultCheck(`の数3、期待2）、`ViewportCameraIdRenderPlanTest`・`BoardComponentRoutingTest`・`SkeletalFramePacketSnapshotTest`（GPUデバイスのないテストでRenderingCoordinator::GenerateDrawCommandsが`m_Device->GetCapabilities()`をnull参照、`578236d`以来）、`FrameCaptureReadbackHelperTest`・`ComponentDataRegistryTest`・`WorldSyncDifferentialTest`（WorldTransformの777）・`CanvasViewRenderTest`・`RenderGraphTextureUsageContractTest`（ShadowMapPassの初期化失敗）（Debugのassertの対話窓で止まりtimeout）、`M9WorldAcceptanceTest`（負の対照の画素差）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --timeout 600`
- stop-when: テストの期待を変える必要がある場合は理由を記録してユーザーへ戻す。
- paths: Library/Core, Test/Core, Game
- notes: 2026-09-25の全体gate（`.harness/runs/20260925-r7-gate/`）で確認。SkinnedRenderPathContractTestはTEST-SKINNED、R4はR4-REOPEN。

## R7-O1: R2 SkyAtmosphereをPT miss radianceとsolar samplingへ接続する
- status: done
- done-when: rasterと同じSkyAtmosphereParametersからPTのsky miss radianceとsolar disk direct lightingを構成し、朝/昼/夕3時刻の有限性・parameter parityを検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- stop-when: R2 parameter semanticsを変更しないと接続できない場合は、R2を先行修正しgoldenを書き換えない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-O2: R3 volumetric fogをPT ray transportへ接続する
- status: done
- done-when: rasterと同じfog density/height/anisotropy parameterからfinite transmittanceとsingle-scatteringを評価し、fog/sky disabled fallbackを維持する。
- verify: `cmake --build build --config Debug --target Game PathTracingVolumetricTest PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVolumetricTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: R3 public parameter semanticsを拡張しないと一致しない場合はR3側契約差として記録し、RenderingからVulkanを参照しない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## SKY-SUN-P1: 空の太陽の地表照度と方向光をCPUで一つに定める
- status: done
- result: `fa40e74`。透過率・地表照度・空の太陽の方向光をSkyAtmosphere/SkySunLightの公開関数にし、透過率LUTも同じ関数で作る（既存LUT値は不変）。
- done-when: 公開APIで、空のパラメータから地表での大気の透過率（RGB、透過率LUTと同じ式・同じ定数）と、太陽の地表照度（太陽円盤の照度×透過率、RGB）と、空の太陽を表す方向光（予約LightId、方向=太陽方向の逆、色=透過率、強度=照度の輝度、影あり）を求められる。空が無効なら方向光を作らない。透過率LUTの生成は同じ関数を使い、既存LUTの値は変わらない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest SkyAtmospherePassContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|SkyAtmosphereIblTest)$"`
- stop-when: LUTの既存値が変わる場合は共有化を止め、同じ式の複製とLUTとの一致テストへ切り替える。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-25）「空の太陽に統一」。空が有効なら空の太陽からエンジンが方向光を作り、ラスタはCSMの影付きで照らし、PTは同じ太陽を円盤の光源標本で数える（二重に数えない）。シーンの方向光は追加の光として残す。既定の起動は空が無効で変わらない。

## SKY-SUN-P2: 空の太陽をFramePacketの光源へ加え、ラスタの影をその灯へ掛ける
- status: done
- result: `4abdbcd`。FramePacket作成で空の太陽を1つだけ加え、CSM・RT影・LightingPassが同じ選び方（`603f366`で専用ヘッダへ分離）で影の灯を決める。
- done-when: GameThreadのFramePacket作成で、空が有効なら空の太陽の方向光を光源表へ1つだけ加える（再利用するpacketでも重複しない）。影を落とす方向光の選択は、空の太陽があればそれを選び、なければ従来どおり「表示される方向光がちょうど1つで影あり」のときだけ選ぶ（CSM・RT影で共通の関数）。ラスタ（lighting.frag・forward_transparent.frag）はCSM/RT影をその灯にだけ掛け、他の方向光は影なしで照らす。空が無効なシーンの描画と既存golden・閾値は変わらない。
- verify: `cmake --build build --config Debug --target Game CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest LightingLightBufferTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest|LightingLightBufferTest|LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なシーンのgoldenが変わる場合は止めて原因を直す。承認済みgoldenの更新が必要になったらユーザーへ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（光源・CSM・RT影・RenderThread）。影の係数は現在すべての方向光に同じ値を掛けるため、方向光が2つになるとCSMもRT影も止まる。影を掛ける灯は光源データの未使用の成分で示し、UBOの配置は変えない。

## SKY-SUN-P3: PTの太陽を地表照度（透過率込み、RGB）で数え、方向光と二重にしない
- status: done
- result: `fc7e985`・`e42a185`。PTの太陽円盤はRGBの地表照度で数え、空の太陽の方向光は点・spot・方向光の評価から除く。
- done-when: PTの太陽円盤の光源標本と2次以降の円盤命中のMISが、SKY-SUN-P1の地表照度（RGB）を使う。PTの点・spot・方向光の評価は空の太陽の方向光を除き、霧の単一散乱はラスタと同じ灯（CSMの灯）を使う。空を有効にした同じシーンで、ラスタの空の太陽の方向光とPTの太陽が同じ照度になる。R7-O1の屋外テストの期待値は同じ公開関数から求める。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingLightingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingLightingVulkanTest|PathTracingVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なPTの基準（R6/R7参照比較、R1白炉・解析照明）が変わる場合は止めて原因を直す。
- paths: Library/Core/Private/Rendering, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・光源）。現状のPTの太陽照度はスカラーで大気の透過率を掛けていない（`PathTracingPass.inl`の`SkyState[1]`）。

## R7-O3: 屋外PT/raster相互比較を受入れR7を完了する
- status: done
- result: `Docs/RenderingValidation/R7OutdoorAcceptance.md`（2026-09-25）。3時刻とも閾値内（朝 平均0.0107/0.0274・一致画素最大0.0989/0.1293・区画0.0368/0.0682、昼 0.0153/0.0189・0.0937/0.1704・0.0576/0.0668、夕 0.0127/0.0238・0.0903/0.1367・0.0376/0.0583）。判定の参照はラスタが実装する輸送（拡散2バウンス）のPT、median of means、影の縁の除外と影の内側の負の対照（ユーザーの判断）。全輸送との差は既知差として記録。
- progress (2026-09-25): 比較テスト`6d7628f`。ラスタ側の修正: 低い太陽の影（`fdbc5dc`）、空の照明を地表から見た空へ（`6753496`）、CSMの深度範囲の向き（`6f1dc0b`）と遮蔽物の境界球（`382489f`）、影の余裕（`d3f254a`）、カスケード境界（`fa00d84`）、RTGI/DDGIの命中面の反射率（`3c80458`）、直接光のAO（`49116c3`）、RTGIの拡散2バウンス（`44d9a3a`、ユーザー決定）。Outdoor goldenは`efce943`で再承認。
- progress: 3時刻ともFLIP平均は閾値内（朝0.0127/0.0277、昼0.0168/0.0196、夕0.0163/0.0245）。8×8区画最大は朝のみ閾値内（昼0.0712/0.0709、夕0.0988/0.0736）、画素単位最大は3時刻とも超過（閾値を超える一致画素 朝17・昼33・夕182）。上位200画素の内訳（昼/夕）は影の縁134/152、3回目以降のバウンス64/21、その他2/27（夕の緑の球の影側の面）。ログは`.harness/runs/20260925-rtgi-2bounce/`。
- done-when: sky/volume込みの屋外sceneでPTとrasterを3時刻比較し、FLIP pool/max-pixel threshold内であり、既知近似差を記録する。R7 core + outdoor extension受入れcommit末尾に`RenderingRoadmap: R7 complete`を付ける。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- verify: relevant R7 Debug build/CTestと3時刻captureの数値レポートを開いて確認する。
- stop-when: 3時刻の一部で閾値超過、NaN/Inf、未解決のR7 core acceptanceが残る場合、完了trailerを付けない。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md
