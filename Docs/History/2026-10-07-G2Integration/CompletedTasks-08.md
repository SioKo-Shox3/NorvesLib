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
