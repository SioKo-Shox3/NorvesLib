# TASKS — NorvesLib

Rendering R1 の承認済み残作業。仕様は `Docs/Plans/RenderingR1PhysicalFoundationPlan.md` と各条件付き計画を参照する。P1〜P4 は PROGRESS.md のコミット・証跡を基点にする。

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
- status: todo
- done-when: P5完了後、条件付き計画v4の40 static rows/120 captures、全画素sRGB oracle、forced format行、GPU CTest exact21、3 script self-test、focused7 build/CPU6、Indoor/Outdoor object-presenceを満たす。source20本の変更をコミットし、P6bの基点と証跡を記録する。
- verify: `cmake --build build --config Debug --target ForwardPassPipelinePlacementTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest RenderingPerceptualDiffTest SceneSerializerTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ForwardPassPipelinePlacementTest|RenderingValidationSceneContractTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest|RenderingPerceptualArtificialDifferenceTest|SceneSerializerTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- verify: `pwsh -NoProfile -File Scripts/CalibrateRenderingVisualThresholds.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/UpdateRenderingGoldenBaselines.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21`
- paths: Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Assets/Shaders/mesh3d.vert, Assets/Shaders/mesh3d.frag, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingPerceptualDiffTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.h, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.h, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.cpp, Test/Core/Rendering/CMakeLists.txt, Scripts/UpdateRenderingGoldenBaselines.ps1, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/TestRenderingGpuCTestContract.ps1, Docs/RenderingValidation/GoldenBaselines.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6a-conditional-implementation-plan-v4.md` の技術仕様を全文読む。plan SHA=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F は2026-08-15承認済み。§11のGPU/skip/各SelfTestR1Contractも実行し、各raw outputを開く。旧mesh shaderの削除は計画で指定された2本に限定。P6bの正式候補生成・source publish・full build/full CTestはこのタスクでは行わない。
