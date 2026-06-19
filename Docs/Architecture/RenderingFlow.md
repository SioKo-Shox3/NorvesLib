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

## RenderGraph 主経路

`SceneView::Render(ViewRenderContext&)` は登録済み `IViewPass` / `IRenderGraphPass` から 1 viewport 分の `RenderGraph` を組み立て、compile 後に実行する。
RenderThread は live view/pass object を実行するが、カメラ、viewport、draw command、proxy などのフレーム描画データは `FramePacket` snapshot と `ViewRenderContext` の active viewport から読む。

Deferred pipeline の production 経路は pass pointer や `SharedResourceRegistry` ではなく、RenderGraph named resource で pass 間接続を行う。

- `ShadowMapPass` は `ShadowMap` を publish/export し、`LightingPass` は named `ShadowMap` を優先して読む。
- `GBufferPass` は `GBuffer.Albedo` / `GBuffer.Normal` / `GBuffer.Material` / `GBuffer.Emissive` / `GBuffer.Depth` を publish する。
- `MegaGeometryPass` は 5 本の GBuffer named attachment がすべて存在する場合だけ load/store attachment として扱う。partial named resource の場合は graph 上では GBuffer を mutate せず、legacy fallback に落とす。
- `SSAO` / `Lighting` / `Forward` / `SSR` / `Bloom` / `ToneMapping` / `FXAA` / `Upscale` は named input/output を優先する。
- `PresentationPass` は通常の swapchain 合成経路であり、`PresentationColor`、次に `ToneMappedColor` を入力として backbuffer へ blit する。

`SceneView::SetupDeferredPipeline()` では production pass pointer 接続を行わない。
`SetGBufferPass()` / `SetInputPass()` などの setter は legacy/fallback bridge 用の互換 API として残す。

## Legacy fallback 境界

`SharedResourceRegistry` は主経路ではなく legacy/fallback bridge である。
production deferred pipeline では GBuffer / ShadowMap / Lighting / Forward transparent の registry 登録を無効化し、named resource を正とする。
ただし単体利用や未移行 fallback のため、各 pass の既定値は互換寄りに保つ。

`PresentationComposer` は `PresentationPass` が graph input や blit resource 不足で handle できない場合の legacy fallback helper である。
`RenderFrameExecutor` は graph presentation が handled したときだけ fallback compose をスキップする。
graph presentation も fallback compose も成功しない viewport は presentation 済みとして数えず、次の presentation は clear 判定を維持する。

旧 direct draw fallback は、pass が未登録で draw command だけがある view の互換経路として残す。
RenderGraph 対応済み view では pass chain を登録して graph 経路を使う。

## RHI 境界

- Rendering 層は `RHI::IDevice`、`ICommandList`、`ISwapChain` などの抽象 API だけを見る。
- backend 固有の型は `Library/Core/Private/RHI/<Backend>/` に閉じ込める。
- shaderc/GLSL compiler は `IDevice::CreateShaderCompiler()` で作成する。
- Slang compiler は `IDevice::CreateSlangShaderCompiler()` で作成する。未対応 backend は `nullptr` を返す。
- Rendering 層から `RHI/Vulkan/*` を include しない。

## RenderGraph debug dump

`RenderGraph` は `RGDumpOptions` で text / dot / json の debug dump と debug marker を制御できる。
`RenderingCoordinatorSettings::RenderGraphDumpOptions` または `RenderWorldSettings::RenderGraphDumpOptions` から設定し、`RenderingCoordinator` が内部 `RenderGraph` へ渡す。

主要オプション:

- `bEnabled`: dump 構築を有効化する。
- `bWriteFiles`: `OutputDirectory` へファイルを書き出す。
- `bText` / `bDot` / `bJson`: 生成形式を選ぶ。
- `bDebugMarkers`: command list の debug marker を pass 単位で出す。
- `MaxFrameCount`: file write 対象フレーム数を制限する。
- `FilePrefix`: 出力ファイル名の prefix。

`NORVES_ENABLE_RENDERGRAPH_DUMP` が無効な build では dump 文字列と file write は無効化されるが、`RenderGraphDebugStats` は compile / execute / barrier / transient acquire の確認に使える。

CI / regression 確認では以下を使う。

```powershell
cmake --build build --config Debug --target RenderGraphDumpTest
ctest --test-dir build -C Debug -R RenderGraphDumpTest --output-on-failure
```

描画経路を変更した場合は、必要に応じて `RenderGraphCompileTest` / `RenderFrameExecutorPlanTest` / `RenderGraphPresentationPassTest` も併走し、named resource の version、barrier、presentation handled/fallback の期待を確認する。

## Resize と Shutdown

- Resize は `RenderWorld::Resize()` で保留し、次の `BeginFrame()` で `WaitForRender()` 後に適用する。
- Shutdown は RenderThread を先に停止し、その後 RenderResources、RenderingCoordinator、RHI 参照を解放する。
- SwapChain 依存リソースは Resize/dirty presentation のタイミングで再作成する。

## 今後の拡張方針

- pass 間の texture state と lifetime は RenderGraph を正とし、`SharedResourceRegistry` へ戻さない。
- frames-in-flight を増やす場合は、command buffer、descriptor、uniform allocation、resource retirement を frame index 単位で分離する。
- RHI コメントや型名には backend 固有語を出さず、backend 実装ファイル内でだけ Vulkan/DX へ変換する。
