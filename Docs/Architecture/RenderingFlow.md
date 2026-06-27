# Rendering Flow

## 目的

この文書は GameThread、RenderThread、`FramePacket`、`SceneView`、RHI 境界の責務を定義します。

## レイヤー

- `RenderWorld`: Game 側の入口。フレーム進行、RenderThread 起動、Resize 保留を管理する。
- `RenderingCoordinator`: Screen、SceneView、DrawCommand、FramePacket、RHI リソースを調整する。
- `RenderThread`: スレッド管理と `RenderingCoordinator::RenderFrame()` 呼び出しだけを担当する。
- `RHI::IDevice`: backend 固有オブジェクトの生成口。Rendering 層は Vulkan などの具象型を直接生成しない。

## FramePacket

`FramePacket` は GameThread から RenderThread へ渡す 1 フレーム分のスナップショットです。

状態遷移:

1. `Empty -> Writing`: GameThread が `AcquireForWrite()` で取得する。
2. `Writing -> Ready`: GameThread が `FinishWrite()` で確定する。
3. `Ready -> Queued`: Multi-threaded path で RenderThread に渡す。
4. `Queued -> Reading`: RenderThread が描画直前に読み取り権を取る。
5. `Reading -> Recycling -> Empty`: 描画完了後に再利用可能にする。

Single-threaded path では `Ready -> Reading -> Empty` を GameThread 内で完了する。

## Packet 所有規約

- `BeginFrame()` の Ready packet drain は single-threaded path 専用。
- Multi-threaded path では `Ready` packet を GameThread 側で回収しない。
- `RenderFrame(nullptr)` は描画をスキップする。
- RenderThread は live `SceneView` を読まず、必ず `FramePacket` の snapshot を読む。
- `ReleasePacket()` は `Reading` packet だけを `Empty` に戻す。

## SceneView 同期

- `World::SyncToSceneView()` は現在の World 状態から proxy を再構築する。
- Mesh、Light、MegaGeometry proxy は毎回 `ClearAllProxies()` 後に有効な Component だけを追加する。
- Component が disabled または owner が inactive の場合、その frame の proxy には残らない。
- DrawCommand は `GenerateDrawCommands()` で `SceneView` から生成され、`FramePacket` にコピーされる。

### カメラ経路（3 層）

カメラは Mesh/Light/MegaGeometry とは別に、専用の 3 層構成で SceneView のメインカメラ proxy を駆動する。

- **3 層カメラ構成**:
  - `MayaCameraController`: 入力 → `SpringArmIntent` の感度換算のみを担う（World には属さない純粋な変換層）。
  - `SpringArmComponent`: `SpringArmIntent` を受けて球面アームを駆動し、owner WorldObject の Transform（位置・回転）を毎フレーム上書きする。`World::Tick` が `SpringArmComponent::Tick` を自動駆動する。
  - `CameraComponent`: レンズ層。owner Transform から forward/up/right を導出し `BuildCameraProxy` で `CameraProxy` を生成する。
- **アクティブカメラの決定的選定**: `World::SyncToSceneView()` が全 WorldObject を走査し、`bIsActiveCamera && IsActive()` な `CameraComponent` を 1 つ選定する（複数該当時は RenderOrder 最小、同値なら ComponentId 最小で決定的に決まる）。選定したカメラは `BuildCameraProxy` → `SceneView::SetMainCameraProxy` で反映し、該当なしなら `ClearMainCameraProxy` する。Mesh/Light の RemoveStale 掃引と同様、毎 Sync で全上書きする。
- **RenderingCoordinator の採用と補完**: `GenerateDrawCommands` は SceneView のメインカメラ proxy を最優先で採用する。`CameraComponent` は AspectRatio/Viewport を設定せず描画解像度に追従するため、`RenderingCoordinator` が空の Viewport を描画解像度で補完し、`BuildViewportRenderPlan` が実ピクセル矩形から AspectRatio を再計算する。

## RHI 境界

- Rendering 層は `RHI::IDevice`、`ICommandList`、`ISwapChain` などの抽象 API だけを見る。
- backend 固有の型は `Library/Core/Private/RHI/<Backend>/` に閉じ込める。
- shaderc/GLSL compiler は `IDevice::CreateShaderCompiler()` で作成する。
- Slang compiler は `IDevice::CreateSlangShaderCompiler()` で作成する。未対応 backend は `nullptr` を返す。
- Rendering 層から `RHI/Vulkan/*` を include しない。

## Resize と Shutdown

- Resize は `RenderWorld::Resize()` で保留し、次の `BeginFrame()` で `WaitForRender()` 後に適用する。
- Shutdown は RenderThread を先に停止し、その後 RenderResources、RenderingCoordinator、RHI 参照を解放する。
- SwapChain 依存リソースは Resize/dirty presentation のタイミングで再作成する。

## 今後の拡張方針

- pass 間の texture state と lifetime は render graph または frame resource manager に集約する。
- frames-in-flight を増やす場合は、command buffer、descriptor、uniform allocation、resource retirement を frame index 単位で分離する。
- RHI コメントや型名には backend 固有語を出さず、backend 実装ファイル内でだけ Vulkan/DX へ変換する。
