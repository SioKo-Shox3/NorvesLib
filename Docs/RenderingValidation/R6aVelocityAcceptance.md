# R6-a velocity 受入れ記録

受入れ日: 2026-09-22

## 判定

R6-a の独立ゲートを受入れ完了とする。通常の遅延不透明 `MeshProxy` について、前フレームのオブジェクト変換とメインカメラを `FramePacket` の値スナップショットとして RenderThread へ渡し、GBuffer の `GBuffer_Velocity` へ `R16G16_FLOAT` の velocity を出力した。

起動経路は変更していない。`GameApplicationHandler::CreateGameModeStateMachine` から `Rendering3DTest` を開始し、球・地面・岩・ライト球・方向ライト・HDR環境を構成する既存シーンをそのまま使用する。R6-aのために別シーンや別起動経路は追加していない。

## 実装範囲

- `GPUSceneInstanceData` に `PreviousWorld` を追加し、通常の `MeshProxy` とインスタンス描画へ前フレーム変換をコピーする。
- `World::SyncEntityRecursive` が現フレームの `MeshProxy` を毎フレーム更新し、proxy公開後に `MeshComponent` の変換履歴を確定することで、移動後に停止した物体の履歴を現フレームへ進める。
- `FramePacket` に `PreviousMainCamera` と有効フラグを追加し、`RenderingCoordinator` が前回完了フレームのカメラを次のパケットへコピーする。
- カメラを公開しなかったフレームをまたぐ場合は、次フレームで前カメラ履歴を再利用しない。
- GBuffer に5枚目のカラー出力として `GBuffer.Velocity` / `GBuffer_Velocity` を追加し、フォーマットを `R16G16_FLOAT`、readback用usageを `TransferSrc` とする。既存のAlbedo/Normal/Material/Emissive 4面の値契約は維持する。
- velocity shader は device clip space を viewport UV へ変換し、`currentUV - previousUV` を出力する。カメラ履歴が初回または無効の場合、velocityはゼロとする。
- skinned mesh、MegaGeometry、Forward透明の履歴拡張は本工程の対象外とし、後続工程へ残す。

## 検証結果

### ビルドと契約テスト

実行したコマンド:

```text
cmake --build build --config Debug --target Game -- /m:1
```

結果: `Game.vcxproj -> ...\build\Game\Debug\Game.exe`、exit 0。

関連するCPU/RenderGraph/FramePacket契約CTest:

```text
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(FrameCaptureReadbackHelperTest|FramePacketManagerTest|RenderFrameExecutorPlanTest|MeshBatcherTest|MeshBatcherInstancingTest|InstanceDataFlattenTest|GBufferMaterialDescriptorCacheTest|SceneViewViewportCommandTest|BoardTransformTest|CanvasViewRenderTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RenderGraphNamedResourceTest|RenderGraphAttachmentStateTest|WorldCameraSyncTest|PhysicsArchitectureContractTest|PhysicsFixedStepPipelineTest)$"
```

結果: `100% tests passed, 0 tests failed out of 17`。

### GPU velocity readback

```text
ctest --test-dir build -C Debug --output-on-failure -V --no-tests=error -R "^RenderingVelocity(Static|Motion|Camera|Object|FirstFrame|MoveThenStop)VulkanTest$"
```

結果: `100% tests passed, 0 tests failed out of 6`。

readback結果:

```text
static:        initial frame=0 max_magnitude=0 non_zero=0
static:        stable  frame=2 max_magnitude=0 non_zero=0
camera:        moved frame=2 expected=(0.0151554,0) actual=(0.015152,0)
object:        moved frame=2 expected=(-0.0649519,0) actual=(-0.0649414,0)
camera+object: moved frame=2 expected=(-0.0497965,0) actual=(-0.0497742,0)
camera+object: moved-center expected=(-0.0497965,0) actual=(-0.0497742,0)
object:        background pixel=(255,0) expected=(0,0) actual=(0,0)
first-frame:   initial frame=0 max_magnitude=0 non_zero=0
move-stop:     stopped frame=4 max_magnitude=0 non_zero=0
```

カメラのみ、物体のみ、およびカメラ＋物体のサンプルは、既知の現在/前フレーム行列から算出したNDC差分とGPU readbackを `0.002` 以下の許容誤差で比較した。物体のみでは対象外の背景画素もゼロであることを確認し、移動後停止は別フレームで確認して履歴の持ち越しによる残留velocityがないことを固定した。

### 既定Game起動

```text
build\Game\Debug\Game.exe --imgui --exit-after-rendered-frames=120
```

`Game.log` で次を確認した。

- `CreateGameModeStateMachine` と `3Dレンダリングテストモードを開始します`
- 球、地面、ライト球、方向ライトの生成
- boulderの非同期ロード開始と `Boulder model loaded and added to World`
- `Environment source and derived IBL resources created`
- `GBufferPass initialized` と `GBufferPass shutdown`
- `exit-after-rendered-frames reached rendered=120 baseline=0 target=120`

実行時のVulkan validation error (`Validation Error` / `VUID-`) は0件だった。

ログにあるSlang SDK未導入による `neural_material_decode.slang` の既存warning/errorは、decoder無効化の既存フォールバックであり、R6-a対象経路の終了やvelocity readbackを失敗させていない。

## ロードマップ整合

RoadMapのR6-a完了条件である「既知移動に対するvelocity readbackの解析値検証」を満たした。R6本体のRTGI・テンポラル蓄積・デノイザ、R7、R8は未完了のまま維持する。`Docs/Plans/RenderingRoadmap.md` は無視対象のため変更・追跡化していない。
