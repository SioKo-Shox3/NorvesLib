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

## RHI 境界

- Rendering 層は `RHI::IDevice`、`ICommandList`、`ISwapChain` などの抽象 API だけを見る。
- backend 固有の型は `Library/Core/Private/RHI/<Backend>/` に閉じ込める。
- shaderc/GLSL compiler は `IDevice::CreateShaderCompiler()` で作成する。
- Slang compiler は `IDevice::CreateSlangShaderCompiler()` で作成する。未対応 backend は `nullptr` を返す。
- Rendering 層から `RHI/Vulkan/*` を include しない。

## Resize と Shutdown

- Resize は `RenderWorld::Resize()` で保留し、次の `BeginFrame()` で `WaitForRender()` 後に適用する。
- Shutdown は RenderThread を先に停止し、その後 ResourceManager、RenderingCoordinator、RHI 参照を解放する。
- SwapChain 依存リソースは Resize/dirty presentation のタイミングで再作成する。

## 今後の拡張方針

- pass 間の texture state と lifetime は render graph または frame resource manager に集約する。
- frames-in-flight を増やす場合は、command buffer、descriptor、uniform allocation、resource retirement を frame index 単位で分離する。
- RHI コメントや型名には backend 固有語を出さず、backend 実装ファイル内でだけ Vulkan/DX へ変換する。
