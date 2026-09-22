# NEXT_FINDINGS

- [R6-a][resolved 2026-09-22] 最終ソース更新後にRenderingVelocityVulkanTestを再リンクし、GPU 6/6、関連CTest 17/17、Game build、Game 120フレームを再実行した。証拠は`.harness/runs/20260922-r6a-final/`と19:09:37更新の`Game.log`。
- [R6-a][resolved 2026-09-22] 解析サンプルの許容誤差を0.002へ締め、カメラのみ期待`0.0151554`/実測`0.015152`、物体のみ期待`-0.0649519`/実測`-0.0649414`、併用期待`-0.0497965`/実測`-0.0497742`、背景画素`(255,0)`の`0`/`0`を確認した。

- [R6-a][resolved 2026-09-22] `World::SyncEntityRecursive` が現フレームのproxyを毎フレーム更新し、proxy公開後に`MeshComponent`の変換履歴をcommitするよう修正した。`move-then-stop` GPU readbackで停止後 `max_magnitude=0` / `non_zero=0` を確認した。
- [R6-a][resolved 2026-09-22] GPU検証へ既知のカメラ/物体点の解析投影値、初回と安定静止の別frame、移動後停止を追加した。カメラのみは期待`0.0151554`/実測`0.015152`、カメラ＋物体は期待`-0.0497965`/実測`-0.0497742`だった。
- [R6-a][resolved 2026-09-22] 最終ソースでGame build、関連CTest 17/17、velocity GPU CTest 6/6、既定Game 120フレーム起動ログを再実行し、受入れ記録へ反映した。

- [R4-P4][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。
- [R4][non-blocking][資源寿命] DDGIProbePassのframe slot資源resetと前slot atlas参照に対し、VulkanTexture側の遅延削除キューは無い。slot再利用前のfence待機条件を受入れ記録へ明記し、VUID陽性対照と合わせて確認する。R4受入れでは9件CTestと実GPU captureのVUID_COUNT=0を確認したが、陽性対照は別追跡とする。
- [R4][non-blocking][数値範囲] RG16F distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限と、visibility floor/normal biasの漏れ・自己遮蔽を別の数値契約で固定する。
- [R4][non-blocking][性能] probe relocation/classification、自動配置、鏡面GI、反射・透過、GPU性能計測はR4の対象外であり、性能gateはDeferredとする。
- [R4][non-blocking][same-slot履歴] 現在のMAX_FRAMES_IN_FLIGHT=1では同一frame slotの履歴swapが実FrameNumberを保ったまま動作する。将来slot数を増やす場合は、same-slot履歴にも直前frame判定を加え、古いatlas chainを混在させない。
- [R4][non-blocking][受入れ運用] DDGI無効A/Bは同一プロセス内で無効baseline→有効capture→無効verifyを比較する経路であり、別プロセス間の再現性検証やvalidation陽性対照は別ゲートとする。Cornell threshold TSVのschema header統一も別整理とする。
- [R5-P5][non-blocking][容量] `VulkanDevice::CreateAccelerationStructure`/`Build`のBLAS size queryを実ジオメトリ条件で再問い合わせし、保存済みstorage/scratch容量との比較を追加する。現行の同期BuildはGPU queryとresource寿命を検証済み。
- [R5-P5][non-blocking][入力usage] triangle BLASのvertex/index入力がBDAだけでなく`VertexBuffer`/`IndexBuffer` usageを満たす契約を追加し、StorageBufferだけの入力を拒否するケースを固定する。
- [R5-P5][non-blocking][同期] 現行の同期Buildはqueue waitIdleで完了を保証する。将来非同期Buildへ拡張する場合はdst stageのRayTracingShaderを含むbarrierとfence依存を再検証する。
- [R5-P5][non-blocking][行末] `VulkanDevice.cpp`のP5範囲外に残るLF/CRLF混在を、対象範囲を分離した整理タスクとして扱う。
- [R5][non-blocking][swapchain同期] `VK_LAYER_VALIDATE_SYNC=1`で既存のNoCaster系swapchain画像に報告された`SYNC-HAZARD-WRITE-AFTER-READ`を、RT影専用経路とは分離したswapchain acquire/presentの同期課題として追跡する。
- [R5][non-blocking][テスト注入] `VulkanDevice.cpp`のGPU同期失敗注入フックを製品コードから分離またはビルド時ガードする。現行のRHITextureUpdateVulkanTestは注入経路を検証済み。
- [R5][non-blocking][全体build] Physics static assertを含む既知の全体build失敗と、R5対象外の既知CTest失敗を分離したまま、クリーンクローンのconfigure/ALL_BUILD再検証を別ゲートで行う。
- [R5][non-blocking][行末] `RayTracingSceneSubsystem.h`、TASKS/PROGRESSの既存運転履歴に残るLF/CRLF混在を、意味差分と分離して整理する。

## 反復 2 — 評価者(claude)の判定: RESOLVED

対象: R6-P1 RTGI結果形式とfallback契約を実装する

## 修正結果

R6-P1-FIXでSceneRevisionをシーン構成だけへ限定し、History側revision差を即時fallbackに使わず、構成revision不一致時だけ公開結果を不採用にした。履歴revisionが1つ前でもRTGI選択を維持し、構成変更時にfallbackへ戻る契約テストを追加した。証拠は`.harness/runs/20260922-200402/verify-R6-P1-FIX-1.txt`と`verify-R6-P1-FIX-2.txt`。

## 判定理由

証拠は実在し対象タスクのものです。`verify-R6-P1-1.txt` は Game build の EXIT_CODE=0、`verify-R6-P1-2.txt` と `recheck-R6-P1-2-2.txt` は指定 CTest 3/3 passed を示し、私の再実行でも `RenderingDDGILightingContractTest` と `RayTracingSceneSnapshotTest` は 2/2 passed でした。差分は `paths:` の内側に収まり、numstat と `--ignore-cr-at-eol --numstat` は一致、新規ヘッダは UTF-8 BOM + CRLF です。

しかし done-when の「FramePacket の scene/light revision」と「履歴 resource の current/history 公開」の**契約の意味**が、このタスクが前提とする R6-M1 の方式選定(`Docs/RenderingValidation/R6TechniquePlan.md`)と矛盾しています。

**未達条件: scene/light revision と履歴契約が R6-M1 の意味論を満たさない**

- 計画 88 行目は「環境、材質、TLAS 構成の変更は全画面履歴を無効化」、96〜97 行目は「カメラ・物体移動は R6-a velocity と depth/normal 棄却で pixel 単位に処理し、**全画面リセットを常用しない**」、95 行目は「動的ライトの revision 変化中は history weight の上限を 0.25 へ落とし、2 rendered frames 後に通常値へ戻す」と定めています。
- 実装の `HashSceneRevision`(`RenderingCoordinator.cpp:84-159`)は全 DrawCommand の `WorldMatrix`/`NormalMatrix`、全 `InstanceData` の `World`/`PreviousWorld`、全 RT インスタンスの `transform` をハッシュします。物体が 1 つでも動いている間は `PreviousWorld != World` なので **毎フレーム SceneRevision が進みます**。UI board の DrawCommand(`AppendRebasedDrawCommands` 経由で同じ配列に入る)の変化でも進みます。
- `RTGIHistoryResources::IsForFrame`(`RTGIContract.h:216-225`)は `History.SceneRevision == sceneRevision && History.LightRevision == lightRevision` を要求し、`ResolveRTGIIndirectLighting`(同 274-289 行)はそれが崩れると `ResourceUnavailable` で DDGI/IBL へ落とします。前フレームの履歴は定義上「前の revision」を持つため、物体が動いている限り履歴は常に不完全 → RTGI は選ばれず、8 フレーム履歴も「ライト移動時は weight 抑制」も成立しません。これは P2/P3 がこの契約を変えない限り実現不可能で、P1 の目的(契約固定)に反します。
- テスト `TestRTGIFallbackContract` はこの意味論を検証していません(不完全 result の拒否と fallback 順序のみ)。

**再現(静的読解)**: `RenderingCoordinator.cpp:1976` で毎フレーム `UpdateFrameRevisions` → `HashSceneRevision` が `instance.PreviousWorld` を含む → 移動中の物体で毎フレーム `m_SceneRevision` 増加 → `RTGIHistoryResources::IsForFrame` が偽。

**最小の直し方(候補)**

1. `HashSceneRevision` から per-object の変換(`WorldMatrix`/`NormalMatrix`/`World`/`PreviousWorld`/RT `transform`)を外し、計画どおり「構成」だけをハッシュする: DrawCommand/RT インスタンスの集合(Mesh/Material/ObjectId の並び)、材質パラメータ、Sky/DDGI/Fog、TLAS インスタンス数・opaque フラグ。ライト revision は現状の内容で可(位置変化を含めるのは計画に合致)。あわせて `RTGIHistoryResources::IsForFrame` は **History 側の revision 一致を要求せず** Current 側だけを見る形にし、History の revision 差は P3 が weight 抑制/棄却に使う入力として残す。
2. 代替: revision を「構成 revision」と「変換 revision」に分け、履歴契約の完全性判定は構成 revision のみで行う。こちらは FramePacket のフィールドが増えるので 1 案のほうが小さい。

いずれの案でも `TestRTGIFallbackContract` に「History の revision が 1 つ前でも `IsComplete()`/RTGI 選択が維持される」ケースと「Scene 構成変更で不採用になる」ケースを足してください。

## non-blocking

- `LightingPass::Declare` は `RTGIDiffuseIndirect` を読むだけで `Execute` は結果を消費しません。現状は誰も `PublishRTGI` を呼ばないので到達不能ですが、将来 `Source == RTGI` になると `bUseDDGILighting` が偽になり RTGI も DDGI も適用されない状態になります。P2 で束ねる前提なら PROGRESS か TASKS に明記してください。
- `HashSceneRevision` は毎フレーム全 DrawCommand・InstanceData・RT インスタンスを走査します(GameThread の CommandGeneration 内)。大規模シーンでの費用は R4/R5 と同じ性能 gate の追跡事項に入れてください。
- `m_LastSceneRevisionHash == 0u` を「初回」の番兵に使っているため、ハッシュがちょうど 0 になった場合に初回扱いされます。確率は無視できますが、`bool m_bRevisionHashValid` のほうが正確です。
- 英語コメント・文言の混入はありませんでした。

## 実行できなかったコマンド

- `cmake --build build --config Debug --target Game -- /m:1`(保存された `verify-R6-P1-1.txt` の EXIT_CODE=0 を採用)
- `RenderGraphCompileTest`(約 75 秒。保存された `verify-R6-P1-2.txt` と `recheck-R6-P1-2-2.txt` の Passed を採用)

`.harness/lessons/` は存在しないため教訓の選別は行っていません。

## R6-P4 評価の非blocking追跡事項

- GPUテストはtemporalの入力radianceをreadbackし、denoised textureは公開状態とSceneColor差分で間接確認している。denoised値を直接読むtest seamまたはgolden比較は追加余地として残す。
- cross-bilateralのdepth閾値は非線形GBuffer depthの差を使うため、遠景で境界棄却が緩む可能性がある。material差にはAO成分も含むため、tapが過剰に棄却される可能性と合わせてR6-P5/P6の受入れcaptureで観察する。

## 反復 4 — 評価者(claude)の判定: NEEDS_WORK

対象: R6-P1-FIX revisionとRTGI履歴契約の意味論を修正する

**未達条件: done-when 第1項「SceneRevision が毎フレームの物体変換ではなくシーン構成の変更を表す」**

差分は `HashSceneRevision` から `WorldMatrix`/`NormalMatrix`/`World`/`PreviousWorld`/TLAS `transform` の直接ハッシュを外しましたが、ハッシュの入力である `packet.DrawCommands` と `packet.RayTracingScene.Instances` は依然としてカメラ位置と物体変換に依存する「順序付き・カリング済み」の列です。そのため物体やカメラが動くと構成が変わっていなくても revision が進みます。

- `SceneView::GenerateCommands`(`Library/Core/Private/Rendering/SceneView.cpp:1000-1020`)は `CalculateSortKey(SortDepth, …)` の後に `DrawCommandSorter::Sort(m_OpaqueCommands, FrontToBack)` を行い、`SortDepth` は `CullProxies`(同 `:826`)でカメラ距離から毎フレーム再計算されます。`HashSceneRevision`(`RenderingCoordinator.cpp:91-114`)は `hash ^= byte; hash *= prime` の逐次ハッシュで **順序依存** なので、2つの描画コマンドの奥行き順が入れ替わるたびに(物体移動・カメラ移動の双方で)`m_SceneRevision` が増えます。
- `CullProxies`(`SceneView.cpp:778-827`)は既定で frustum/distance カリング有効(`SceneView.h:22,24`)なので、カメラ回転で物体が視錐台を出入りするだけで `DrawCommands.size()`・`FirstInstance`/`InstanceDataOffset`・`InstanceData.size()` が変わり revision が進みます。
- `RayTracingSceneSubsystem::BuildFrameSnapshot`(`RenderingCoordinator.cpp:575-576`)は `OpaqueCommandRange` からその順序で `Instances` を組むため、同じ理由で `Instances` のハッシュも動きます。
- 計画(`Docs/RenderingValidation/R6TechniquePlan.md:88,96-97`)は scene revision 差を「全画面履歴無効化」に、カメラ・物体移動は「pixel 単位」に割り当てています。上記の状態では通常のカメラ操作中に P3 が全画面リセットを常用することになり、前回指摘と同じ欠陥が1段間接になっただけです。
- 追加テスト(`RenderingDDGILightingContractTest.cpp:468-472`)は `HashSceneRevision` のソースに `WorldMatrix` 等の字句が無いことしか見ておらず、この性質を反証していません。

再現(静的読解): 2物体のシーンでカメラを前後に動かし、2物体の `SortDepth` の大小が入れ替わるフレーム → `m_OpaqueCommands` の並びが入れ替わる → `HashSceneRevision` の値が変わる → `UpdateFrameRevisions`(`:1733-1744`)で `m_SceneRevision++`。

**最小の直し方(候補)**

1. ハッシュを**順序非依存・カリング非依存**にする。項目ごとのハッシュを可換に結合(XOR/加算)し、入力を「並び替え・カリング前」の集合にする。具体的には `SceneView` の proxy 収集時点(`m_MeshProxies`/`m_BoardProxies`、`ObjectId`・`MeshHandle`・`MaterialHandle`・`bCastShadow`・材質定数)と、TLAS 側は `customIndex`・`MeshHandle`・`IndexOffset/Count`・`Material` を要素ハッシュにして可換結合し、`FirstInstance`/`InstanceDataOffset`/`size()` のような配置由来の値は外す。
2. 代替: revision の計算を `GenerateDrawCommands` のカリング/ソート前(SceneProxy 更新側)へ移し、`FramePacket` へ値コピーする。案1のほうが差分が小さい。

いずれの案でも契約テストに「同じ描画コマンド集合を順序だけ入れ替えた/1つを frustum 外へ出した FramePacket で revision が変わらない」ケースを足してください(`HashSceneRevision` が無名名前空間なら、集合ハッシュを公開ヘルパに切り出してテストする)。

**満たされている点**

- `IsForFrame` が Current 側だけを見て、History の差を `HasHistoryRevisionMismatch` で保持する契約と、履歴 revision が1つ前でも RTGI が選ばれる/現フレーム revision 不一致で `ResourceUnavailable` へ戻るテストは追加済み。私の再実行でも `RenderingDDGILightingContractTest.exe` と `RayTracingSceneSnapshotTest.exe` は exit 0。
- 証拠 `verify-R6-P1-FIX-1.txt`(EXIT_CODE=0)、`verify-R6-P1-FIX-2.txt`・`recheck-R6-P1-FIX-4-2.txt`(3/3 passed)は実在し対象タスクのもの。対象コミット `190e812` の変更は `paths:` 内、`--numstat` と `--ignore-cr-at-eol --numstat` は一致、英語コメント・文言の混入なし。

**non-blocking**

- 範囲内の `cc3dab4 作業途中の保存` は R6-P2 の RTGI compute/`lighting.frag` 変更を含む WIP コミットで、対象タスクの差分ではないため判定対象外としました。ただし `Assets/Shaders/RTGI/` が未追跡のまま `LightingPass` がそれを `LoadShader` する状態なので、P2 側で整合を取ってください。
- 「構成変更で不採用」のテストは「公開済み result の revision が現フレームと不一致」を模しており、履歴側の不採用そのものは `HasHistoryRevisionMismatch` の消費者(P3)に委ねられています。P3 の done-when に「mismatch 時に履歴を棄却する」を明記してください。
- 字句の不在を確認するソース文字列テストは実装の書き換えで容易に無効化されるため、性質テストに置き換えるのが望ましいです。

**実行できなかったコマンド**

- `cmake --build build --config Debug --target Game -- /m:1`(保存された `verify-R6-P1-FIX-1.txt`・`recheck-R6-P1-FIX-4-1.txt` の exit 0 を採用)
- `RenderingVelocityCameraVulkanTest`(GPU テスト。保存された `verify-R6-P1-FIX-2.txt` の Passed を採用)

`.harness/lessons/` は存在しないため教訓の選別は行っていません。

## 反復 5 — 評価者(claude)の判定: NEEDS_WORK

対象: R6-P2 1 bounce ray-query GIを接続する

対象: R6-P2 1 bounce ray-query GIを接続する(実装本体は範囲直前の `cc3dab4` と範囲内の `a684739`/`f22d31a`)

**未達条件: done-when 前半「compute shader内のray queryでTLAS hit/missを処理し、有限なdiffuse indirect radianceをLightingPassへ渡す」に実行時の証拠がない**

- 証拠 `verify-R6-P2-1.txt`(Game build EXIT_CODE=0)と `verify-R6-P2-2.txt`(2/2 passed)は実在し対象タスクのものです。しかしその2テストは新コードを一切通しません。
  - `DDGIProbeRayQueryVulkanTest.cpp:820-830` は `context.RTGICapability` も `bRTGITLASAvailable` も設定せず(既定 false)、`lightingPass.Execute(context)`(レガシー経路 `LightingPass.cpp:2228-2266`)は `SharedResources` から GBuffer だけを取り、RTGI テクスチャは null のまま `ExecuteWithInputs` へ渡します。`ExecuteRTGI`(`:2737-2750`)は最初の条件で return false します。私が同テストを直接実行した出力(7行)にも RTGI の痕跡はありません。
  - `RenderingRayTracingShadowVulkanTest.cpp` は `LightingPass` を参照していません(grep 0件)。
- `ExecuteRTGI` の失敗はすべて「警告ログ+fallback」に吸収されます(shader ロード失敗 `:2624`、pipeline 生成失敗 `:2635`、例外 `:2941-2953`)。したがって shaderc での compile 失敗・descriptor layout 不一致・dispatch の validation error が起きても、列挙された verify は緑のままです。done-when の肯定側(hit/miss 処理、有限な radiance の受け渡し)が「もっともらしい」だけで、反証可能な証拠がありません。
- 私が確認できた静的整合(参考): `glslangValidator --target-env vulkan1.2 -S comp` は exit 0。`RTGIInstanceData` は C++/GLSL とも 112 byte、light 構造体・型判定(`position.w`)・spot 減衰・equirect UV・`ReconstructWorldPosition` は `lighting.frag` と一致。BLAS の頂点は `Mesh3DVertex`(Position 先頭、R32G32B32、uint32 index)で shader の fetch と一致。`UpdateLightBuffer`(`:3004`)は `ExecuteRTGI`(`:3040`)より先。RenderGraph は transient を毎フレーム `Undefined→UnorderedAccess` へ遷移するので早期 return 時の layout 不整合はなし。R5 の `RayTracingShadowPass`/`RHI`/`Assets/Shaders/RayTracing` に変更なし。

**再現手順**

```
build/Test/Core/Rendering/Debug/DDGIProbeRayQueryVulkanTest.exe   # exit 0、出力に RTGI なし
grep -c 'LightingPass' Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp   # 0
```

**最小の直し方(候補)**

1. (推奨)`Test/Core/Rendering/RTGIDiffuseIndirectVulkanTest.cpp` を `DDGIProbeRayQueryVulkanTest` を雛形に追加する。`context.RTGICapability = MakeRTGIRayQueryCapability(capabilities)`、`bRTGITLASAvailable = packet.HasCompleteRayTracingScene()` を設定し、`R16G16B16A16_FLOAT` / `ShaderRead|ShaderWrite` のテクスチャを作って friend 経由で `ExecuteWithInputs` に渡す。readback で (a) 全 texel が有限、(b) emissive 三角形へ向く画素の radiance > 0、(c) `context.PhysicalLighting.RTGI.bPublished == true` を assert。さらに `bRTGIEnabled=false` と TLAS 不完全の2ケースで `bPublished == false` かつ scene color が fallback と一致することを assert する。`TASKS.md` の verify にこのテストを追加し、証拠を保存する。
2. 代替(弱い): Game を起動してログを保存し、`RTGI ... できません` / `既存間接光へ戻ります` の警告が無いことと、RTGI 有効/無効のスクリーンショット差を証拠にする。案1のほうが再現可能で、以後の P3 でも回帰検出に使える。

**満たされている点**

- RT 非対応・無効化・TLAS 不完全・資源生成失敗・例外の各経路が `ExecuteRTGI` で false を返し、`ResolveIndirectLighting` が DDGI→IBL→raster を選ぶ構造になっている(`:3047-3072`、`lighting.frag:1137-1189`)。
- 範囲内の変更は `paths:` 内。`--numstat` と `--ignore-cr-at-eol --numstat` は範囲・WIP コミットとも一致。英語コメント・文言の混入なし。`Assets/Shaders/RTGI/DiffuseIndirect.comp` は追跡済み(前回の non-blocking は解消)。

**non-blocking**

- `m_RTGIComputeParametersBuffer`・`m_RTGIComputeInstanceDataBuffer`・`m_RTGIComputeDescriptorSet` は単一実体を毎フレーム `Update` している。既存の `m_LightDataBuffer`/`m_LightingDescriptorSet` と同じ流儀だが、`DDGIProbePass` は `frameResources` で frame-slot 分離している。in-flight フレームとの競合が無い根拠(RHI 側で更新が遅延/複製されるか)を P3 で確認しておくとよい。
- `RTGIInstanceData::Transform` は shader 側で未使用(法線は `rayQueryGetIntersectionWorldToObjectEXT` から取得)。
- `history.History` に現フレームのテクスチャと `AgeFrames=0` を入れており、実質 placeholder。P3 の履歴接続で置き換わる前提として TASKS に明記しておく。
- `EvaluateRTGIEndpoint` の diffuse 項に Fresnel kD が無い(IBL 経路は kD を掛ける)ため、金属寄りの面でわずかに過大。R6 の見た目調整で扱えばよい。

**実行できなかったコマンド**

- `cmake --build build --config Debug --target Game -- /m:1`(保存された `verify-R6-P2-1.txt`・`recheck-R6-P2-5-1.txt` の exit 0 を採用)
- `RenderingRayTracingShadowVulkanTest`(保存された `verify-R6-P2-2.txt`・`recheck-R6-P2-5-2.txt` の Passed を採用。`DDGIProbeRayQueryVulkanTest` のみ直接実行し exit 0 を確認)

`.harness/lessons/` は存在しないため教訓の選別は行っていません。

## 反復 7 — 評価者(claude)の判定: 判定不能

対象: R6-P1-FIX-2 SceneRevisionをカリング・ソート非依存へ修正する

1行目が PASS/NEEDS_WORK ではなかった(1行目: 評価は完了しました。判定を書きます。)。回答をそのまま貼る:

…(先頭を省略)
Component::CurrentLODLevel` 由来で、カメラ距離から自動更新する箇所は無い(grep で代入なし)ため構成値として妥当。
- `RevisionSetAccumulator`(count / xor / sum of mixed)により集合の順序に依存しない。DrawCommands・InstanceData・RayTracingScene は参照しない(ソースにも無く、テストの source-scan assert でも固定)。
- 性質テスト `TestSceneRevisionIsCompositionOnly` はカメラ移動・物体/Skinned 移動・DrawCommands 全削除・RT instance 削減・proxy/UI(Board)/RT の並べ替えで不変、追加・削除・メッシュ変更・材質変更・環境(SunAltitude)変更で変化することを assert している。
- R6-P1 の履歴 revision 契約(`UpdateFrameRevisions` 本体、`HasHistoryRevisionMismatch`)と FramePacket の値所有は差分で触れていない。

**証拠**

- `verify-R6-P1-FIX-2-1.txt`: Game ビルド `EXIT_CODE=0`(167行、Game.exe 生成まで確認)。
- `verify-R6-P1-FIX-2-2.txt`: 指定3テスト `100% tests passed`(RayTracingSceneSnapshotTest / RenderingVelocityCameraVulkanTest / RenderingDDGILightingContractTest)。`recheck-R6-P1-FIX-2-7-1/2` も同結果。
- verify のビルド対象は `Game` だけでテスト実行ファイルの再ビルドを保証しないため、`RenderingDDGILightingContractTest.exe`(00:16 生成、コミット 00:23 の直前)に `HashSceneRevisionInternal` と `packet.Scene.SkinnedMeshProxies` の文字列が含まれることを確認し、私自身も再実行した:

```
ctest --test-dir build -C Debug -R "^RenderingDDGILightingContractTest$"
1/1 Test #108: RenderingDDGILightingContractTest ...   Passed    0.03 sec
100% tests passed, 0 tests failed out of 1
```

- 行末: `git diff --numstat` と `--ignore-cr-at-eol --numstat` は5ファイルとも一致。変更ファイルはすべて `paths:` 内。英語コメント・文言の混入なし。

**non-blocking**

- 旧ハッシュにあった `InstanceData.ObjectColor` は新ハッシュに無い(MeshProxy に無いので proxy 集合からは観測不能)。R6-P3 の履歴側 material 棄却で概ね吸収されるが、物体色だけを変えた場合に revision が変わらないことは TASKS の R6-P3 の前提として明記しておくとよい。
- `MeshProxy::CustomData` はハッシュに入るため、ゲーム側が毎フレーム CustomData をアニメーションさせると revision が毎フレーム変わり RTGI 履歴が積み上がらない。旧ハッシュも同じだったので回帰ではない。
- 今後の verify では `--target Game RenderingDDGILightingContractTest` のようにテスト実行ファイルもビルド対象へ含めると、stale バイナリでの緑を防げる。

**実行できなかったコマンド**

- `cmake --build build --config Debug --target Game -- /m:1`(保存出力の exit 0 を採用)
- `RayTracingSceneSnapshotTest` / `RenderingVelocityCameraVulkanTest`(GPU テスト。保存出力の Passed を採用。契約テストのみ直接再実行)

`.harness/lessons/` は存在しないため教訓の選別は行っていません。
