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

## カメラ供給経路

`Rendering3DTest` のカメラは入力、姿勢、描画 snapshot を3層に分けます。

1. `CameraInputCollector` は `InputRouter` で上位 controller に consume されず到達したマウスと左右 Alt の状態だけを、フレーム限定 `InputState` へ集約します。Alt なしの drag はカメラ intent にならず、scroll は Alt に依存しません。
2. `MayaCameraController::BuildIntent()` は値 `InputState` を値 `SpringArmIntent` へ変換します。`SpringArmComponent` は World 所有 pivot の `ObjectId` を安全に再解決し、camera Entity の world transform を更新します。
3. camera Entity が Inner 所有する `CameraComponent` は `BuildCameraProxy()` で値 `CameraProxy` を構築し、GameThread が `RenderWorld::SetMainCamera()` へ渡します。

GameThread から RenderThread へ渡るのは `FramePacket` に複製された `CameraProxy` snapshot だけです。`Entity`、`CameraComponent`、`SpringArmComponent` などの live `Object` pointer は RenderThread へ渡しません。Enter 失敗と Leave では `InputRouter` の借用登録を先に解除して公開 pointer を null 化し、Entity と Inner Component の実所有解放は `GameModeScope` が行います。

## DebugViewMode

- `Rendering3DTest` の入力は `F1=Normal`、`F2=Unlit`、`F3=Wireframe`、`F4=MegaGeometryClusters`、`F5=循環`。
- オンスクリーン HUD は現状なく、モード名確認は `DebugView` カテゴリのログで行う。ログは `RenderWorld::SetDebugViewModeAll()` 後に `GetMainViewportDebugViewMode()` で読み直した main Viewport の反映後値を出す。
- canonical な保持先は GameThread の live `Viewport`。`RenderingCoordinator::BuildViewportRenderPlan()` が `Viewport::GetDebugViewMode()` を `ViewportRenderPlan.DebugMode` へコピーし、RenderThread / pass 側は `ViewRenderContext::GetActiveDebugMode()` 経由で snapshot 値だけを見る。
- `DebugViewMode != Normal` のときは postprocess bypass を有効化し、SSR / Bloom / ToneMapping / FXAA は local GPU params で無効化する。RenderGraph の named resource / export contract は通常経路と同じまま維持する。
- シェーダーは CMake copy ではなく、`NORVES_SHADER_DIR=${CMAKE_SOURCE_DIR}/Assets/Shaders` を使って `ShaderManager` が実行時コンパイルする。

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

### IBL（環境光）

全球の平均照度 E_avg は `CreateIblResources` が IBL の組ごとに CPU で求める。
拡散照度の資源と同じ選び方で、強度（`ambientColor.w`）を掛ける前の値を照明の UBO に載せる。
G11 の GR54 が雲量によって IBL を GPU で混ぜるときは、E_avg も同じ重みで線形に混ぜて更新する。

### スキンの転送と視錐台の絞り込み

`SkinnedMeshGpuStore` の頂点バッファ（VB）とインデックスバッファ（IB）は `CPUAccessible = false` の DeviceLocal に置き、ステージングを経由して転送する。`FramePacket` のフレームリースに初めて載った資産は、RenderThread のフレームの始めに、パスの記録より前に転送する。ボーンパレットは毎フレーム書き込むため CPU から見えるメモリに置く。ログ有効時は、最初の転送で `SKINNED_MESH_MEMORY vb=<型> ib=<型>` を一度だけ出す。

`AppendSkinnedDrawCommands` は姿勢に応じた局所の `AnimatedBounds` を `DrawCommand::Skinned.LocalBounds` に持たせる。`SkinnedLodSelection.h` の純関数 `MakeSkinnedViewFrustum` と `IsSkinnedDrawInView` は、主カメラの View×Projection から作った視錐台とワールド変換後の境界を照合する。判定は保守的で、面をまたぐ描画、境界がない描画、視錐台が無効な場合は残す。`RenderWorld::SetSkinnedViewCullingEnabled` は検証用の切り替えで、`SceneProxy::bSkinnedViewCulling` に写される。既定は on。

可視バッファの経路では、`SkinningComputePass` は主カメラに見える描画と、影の出力を読むときは視錐台外でも影を落とす描画を変形する。VSM は主カメラの視錐台外の影も描くためである。`VisibilityRasterPass` は主カメラに見えないインスタンスを ID のラスタに記録しない。予備の `GBufferPass` も視錐台外のスキンを記録しない。両経路で記録したスキンの描画は `SkinnedGBufferRecordedDraws` に数える。

### 太陽の影とカスケードの絞り込み

`ShadowMapPass` は CSM の各カスケードで、MegaGeometry に加え、スキンと通常のメッシュの影を落とす描画を絞り込む。スキンの境界は `BuildSkinnedShadowCasterWorldBounds`、通常のメッシュは `MeshProxy` の境界球から求める。インスタンシングでは、同じメッシュの影を落とす proxy の境界球を包む球を使う。`SelectCascadedShadowCasterDraws` は `CascadedShadowCascadeMayContainCaster` を使い、影の地図の XY にかからないカスケードには描画を出さない。境界を求められない描画はすべての有効なカスケードに残す。ログ有効時は描画数か候補数が変わったときに `CSM_CASTER_CULL c0=<描いた>/<候補> …` を出す。

`RenderWorld::SetSunShadowParameters` は `SunShadowParameters` の `MaxDistance` と `SplitLambda` を設定し、`SceneProxy::SunShadow` と `FramePacket` を経由して `ShadowMapPass` に渡す。`MaxDistance` は CSM と VSM の影の範囲、`SplitLambda` は CSM の分割に効く。`SanitizeSunShadowParameters` は非有限値と 0 以下を「設定のまま」を表す 0 にし、`SplitLambda` の上限を 1 にする。既定は 80 m と λ 0.5。旧 `SetSunShadowMaxDistance` も残り、新しい設定口へ転送する。
## 材質ごとの GBuffer プログラム

GameThread は `RenderWorld::RegisterGBufferProgram(GBufferProgramDesc)` に名前とシェーダーのファイル名を渡す。初期化の窓では即時に登録され、窓が閉じた後の登録・解除は保留されて RenderThread の `SceneView::Render` 冒頭で反映される。頂点シェーダーの名前が空なら標準を使う。材質は `MaterialCreateData::Program` で名前を指定し、空なら標準を使う。未登録、シェーダーを読めない、またはパイプラインを作れない場合は標準へ落ちる。

`GBufferPass` は `{program, cull, alphaClip, polygon, skinned}` を鍵とする表を持ち、初めて必要になった鍵のパイプラインを同期して作り、同じ鍵の材質で再利用する。作成成功時には `GBufferPipelineCreated program=<名前|standard> skinned=<0|1> polygon=<fill|line> cull=<back|none|front> alphaClip=<0|1> total=<N>` を INFO で出す。並列のコマンド記録中は表の検索だけを行い、シェーダーやパイプラインは記録の準備段で作る。パイプライン作成時に永続キャッシュは使わないため、新しい鍵の初回描画ではヒッチが生じうる。

可視バッファ on ではプログラム指定の物は ID のラスタに含めず、`VisibilityResolvePass` の後・`DecalPass` の前の `GBufferProgramOverlayPass` が GBuffer と深度へ重ねる。標準材質だけのフレームでは重ねる資源を宣言しない。fallback（`--visibility-buffer=off`）では `GBufferPass` が標準材質の後にプログラム材質を描く。重ねる物は ID のラスタの深度に載らないため、2 パスの遮蔽に使う HZB の遮蔽物にならない。

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

## 描画の拡張の段（GR25）

ゲーム側は `RenderExtensionDesc` の `Name`・`Stage`・`Order` と `RenderExtensionFactory` を指定してパスを登録する。`Stage` が決める差し込み位置と書き込み先の例は次のとおり。パスは `Declare` で RenderGraph 資源の読み書きを宣言し、後続が参照する名前へ接続する。

| `RenderExtensionStage` | 差し込む位置 | 書き込む対象の例 |
|---|---|---|
| `GBuffer` | `FoliagePass` の後、`SSAOPass` の前 | `GBuffer` の色・法線・深度 |
| `AfterLighting` | `LightingPass` の直後、`ShadowProbePass`・`SSRPass` の前 | 照明後の HDR シーンカラー `Scene.Color` |
| `Transparent` | 半透明の `ForwardPass` の後、`DistortionPass` の前 | 半透明合成後の HDR シーンカラー `SSR.SceneColor` または `Scene.Color` |
| `PostTemporalAA` | 後段スタックの `TemporalAAPass` の後 | TAA が有効なら TAA 適用後の HDR シーンカラー |
| `PostTonemap` | 後段スタックの `VignettePass` の後 | トーンマップ・Vignette 適用後の `ToneMappedColor` |
| `ShadowCaster` | 予約。現在は登録を拒否する | 影を落とす段は別の項目で追加する |

`GBuffer` は、GBuffer を書く最後のパスの後に当たり、`--visibility-buffer` の on / off で位置は変わらない。同じ段の拡張は `Order` の小さい順に並び、同じ `Order` なら登録順に並ぶ。

パス名の規約は `Ext.<Game>.<Name>` とする。`Ext.` で始まらない名前と、登録済みまたは保留中の同名パスは拒否される。`IViewPass` と `IRenderGraphPass` の両方を実装したパスだけを登録でき、レガシー経路のパスは拒否される。ファクトリは生成関数ポインタと `context` の組であり、登録時に一度呼んで返された実体の型を検査し、初期化せず破棄する。実際の差し込みや再構築でも呼ばれるため、`context` は登録解除の反映まで有効に保ち、ファクトリから登録表へ再入しない。

登録・解除・有効状態の変更は GameThread から `Register`・`Unregister`・`SetEnabled` で行い、mutex で守られた保留の列に積む。初期化の窓では即時に反映され、モジュールの登録と `OnPostInitialize` の後、ゲームスレッドが走り出す前に `CloseInitializationWindow` で窓を閉じる。呼ばれない場合は最初の `ApplyPending` で閉じる。以後、通常の登録操作による `m_Passes` と後段スタックの変更は RenderThread の `SceneView::Render(ViewRenderContext&)` 冒頭の `ApplyPending` で行う。`SetupDeferredPipeline` で組み直す場合は末尾の `ReinsertAll` が登録済みの拡張を入れ直し、既にある実体は重複して挿入しない。

拡張パスは守りの包みを通して呼ばれる。`Initialize` の失敗や `Setup`・`Declare`・`Execute` などで発生した例外はそのパスの実体だけを無効にし、失敗のログをその実体につき一度だけ出す。失敗を `View::Render` に伝播させず、ほかのパスの描画を続ける。後段スタックの初期化後に拡張が追加された場合も、`View::Render` が未初期化のネイティブパスを初期化してから RenderGraph に追加する。

ゲーム側の窓口には `RenderWorld::RegisterRenderExtension(desc, factory)` などがある。描画モジュールは `IRenderModule::RegisterRenderExtensions(RenderExtensionRegistry&)` でも登録でき、この関数はモジュールのインストール後に一度呼ばれる。

`SetupPathTracingPipeline` の経路には拡張を挿入しない。セットアップ時に登録があればその時点で、なければ後から初めて登録された時点で、登録表につき一度だけ警告を出す。

被写界深度の `DepthOfFieldPass` と動きぼけの `MotionBlurPass` は `SetupDeferredPipeline` で、検証用パスの後・後段スタックの前に、この順で追加される。パイプラインの組み直し時にも追加される。

### 拡張データ枠

- キーは `inline constexpr RenderExtensionDataKey<MyParams> kMyKey{Identity::Literal("MyKey", sizeof("MyKey") - 1)};` の形で定義する。値の型 `T` は trivially copyable とする。
- GameThread から `RenderWorld::SetExtensionData(key, value)` または `SetExtensionArray(key, values)` で設定する。バイト列の入口は `SetExtensionBytes`、削除は `RemoveExtensionData(key.Name)`。いずれもロック無しの GameThread 専用で、次の `FramePacket` から反映される。
- 値は不変の `RenderExtensionBlob` として保持する。`LayoutHash` は型名・サイズ・アラインメントのハッシュ、`Count` は要素数、`Revision` は更新の識別値。同じ名前への Set でレイアウト・要素サイズ・要素数・全バイトが前回と一致すれば、同じブロブと `Revision` を使い回す。
- `SnapshotSceneParameters` は `Scene.Extensions` へ共有ポインタだけを写す。変わらないフレームのデータ本体のコピーは 0 で、RenderThread はパケットが共有する不変の値を読む。
- RenderThread のパスは `context.FindExtension(key)` で値、`context.FindExtensionArray(key)` で配列を借用する。`LayoutHash` が合わなければ値は `nullptr`、配列は空の `Span` を返し、ログ有効時はブロブごとに一度だけ警告する。借用はそのブロブの共有参照が保持されている間だけ有効。
- パスはブロブの `Revision` を比較し、必要なときだけ GPU へアップロードする。

### 拡張データと履歴の約束

- GPU 資源をブロブに入れない。GameThread は GPU に触れない。
- MB 級の配列を毎フレーム Set しない。同じバイト列でも比較の費用は掛かる。
- 1 つの View に複数の Viewport があると、パスは Viewport ごとに `Declare`・`Execute` される。パス内の履歴は Viewport の id で分ける。
- 拡張データの変化は `ComputeSceneRevisionHash` に含めず、それだけでは RTGI・DDGI の履歴をリセットしない。

### 拡張パスの時間（ExtensionFrame）

`ViewRenderContext::ExtensionFrame`（`RenderExtensionFrameInfo`）は、描画するパケットのゲーム時間と、最後に描いたパケットの時間を渡す。

| メンバ | 意味 |
|---|---|
| `FrameNumber` | パケットのゲームのフレーム番号 |
| `GameTotalTime` | パケットに記録したゲームの累積時間（秒） |
| `GameDeltaTime` | パケットに記録したゲームのフレーム時間（秒） |
| `PreviousRenderedTotalTime` | RenderThread が最後に描いたパケットの `GameTotalTime`。初回は `GameTotalTime - GameDeltaTime` |

- `ViewRenderContext::DeltaTime` は通常 RenderThread のフレーム時間であり、ゲームの dt ではない。ゲーム時間には `ExtensionFrame` を使う。決定的撮影時は `DeltaTime` もパケットの固定刻みに置き換わる。
- `PreviousRenderedTotalTime` は `RenderedObjectHistory` が渡す値で、描画がゲームのフレームを飛ばしても、物体の前の位置を最後に描いたフレームへ付け替える処理と同じ基準になる。

### TAA と半透明

`Scene.Reactive` は TAA 前の半透明による被覆率を R8_UNORM で表す。ForwardPass は材質の `bWritesReactive` の印のある半透明の描画だけを、主の描画の後に Scene.Reactive へ同じブレンドで描き直し、Transparent 段の拡張パスは `ReactiveTargetBinder` で書ける。
TAA はリアクティブに応じて現在の色の割合を 1 へ寄せ、前フレームのリアクティブを専用の R8 履歴で減衰させながら持ち越す。色の履歴を収める箱の半幅は現在のリアクティブで広げる。
雨や粒子は `PostTemporalAA` 段で TAA の後に合成することを推奨する。この経路ではリアクティブも velocity も要らない。
リアクティブは湯気など TAA の前に描く半透明に使う。

### 拡張パスの補助

- `GBuffer` の段では `GBufferTargetBinder` を使える。パスの `Initialize` でデバイスを渡し、`Declare` で 5 枚の色と深度の Load/Store を宣言する。`Execute` で `Prepare` が成功したら `BeginRenderPass(GetRenderPass(), GetFramebuffer())` へ進む。描画先が替わると Framebuffer、形式が替わると RenderPass も作り直す。RenderPass が替わったらパス側のパイプラインも作り直し、`Shutdown` では `Reset` する。
- `PostTemporalAA` の段では `ResolveHdrChainHead(builder)` が `SSR.SceneColor`、次に `Scene.Color` の順で存在する資源名を返す。どちらも無ければ空の `Identity` を返す。アクセスは宣言しないので、パス自身が戻り値を使って必要な読み書きを宣言する。

## 今後の拡張方針

- pass 間の texture state と lifetime は RenderGraph を正とし、`SharedResourceRegistry` へ戻さない。
- frames-in-flight を増やす場合は、command buffer、descriptor、uniform allocation、resource retirement を frame index 単位で分離する。
- RHI コメントや型名には backend 固有語を出さず、backend 実装ファイル内でだけ Vulkan/DX へ変換する。
